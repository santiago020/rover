#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <RH_RF95.h>

/* ============================================================
   GATEWAY BASE ROVER (ESP32: LoRa <-> WiFi/HiveMQ)
   ------------------------------------------------------------
   Responsabilidades:
   1. Conectarse a la red WiFi y al broker MQTT (HiveMQ Cloud con TLS).
   2. Suscribirse a rover/comando (QoS 1) y retransmitir cada comando
      hacia el Rover mediante LoRa.
   3. Recibir paquetes LoRa del Rover:
      - Si es un ACK (ID=...|OK), publicarlo en rover/ack.
      - Si es telemetría (sensores), inyectar RSSI LoRa y publicarlo
        en rover/sensores.
============================================================ */

// ============================================================
//                     CONFIGURACIÓN WIFI
// ============================================================
const char* WIFI_SSID = "aaaa";             // Cambia por el nombre de tu red WiFi
const char* WIFI_PASSWORD = "12345678";     // Cambia por la contraseña de tu WiFi

// ============================================================
//                     CONFIGURACIÓN HIVEMQ
// ============================================================
const char* MQTT_SERVER = "f9980291ea454f999cbfb77ee79a623b.s1.eu.hivemq.cloud";
const int   MQTT_PORT   = 8883;
const char* MQTT_USER   = "rover";
const char* MQTT_PASSWORD = "Rover2026!";

// Topics MQTT
const char* TOPIC_DATOS   = "rover/sensores";
const char* TOPIC_COMANDO = "rover/comando";
const char* TOPIC_ACK     = "rover/ack";

// ============================================================
//                     CONFIGURACIÓN LORA
// ============================================================
// Grove LoRa TX -> ESP32 GPIO16
// Grove LoRa RX -> ESP32 GPIO17
#define LORA_RX 16
#define LORA_TX 17

RH_RF95<HardwareSerial> rf95(Serial2);
WiFiClientSecure espClient;
PubSubClient mqtt(espClient);

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

// ============================================================
//                     CONEXIÓN WIFI
// ============================================================
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println();
  Serial.println("========================================");
  Serial.println("CONECTANDO A WIFI...");
  Serial.println("========================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long tInicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - tInicio < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWIFI CONECTADO");
    Serial.print("IP asignada: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFallo conectando WiFi. Reintentando luego...");
  }
}

// ============================================================
//                     CONEXIÓN MQTT
// ============================================================
void conectarMQTT() {
  while (!mqtt.connected() && WiFi.status() == WL_CONNECTED) {
    Serial.println("========================================");
    Serial.println("CONECTANDO A HIVEMQ CLOUD...");
    Serial.println("========================================");

    String clientID = "ROVER_GATEWAY_" + String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqtt.connect(clientID.c_str(), MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("MQTT CONECTADO EXITOSAMENTE");

      // Suscripción con QoS 1 para no perder comandos críticos de movimiento
      if (mqtt.subscribe(TOPIC_COMANDO, 1)) {
        Serial.print("Suscrito con QoS 1 a: ");
        Serial.println(TOPIC_COMANDO);
      } else {
        Serial.println("ERROR suscribiendo a comandos");
      }
    } else {
      Serial.print("ERROR MQTT. Código estado: ");
      Serial.println(mqtt.state());
      Serial.println("Reintentando en 3 segundos...");
      delay(3000);
    }
  }
}

// ============================================================
//                     CALLBACK MQTT (Comandos entrantes)
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String mensaje = "";
  for (unsigned int i = 0; i < length; i++) {
    mensaje += (char)payload[i];
  }

  Serial.println();
  Serial.println("========================================");
  Serial.println("COMANDO RECIBIDO DE LA WEB (MQTT)");
  Serial.print("Topic: "); Serial.println(topic);
  Serial.print("Mensaje: "); Serial.println(mensaje);
  Serial.println("========================================");

  if (String(topic) == TOPIC_COMANDO) {
    Serial.println("Retransmitiendo comando hacia el Rover por LoRa...");

    // Enviar comando por radio LoRa
    rf95.send((uint8_t*)mensaje.c_str(), mensaje.length());
    rf95.waitPacketSent();

    Serial.println("Comando enviado por LoRa. Escuchando ACK...");
    rf95.setModeRx();
  }
}

