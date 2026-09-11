#include <RH_RF95.h>

/* ============================================================
   ROVER PRINCIPAL (ESP32: Control de Motores, Sensores y LoRa)
   ------------------------------------------------------------
   Responsabilidades:
   1. Escuchar comandos LoRa enviados desde el Gateway / Dashboard.
   2. Procesar comandos (dirección, STOP, cambio de modo AUTO/MANUAL)
      y responder inmediatamente un ACK (ID=...|OK) por LoRa.
   3. Es la FUENTE DE VERDAD del modo de operación (AUTO / MANUAL).
      Todos los navegadores se sincronizan con este estado.
   4. Recopilar telemetría de los 24 sensores, calcular el Checksum
      CRC (mod 256) y transmitirlo periódicamente por LoRa.
============================================================ */

#define LORA_RX 16
#define LORA_TX 17

RH_RF95<HardwareSerial> rf95(Serial2);

// ============================================================
//                     ESTADO DEL ROVER
// ============================================================
String modoRover    = "MANUAL";    // "MANUAL" o "AUTO" (fuente de verdad)
String estadoRover  = "DETENIDO";  // "DETENIDO", "ADELANTE", "ATRAS", etc.
float  voltajeBateria = 12.45;     // Voltaje (puede leerse de un divisor en pin ADC)
float  anguloSimulado = 45.0;

// Coordenadas GPS del Rover y destino en modo AUTO
float latRover = 4.609710;
float lonRover = -74.081750;
float targetLat = 0.0;
float targetLon = 0.0;
bool  targetDefinido = false;

// Coordenadas Home (Base de retorno reescribible)
float homeLat = 0.0;
float homeLon = 0.0;
bool  homeDefinido = false;

// ============================================================
//         CONFIGURACIÓN DE SEGURIDAD ANTI-VUELCO
// ============================================================
const float UMBRAL_VUELCO_GRADOS = 35.0f;       // Inclinación máxima permitida (> 35°)
const unsigned long TIEMPO_VUELCO_MS = 300;     // Duración continua para disparar corte (> 300 ms)

float actualImuX = 0.0f;
float actualImuY = 0.0f;
float actualImuZ = 0.98f;
float inclinacionActual = 0.0f;
float pitchActual = 0.0f;
float rollActual = 0.0f;

bool inclinacionPeligrosa = false;
unsigned long inicioPeligroVuelco = 0;
bool emergenciaVuelcoActiva = false;

unsigned long ultimoEnvioSensores = 0;
const unsigned long intervaloTelemetria = 1500; // Envío cada 1.5 segundos

// ============================================================
//         CONTROL DE MOTORES Y CORTE DE SEGURIDAD
// ============================================================
void detenerMotores() {
  /* --------------------------------------------------------
     AQUÍ SE CORTAN LOS PINES FÍSICOS DE TUS MOTORES (PWM = 0)
     ej: analogWrite(PIN_PWM_IZQ, 0); analogWrite(PIN_PWM_DER, 0);
     -------------------------------------------------------- */
  estadoRover = "DETENIDO";
}

void actualizarInclinacionIMU(float ax, float ay, float az) {
  actualImuX = ax;
  actualImuY = ay;
  actualImuZ = az;

  // Roll: inclinación lateral (Y vs Z)
  float rollRad = atan2(ay, az);
  rollActual = rollRad * (180.0f / 3.14159265f);

  // Pitch: inclinación longitudinal (X vs plano YZ)
  float pitchRad = atan2(-ax, sqrt(ay * ay + az * az));
  pitchActual = pitchRad * (180.0f / 3.14159265f);

  // Inclinación combinada
  inclinacionActual = sqrt(pitchActual * pitchActual + rollActual * rollActual);

  // Seguridad Anti-Vuelco: Inclinación > 35° sostenida por más de 300 ms continuos
  if (inclinacionActual > UMBRAL_VUELCO_GRADOS) {
    if (!inclinacionPeligrosa) {
      inclinacionPeligrosa = true;
      inicioPeligroVuelco = millis();
    } else if (millis() - inicioPeligroVuelco >= TIEMPO_VUELCO_MS) {
      if (!emergenciaVuelcoActiva) {
        emergenciaVuelcoActiva = true;
        detenerMotores();
        estadoRover = "EMERGENCIA_VUELCO";

        Serial.println();
        Serial.println("**************************************************");
        Serial.println("¡¡¡ALERTA CRÍTICA: PARADA DE EMERGENCIA POR VUELCO!!!");
        Serial.print("Inclinación crítica: ");
        Serial.print(inclinacionActual, 1);
        Serial.print("° (> 35°) sostenida por ");
        Serial.print(millis() - inicioPeligroVuelco);
        Serial.println(" ms.");
        Serial.println("MOTORES CORTADOS INMEDIATAMENTE POR EL ESP32.");
        Serial.println("**************************************************");
      }
    }
  } else {
    // Si la inclinación se normaliza por debajo de 25° (histeresis de 10°)
    inclinacionPeligrosa = false;
    if (emergenciaVuelcoActiva && inclinacionActual < 25.0f) {
      emergenciaVuelcoActiva = false;
      estadoRover = "DETENIDO";
      Serial.println("Rover recuperó estabilidad (< 25°). Parada de emergencia rearmada.");
    }
  }
}

// ============================================================
//                     CÁLCULO DE CRC (MOD 256)
// ============================================================
uint8_t calcularCRC(const String& payload) {
  uint8_t sum = 0;
  for (size_t i = 0; i < payload.length(); i++) {
    sum = (sum + (uint8_t)payload[i]) % 256;
  }
  return sum;
}

// Extrae el valor de una clave dentro de una cadena delimitada por '|'
// Ejemplo: extraerCampo("CMD=ADELANTE|ID=abc123", "ID") -> "abc123"
String extraerCampo(const String& texto, const String& clave) {
  String buscada = clave + "=";
  int inicio = texto.indexOf(buscada);
  if (inicio == -1) return "";
  inicio += buscada.length();

  int fin = texto.indexOf('|', inicio);
  if (fin == -1) fin = texto.length();

  return texto.substring(inicio, fin);
}

// ============================================================
//                     ENVIAR ACK POR LORA
// ============================================================
void responderACK(const String& idComando, bool exito, const String& errorMsg = "") {
  if (idComando.length() == 0) return;

  String respuesta = "ID=" + idComando + (exito ? "|OK" : "|ERR=" + errorMsg);

  Serial.print("Enviando ACK por LoRa: ");
  Serial.println(respuesta);

  rf95.send((uint8_t*)respuesta.c_str(), respuesta.length());
  rf95.waitPacketSent();
  rf95.setModeRx(); // Volver inmediatamente a escuchar
}