// ============================================================
//                     INICIAR LORA
// ============================================================
void iniciarLoRa() {
  Serial.println("INICIANDO GROVE LORA...");
  Serial2.setPins(LORA_RX, LORA_TX);
  Serial2.begin(57600, SERIAL_8N1, LORA_RX, LORA_TX);
  delay(200);

  if (!rf95.init()) {
    Serial.println("ERROR: No se pudo iniciar módulo LoRa");
    while (true) delay(1000);
  }

  if (!rf95.setFrequency(868.0)) {
    Serial.println("ERROR configurando frecuencia 868 MHz");
    while (true) delay(1000);
  }

  rf95.setTxPower(20, false);
  rf95.setModeRx();
  Serial.println("LoRa iniciado a 868 MHz en modo recepción");
}

// ============================================================
//                     REVISAR LORA (Mensajes del Rover)
// ============================================================
void revisarLoRa() {
  if (!rf95.available()) return;

  uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
  uint8_t len = sizeof(buf);

  if (rf95.recv(buf, &len)) {
    String mensaje = "";
    for (uint8_t i = 0; i < len; i++) {
      mensaje += (char)buf[i];
    }

    int rssiLoRa = rf95.lastRssi();

    Serial.println();
    Serial.println("========================================");
    Serial.println("MENSAJE LORA RECIBIDO DEL ROVER");
    Serial.print("Contenido: "); Serial.println(mensaje);
    Serial.print("RSSI LoRa: "); Serial.println(rssiLoRa);
    Serial.println("========================================");

    // 1. Si es confirmación ACK del Rover (ej: ID=xyz|OK o ID=xyz|ERR=...)
    if (mensaje.startsWith("ID=") || mensaje.indexOf("|ID=") != -1) {
      if (mqtt.connected()) {
        Serial.println("Publicando confirmación en rover/ack...");
        mqtt.publish(TOPIC_ACK, mensaje.c_str(), false);
      }
    }
    // 2. Si es paquete de telemetría de sensores
    else {
      // Inyectar RSSI medido por este Gateway si no viene en el paquete
      if (mensaje.indexOf("RSSI=") == -1) {
        int crcIdx = mensaje.indexOf("|CRC=");
        if (crcIdx != -1) {
          String sinCrc = mensaje.substring(0, crcIdx) + "|RSSI=" + String(rssiLoRa);
          uint8_t nuevoCrc = calcularCRC(sinCrc);
          char crcHex[5];
          sprintf(crcHex, "%02X", nuevoCrc);
          mensaje = sinCrc + "|CRC=" + String(crcHex);
        } else {
          mensaje += "|RSSI=" + String(rssiLoRa);
        }
      }

      if (mqtt.connected()) {
        Serial.println("Publicando telemetría en rover/sensores...");
        mqtt.publish(TOPIC_DATOS, mensaje.c_str());
      }
    }

    rf95.setModeRx();
  }
}

// ============================================================
//                     SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("########################################");
  Serial.println("#     GATEWAY BASE ROVER (LORA-MQTT)   #");
  Serial.println("########################################");

  conectarWiFi();

  // Conexión TLS segura a HiveMQ Cloud
  espClient.setInsecure();
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);

  iniciarLoRa();

  Serial.println("GATEWAY INICIALIZADO Y LISTO");
}

// ============================================================
//                     LOOP
// ============================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }

  if (!mqtt.connected()) {
    conectarMQTT();
  }

  mqtt.loop();
  revisarLoRa();
  delay(5);
}