// ============================================================
//                     PROCESAR COMANDO
// ============================================================
void procesarComando(const String& raw) {
  Serial.println();
  Serial.println("================================");
  Serial.println("COMANDO RECIBIDO POR LORA");
  Serial.print("Mensaje bruto: "); Serial.println(raw);
  Serial.println("================================");

  String cmd = extraerCampo(raw, "CMD");
  String id  = extraerCampo(raw, "ID");

  // Si no viene con formato CMD=..., usar el string completo
  if (cmd.length() == 0) {
    cmd = raw;
  }

  // 0. Seguridad: Si la parada de emergencia por vuelco está activa
  if (emergenciaVuelcoActiva) {
    if (cmd == "STOP" || cmd == "RESET_VUELCO") {
      detenerMotores();
      if (inclinacionActual < UMBRAL_VUELCO_GRADOS) {
        emergenciaVuelcoActiva = false;
        Serial.println("Parada de emergencia restablecida manualmente.");
      }
      responderACK(id, true);
      return;
    }
    if (cmd == "ADELANTE" || cmd == "ATRAS" || cmd == "IZQUIERDA" || cmd == "DERECHA" || cmd.startsWith("NAV_AUTO") || cmd == "RTL") {
      Serial.println("Comando RECHAZADO: Parada de emergencia por vuelco (>35°) activa.");
      responderACK(id, false, "bloqueo_vuelco_critico");
      return;
    }
  }

  // 1. Comando de Cambio de Modo (MODO=AUTO o MODO=MANUAL)
  if (cmd.startsWith("MODO=")) {
    String nuevoModo = cmd.substring(5);
    nuevoModo.toUpperCase();
    if (nuevoModo == "AUTO" || nuevoModo == "MANUAL") {
      modoRover = nuevoModo;
      Serial.print("Modo de operación cambiado a: ");
      Serial.println(modoRover);
      responderACK(id, true);
    } else {
      responderACK(id, false, "modo_invalido");
    }
    return;
  }

  // 2. Coordenadas de Navegación Automática (NAV_AUTO|LAT=...|LON=... o GOTO)
  if (cmd.startsWith("NAV_AUTO") || cmd.startsWith("GOTO") || raw.indexOf("LAT=") != -1) {
    modoRover = "AUTO"; // Sincroniza al rover en modo autónomo
    String latStr = extraerCampo(raw, "LAT");
    String lonStr = extraerCampo(raw, "LON");
    if (latStr.length() > 0 && lonStr.length() > 0) {
      targetLat = latStr.toFloat();
      targetLon = lonStr.toFloat();
      targetDefinido = true;
      Serial.print("NUEVO DESTINO GPS RECIBIDO -> Lat: ");
      Serial.print(targetLat, 6);
      Serial.print(" | Lon: ");
      Serial.println(targetLon, 6);

      /* --------------------------------------------------------
         AQUÍ TU ALGORITMO AUTÓNOMO PUEDE CALCULAR EL RUMBO:
         ej: calcularRumboHaciaDestino(targetLat, targetLon);
         -------------------------------------------------------- */
    }
    responderACK(id, true);
    return;
  }

  // 3. Fijar o Reescribir Punto Home (SET_HOME|LAT=...|LON=...)
  if (cmd.startsWith("SET_HOME") || raw.indexOf("SET_HOME") != -1) {
    String latStr = extraerCampo(raw, "LAT");
    String lonStr = extraerCampo(raw, "LON");
    if (latStr.length() > 0 && lonStr.length() > 0) {
      homeLat = latStr.toFloat();
      homeLon = lonStr.toFloat();
      homeDefinido = true;
      Serial.print("PUNTO HOME ACTUALIZADO/REESCRITO -> Lat: ");
      Serial.print(homeLat, 6);
      Serial.print(" | Lon: ");
      Serial.println(homeLon, 6);
      responderACK(id, true);
    } else {
      responderACK(id, false, "coordenadas_home_invalidas");
    }
    return;
  }

  // 4. Regreso a Home (RTL / VOLVER_HOME)
  if (cmd == "RTL" || cmd == "VOLVER_HOME" || cmd.startsWith("RTL")) {
    if (homeDefinido) {
      modoRover = "AUTO";
      targetLat = homeLat;
      targetLon = homeLon;
      targetDefinido = true;
      Serial.print("RTL ACTIVADO: Regresando a Home -> Lat: ");
      Serial.print(homeLat, 6);
      Serial.print(" | Lon: ");
      Serial.println(homeLon, 6);
      responderACK(id, true);
    } else {
      Serial.println("RTL RECHAZADO: Punto Home no ha sido configurado.");
      responderACK(id, false, "home_no_definido");
    }
    return;
  }

  // 5. Comandos de Movimiento Manual
  if (cmd == "ADELANTE" || cmd == "ATRAS" || cmd == "IZQUIERDA" || cmd == "DERECHA" || cmd == "STOP") {
    if (cmd == "STOP") {
      detenerMotores();
    } else {
      estadoRover = cmd;
    }
    Serial.print("Estado de movimiento actualizado a: ");
    Serial.println(estadoRover);

    /* --------------------------------------------------------
       AQUÍ SE ACCIONAN LOS PINES FÍSICOS DE TUS MOTORES:
       ej: if (estadoRover == "ADELANTE") moverAdelante();
           else if (estadoRover == "STOP") detenerMotores();
       -------------------------------------------------------- */

    responderACK(id, true);
    return;
  }

  // 6. Comando personalizado / Desconocido
  Serial.print("Comando personalizado ejecutado: ");
  Serial.println(cmd);
  responderACK(id, true);
}

// ============================================================
//                     CONSTRUIR Y ENVIAR TELEMETRÍA
// ============================================================
void enviarTelemetria() {
  bool enMovimiento = (estadoRover != "DETENIDO" && estadoRover != "EMERGENCIA_VUELCO");

  // Simulación dinámica de descarga leve de batería y corrientes
  if (enMovimiento) {
    voltajeBateria = max(10.8f, voltajeBateria - 0.001f);
    anguloSimulado = fmod(anguloSimulado + 5.0f, 360.0f);
  }

  float baseCorriente = enMovimiento ? 2.60 : 0.25;

  String paquete = "";
  paquete += "BAT=" + String(voltajeBateria, 2);

  // 10 Sensores de Corriente (C1 a C10)
  for (int i = 1; i <= 10; i++) {
    float corriente = (i <= 4) ? baseCorriente : 0.15;
    corriente += ((float)(random(-10, 10)) / 100.0f); // Ruido de sensor
    if (corriente < 0.0f) corriente = 0.0f;
    paquete += "|C" + String(i) + "=" + String(corriente, 2);
  }

  // 6 Encoders Absolutos (EA1 a EA6, grados 0.0 a 359.9)
  for (int i = 1; i <= 6; i++) {
    float angulo = fmod(anguloSimulado * i * 19.0f, 360.0f);
    paquete += "|EA" + String(i) + "=" + String(angulo, 1);
  }

  // 4 Encoders Incrementales (EI1 a EI4, estado digital 0 o 1)
  for (int i = 1; i <= 4; i++) {
    int valDig = enMovimiento ? (random(0, 2)) : 0;
    paquete += "|EI" + String(i) + "=" + String(valDig);
  }

  // IMU (X, Y, Z) y Seguridad
  paquete += "|IMUX=" + String(actualImuX, 2);
  paquete += "|IMUY=" + String(actualImuY, 2);
  paquete += "|IMUZ=" + String(actualImuZ, 2);
  paquete += "|TILT=" + String(inclinacionActual, 1);
  if (emergenciaVuelcoActiva) {
    paquete += "|VUELCO=1";
  }
  if (homeDefinido) {
    paquete += "|HLAT=" + String(homeLat, 6);
    paquete += "|HLON=" + String(homeLon, 6);
  }

  // Diagnóstico del sistema
  paquete += "|ESP32M=OK";
  paquete += "|ESP32S=OK";
  paquete += "|ESTADO=" + estadoRover;
  paquete += "|MODO=" + modoRover; // ¡Sincroniza a todos los navegadores!
  paquete += "|LAT=" + String(latRover, 6);
  paquete += "|LON=" + String(lonRover, 6);

  // Calcular Checksum CRC mod 256
  uint8_t crc = calcularCRC(paquete);
  char crcHex[5];
  sprintf(crcHex, "%02X", crc);
  paquete += "|CRC=" + String(crcHex);

  Serial.println();
  Serial.print("Transmitiendo telemetría LoRa (");
  Serial.print(paquete.length());
  Serial.println(" bytes)...");

  rf95.send((uint8_t*)paquete.c_str(), paquete.length());
  rf95.waitPacketSent();

  Serial.println("Telemetría enviada con CRC OK");
  rf95.setModeRx(); // Volver inmediatamente a escuchar comandos
}

// ============================================================
//                     SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("ROVER PRINCIPAL LORA INICIADO");
  Serial.println("================================");

  Serial2.setPins(LORA_RX, LORA_TX);
  Serial2.begin(57600, SERIAL_8N1, LORA_RX, LORA_TX);
  delay(200);

  if (!rf95.init()) {
    Serial.println("ERROR INICIANDO LORA");
    while (true) delay(1000);
  }

  if (!rf95.setFrequency(868.0)) {
    Serial.println("ERROR FRECUENCIA LORA");
    while (true) delay(1000);
  }

  rf95.setTxPower(20, false);
  rf95.setModeRx();

  Serial.println("LoRa configurado en 868 MHz");
  Serial.println("Listo para recibir comandos y reportar telemetría");
}

// ============================================================
//                     LOOP
// ============================================================
void loop() {
  // 1. Monitoreo constante de inclinación IMU y seguridad anti-vuelco (> 35° por > 300 ms)
  // (Si tienes sensor MPU6050/BNO055 conectado por I2C, se lee aquí. Por ahora simulación dinámica)
  bool enMovimiento = (estadoRover != "DETENIDO" && estadoRover != "EMERGENCIA_VUELCO");
  float ruidoX = enMovimiento ? ((float)random(-35, 35) / 100.0f) : 0.02f;
  float ruidoY = enMovimiento ? ((float)random(-25, 25) / 100.0f) : -0.01f;
  float ruidoZ = 0.98f + ((float)random(-3, 3) / 100.0f);
  actualizarInclinacionIMU(ruidoX, ruidoY, ruidoZ);

  // 2. Recibir y procesar comandos de forma inmediata
  if (rf95.available()) {
    uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);

    if (rf95.recv(buf, &len)) {
      String mensaje = "";
      for (uint8_t i = 0; i < len; i++) {
        mensaje += (char)buf[i];
      }
      procesarComando(mensaje);
    }
  }

  // 3. Enviar telemetría periódica
  if (millis() - ultimoEnvioSensores >= intervaloTelemetria) {
    ultimoEnvioSensores = millis();
    enviarTelemetria();
  }

  delay(5);
}