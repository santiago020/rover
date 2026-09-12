# 🛰️ ROVER // Sistema Integral de Control, Telemetría y Navegación GPS

Sistema integral de telemetría de alta frecuencia, control manual/autónomo y navegación cartográfica para vehículo explorador terrestre (Rover). Diseñado para operar sobre enlace de radiofrecuencia de largo alcance (**LoRa**) con puente a la nube (**MQTT / WebSockets TLS**) y visualización en tiempo real en un **Dashboard Web 100% autónomo** alojado en **GitHub Pages**.

[![GitHub Pages](https://img.shields.io/badge/Live-GitHub%20Pages-00ff88?style=flat-square&logo=github)](https://santiago020.github.io/rover/)
[![MQTT HiveMQ](https://img.shields.io/badge/Broker-HiveMQ%20Cloud%20(TLS)-ffb020?style=flat-square&logo=mqtt)](https://www.hivemq.com/)
[![Firmware ESP32](https://img.shields.io/badge/Firmware-ESP32%20Arduino-3b82f6?style=flat-square&logo=espressif)](https://www.espressif.com/)
[![Radio LoRa](https://img.shields.io/badge/Radio-LoRa%20SX1276%2FRF95-a855f7?style=flat-square)](#)

---

## 📋 Tabla de Contenidos
1. [Arquitectura General del Sistema](#-arquitectura-general-del-sistema)
2. [Componentes del Hardware](#-componentes-del-hardware)
   - [ESP32 Esclavo (Rover Autónomo)](#1-esp32-esclavo-cerebro-del-rover)
   - [ESP32 Maestro (Gateway de Campo)](#2-esp32-maestro-gateway-lora---wifi--mqtt)
3. [Dashboard Web de Control (Estación de Operaciones)](#-dashboard-web-de-control)
   - [Control Manual y Parada de Emergencia](#control-manual-y-parada-de-emergencia)
   - [Mapa GPS Interactivo y Navegación](#mapa-gps-interactivo-y-navegación)
   - [Gestión de Punto Home y Retorno Autónomo (RTL)](#gestión-de-punto-home-y-retorno-autónomo-rtl)
   - [Parada de Emergencia Anti-Vuelco](#parada-de-emergencia-anti-vuelco-autónoma)
   - [Telemetría, Encoders y Corrientes](#telemetría-encoders-y-corrientes)
   - [Horizonte Artificial 3D](#horizonte-artificial-3d-e-inclinómetro)
   - [Registro Técnico en Modal Emergente (Log)](#registro-técnico-en-modal-emergente-log)
4. [Protocolo de Comunicación](#-protocolo-de-comunicación)
   - [Formato de Telemetría (Sensores)](#formato-de-telemetría-sensores)
   - [Formato de Comandos y ACK](#formato-de-comandos-y-ack)
   - [Cálculo del Checksum CRC](#cálculo-del-checksum-crc)
5. [Guía de Despliegue en GitHub Pages](#-guía-de-despliegue-en-github-pages)
6. [Historial de Versiones y Actualizaciones (Changelog)](#-historial-de-versiones-y-actualizaciones)

---

## 🏛️ Arquitectura General del Sistema

El sistema utiliza una arquitectura desacoplada en tres niveles:

```
┌────────────────────────────────────────────────────────┐
│                   ROVER (EN CAMPO)                     │
│                                                        │
│  [ Motores / Encoders / Sensores C1-C10 / IMU MPU ]    │
│                           │                            │
│                           ▼                            │
│           ┌────────────────────────────────┐           │
│           │   ESP32 ESCLAVO (FIRMWARE)     │           │
│           │ • Control PWM y lectura ADC    │           │
│           │ • Corte Anti-Vuelco (>35°/300ms)│          │
│           │ • Fuente de verdad AUTO/MANUAL │           │
│           │ • Almacenamiento Punto Home    │           │
│           └───────────────┬────────────────┘           │
│                           │ Radio LoRa (SX1276 / RF95) │
└───────────────────────────┼────────────────────────────┘
                            │ (433 / 868 / 915 MHz)
                            ▼
┌────────────────────────────────────────────────────────┐
│             ESTACIÓN BASE / GATEWAY DE CAMPO           │
│                                                        │
│           ┌────────────────────────────────┐           │
│           │   ESP32 MAESTRO (GATEWAY)      │           │
│           │ • Radio LoRa con hardware serial│          │
│           │ • Módulo Wi-Fi + TLS seguro    │           │
│           │ • Inyección de RSSI de radio   │           │
│           └───────────────┬────────────────┘           │
└───────────────────────────┼────────────────────────────┘
                            │ Internet / Wi-Fi (TLS 8883)
                            ▼
┌────────────────────────────────────────────────────────┐
│                 BROKER MQTT EN LA NUBE                 │
│                 (HiveMQ Cloud Cluster)                 │
│                                                        │
│   Topics:                                              │
│   • rover/sensores (Telemetría periódica hacia web)    │
│   • rover/comando  (Órdenes de operador QoS 1)         │
│   • rover/ack      (Confirmación de ejecución de orden)│
└───────────────────────────┬────────────────────────────┘
                            │ WebSockets Seguros (WSS 8884)
                            ▼
┌────────────────────────────────────────────────────────┐
│               DASHBOARD WEB (OPERADOR)                 │
│         https://santiago020.github.io/rover/           │
│                                                        │
│   • Mapa Leaflet GPS con waypoints interactivos        │
│   • Controles de movimiento táctil + teclado           │
│   • Horizonte artificial 3D + Alerta de Vuelco         │
│   • Gráficas en tiempo real y descarga CSV             │
│   • Modal técnico de eventos 📋 Log                    │
└────────────────────────────────────────────────────────┘
```

---

## 🔌 Componentes del Hardware

### 1. ESP32 Esclavo (Cerebro del Rover)
*Código fuente:* `esclavo/esclavo.ino`

* **Función:** Control de bajo nivel en tiempo real, recolección de sensores, seguridad de chasis y ejecución de trayectorias.
* **Pines y Conexiones:**
  * **LoRa Serial2:** `GPIO16` (RX), `GPIO17` (TX) conectado al módulo Grove LoRa / RF95.
  * **Baudrate:** 9600 baudios en radio, 115200 en depuración serial.
* **Seguridad Anti-Vuelco de Hardware:**
  * Calcula continuamente la inclinación tridimensional combinando Pitch y Roll del acelerómetro:
    $$\text{inclinacionTotal} = \sqrt{\text{pitch}^2 + \text{roll}^2}$$
  * Si la inclinación supera **$35^\circ$ por más de $300\text{ ms}$ continuos**, apaga de inmediato todas las salidas de motores (`detenerMotores()`), conmuta al estado `EMERGENCIA_VUELCO` y bloquea cualquier comando de movimiento hasta que el chasis recupere la horizontalidad ($<25^\circ$).
* **Fuente de Verdad de Modo de Operación:**
  * El estado `AUTO` o `MANUAL` vive en el microcontrolador. Cualquier navegador que se conecte sincroniza su interfaz al estado reportado por el ESP32, garantizando coherencia en múltiples clientes simultáneos.
* **Punto Home en Memoria:**
  * Almacena las coordenadas base fijadas por el operador (`HLAT`, `HLON`) para la ejecución autónoma de retorno (`RTL`).

### 2. ESP32 Maestro (Gateway LoRa <-> WiFi / MQTT)
*Código fuente:* `maestro/maestro.ino`

* **Función:** Puente de comunicaciones transparente entre el campo de operaciones (LoRa) e Internet (HiveMQ Cloud).
* **Pines y Conexiones:**
  * **LoRa Serial2:** `GPIO16` (RX), `GPIO17` (TX).
* **Enrutamiento:**
  * Escucha en MQTT `rover/comando` y retransmite inmediatamente por radio LoRa hacia el rover.
  * Escucha en LoRa:
    * Si recibe un paquete de sensores: inyecta el `RSSI` de intensidad de señal y lo publica en `rover/sensores`.
    * Si recibe un paquete de confirmación (`ID=...|OK`): lo publica inmediatamente en `rover/ack`.

---

## 💻 Dashboard Web de Control

La interfaz web está construida en HTML5 puro, CSS moderno y JavaScript modular, optimizada tanto para computadoras de escritorio como para teléfonos móviles y tablets.

### Control Manual y Parada de Emergencia
* **D-Pad de Precisión:** Botones con soporte de puntero multitáctil (`setPointerCapture`) y vibración háptica al presionar en pantallas móviles.
* **Atajos de Teclado:**
  * `↑` Flecha Arriba: Avanzar (`ADELANTE`).
  * `↓` Flecha Abajo: Retroceder (`ATRAS`).
  * `←` Flecha Izquierda: Girar izquierda (`IZQUIERDA`).
  * `→` Flecha Derecha: Girar derecha (`DERECHA`).
  * `Espacio`: Parada de Emergencia instantánea (`STOP`).
* **Barra Flotante Móvil:** Controles fijados al pie de la pantalla en dispositivos móviles para una conducción ergonómica.

### Mapa GPS Interactivo y Navegación
* **Motor Cartográfico:** Leaflet con mosaicos oscuros de alta resolución *CartoDB Dark Matter*.
* **Diferenciación Visual de Marcadores:**
  * 🤖 **Rover:** Marcador circular verde neón (`#00ff88`) con pulso de radar animado en tiempo real.
  * 🏠 **Punto Home (Base):** Marcador circular cian (`#00f0ff`) con ícono `🏠`.
  * 🎯 **Objetivo (Waypoint):** Marcador circular ámbar (`#ffb020`) con ícono `🎯`.
* **Fijar Destino con un Clic:** El operador puede tocar o hacer clic directamente sobre cualquier punto del mapa. El dashboard actualiza automáticamente los campos de Latitud/Longitud y, si el rover está en modo automático, envía el comando `NAV_AUTO`.
* **Ruta Recorrida (Breadcrumbs):** Línea discontinua verde neón que va trazando la trayectoria histórica del vehículo.
* **Centrado Rápido:** Botones `🤖 Rover` y `🏠 Home` para saltar la cámara al vehículo o a la base al instante.

### Gestión de Punto Home y Retorno Autónomo (RTL)
* **`🏠 Fijar Home (Posición Rover)`**: Captura el GPS actual reportado por el rover y lo establece como base.
* **`📌 Fijar Home desde Campos`**: Permite escribir manualmente coordenadas de interés y fijarlas como Home.
* **Reescritura Libre**: El punto Home puede ser sobrescrito cuantas veces se desee durante la misión.
* **`🚀 Volver a Home (RTL)`**: Pasa el rover a modo `AUTO`, asigna el punto Home como objetivo y ordena el regreso autónomo.
* **Cálculo de Distancias:** Monitorea en tiempo real los metros o kilómetros restantes hasta el destino y hasta el punto Home usando la fórmula de Haversine.

### Parada de Emergencia Anti-Vuelco Autónoma
* Cuando el ESP32 detecta una inclinación $> 35^\circ$ por más de $300\text{ ms}$, corta motores y emite el paquete `VUELCO=1`.
* El dashboard despliega inmediatamente una alerta prioritaria en color rojo brillante con advertencia sonoro-visual:
  > 🚨 **CORTE DE EMERGENCIA EN ESP32: Motores apagados por riesgo de vuelco (>35° durante >300ms)**
* Los controles de avance quedan protegidos visualmente hasta que el vehículo regrese a un ángulo seguro.

### Telemetría, Encoders y Corrientes
* **Monitoreo de Batería:** Voltaje exacto, porcentaje calculado y análisis de tendencia de descarga.
* **10 Sensores de Corriente (`C1` a `C10`):** Con *sparklines* individuales para detectar sobreconsumos o trabas en motores.
* **6 Encoders Absolutos (`EA1` a `EA6`):** Monitoreo angular en grados ($0^\circ$ a $360^\circ$).
* **4 Encoders Incrementales (`EI1` a `EI4`):** Lecturas digitales de pulsos/pasos.

### Horizonte Artificial 3D e Inclinómetro
* Representación visual esférica inspirada en aviónica con indicación de horizonte, cabeceo (*pitch*), alabeo (*roll*) y vector de aceleración vertical en gravedades ($g$).

### Registro Técnico en Modal Emergente (`📋 Log`)
* Para no saturar el espacio vertical en pantalla, el registro de eventos técnicos se encuentra en un diálogo modal emergente accesible mediante el botón **`📋 Log`** en la barra superior.
* Permite auditar todos los paquetes MQTT recibidos, comandos enviados, ACKs y advertencias LoRa, con opción de vaciado (`Limpiar registro`).

---

## 📡 Protocolo de Comunicación

### Formato de Telemetría (Sensores)
El Rover emite paquetes de texto estructurados mediante delimitador barra vertical (`|`):
```text
BAT=12.45|C1=1.20|C2=0.85|...|C10=0.10|EA1=180.0|...|EA6=45.5|EI1=1|...|EI4=0|IMUX=0.02|IMUY=-0.01|IMUZ=0.98|LAT=4.609710|LON=-74.081750|HLAT=4.609710|HLON=-74.081750|ESTADO=DETENIDO|MODO=MANUAL|VUELCO=0|RSSI=-72|ESP32M=OK|ESP32S=OK|CRC=E3
```

| Campo | Descripción | Ejemplo |
| :--- | :--- | :--- |
| `BAT` | Voltaje de batería principal (V) | `12.45` |
| `C1` – `C10` | Corriente en amperes de cada circuito | `1.20` |
| `EA1` – `EA6` | Posición de encoders absolutos (°) | `180.0` |
| `EI1` – `EI4` | Estado binario de encoders incrementales | `1` o `0` |
| `IMUX`, `IMUY`, `IMUZ` | Aceleración en gravedades de los tres ejes | `0.02`, `-0.01`, `0.98` |
| `LAT`, `LON` | Coordenadas GPS actuales del Rover | `4.609710`, `-74.081750` |
| `HLAT`, `HLON` | Coordenadas del Punto Home almacenado | `4.609710`, `-74.081750` |
| `ESTADO` | Estado cinemático del vehículo | `DETENIDO`, `ADELANTE`, `EMERGENCIA_VUELCO` |
| `MODO` | Modo de conducción activo | `MANUAL` o `AUTO` |
| `VUELCO` | Bandera de disparo de seguridad de vuelco | `0` (Normal) o `1` (Disparado) |
| `RSSI` | Intensidad de la señal de radio LoRa (dBm) | `-72` |
| `CRC` | Checksum hexadecimal para validación de trama | `E3` |

### Formato de Comandos y ACK
* **Comando emitido por el Dashboard (`rover/comando`):**
  ```text
  CMD=ADELANTE|ID=k7a9x
  CMD=MODO=AUTO|ID=k7b2z
  CMD=NAV_AUTO|LAT=4.612500|LON=-74.085000|ID=k7c1a
  CMD=SET_HOME|LAT=4.609710|LON=-74.081750|ID=k7d3m
  CMD=RTL|LAT=4.609710|LON=-74.081750|ID=k7e8p
  ```
* **Respuesta de Confirmación del Rover (`rover/ack`):**
  * Confirmación exitosa:
    ```text
    ID=k7a9x|OK
    ```
  * Rechazo por seguridad (ej. vuelco activo):
    ```text
    ID=k7a9x|ERR=bloqueo_vuelco_critico
    ```

### Cálculo del Checksum CRC
Algoritmo de suma modular de 8 bits sobre los caracteres del paquete sin incluir el campo `|CRC=`:
```c
uint8_t calcularCRC(const char* payload, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = (crc + (uint8_t)payload[i]) % 256;
    }
    return crc;
}
```

---

## 🌐 Guía de Despliegue en GitHub Pages

1. **Subir los archivos a tu repositorio GitHub:**
   Asegúrate de que la rama `main` contenga:
   - `index.html` (Dashboard principal)
   - `rover.html` (Copia sincronizada)
   - `README.md` (Esta documentación)
2. **Habilitar Pages:**
   - En tu repositorio, entra en **Settings** ➔ **Pages**.
   - En **Build and deployment** / **Source**: selecciona `Deploy from a branch`.
   - En **Branch**: selecciona `main` y carpeta `/ (root)`. Guarda los cambios.
3. **Acceso Inmediato:**
   El dashboard estará disponible en:
   👉 `https://<tu-usuario>.github.io/rover/`

---

## 📝 Historial de Versiones y Actualizaciones

### [v2.4.0] - 2026-09-11
- **Eliminación de geolocalización de usuario:** Se retiró el marcador azul de estación base y el botón *"Mi ubicación GPS"*, manteniendo el mapa exclusivamente enfocado en el **Rover**, el punto **Home** y los **Waypoints**.
- **Documentación maestra integral:** Creación del manual técnico unificado, diagramas de arquitectura, pinouts de hardware, protocolo de paquetes y registro histórico de cambios.

### [v2.3.0] - 2026-09-10
- **Mapa GPS Interactivo con Leaflet:** Integración de mapa oscuro CartoDB con marcador verde neón con pulso de radar para el rover, soporte de clic interactivo para colocar waypoints y trazado de ruta (*breadcrumbs*).
- **Parada de Emergencia Anti-Vuelco en ESP32:** Implementación en `esclavo.ino` de corte inmediato de motores si la inclinación excede $35^\circ$ por más de $300\text{ ms}$, con notificación prioritaria en el dashboard.
- **Sistema de Punto Home y RTL Reescribible:** Capacidad de registrar y sobrescribir la posición de base en cualquier momento, con botón de retorno autónomo `🚀 Volver a Home (RTL)` y persistencia local.
- **Optimización de Interfaz:** Reorganización del layout priorizando Control, Mapa e IMU en la parte superior; reubicación del registro de eventos en el diálogo emergente `📋 Log`.

### [v2.2.0] - 2026-09-10
- **Fuente de verdad en ESP32:** El modo `AUTO` / `MANUAL` se traslada al microcontrolador para evitar inconsistencias entre navegadores.
- **D-Pad con Pointer Capture:** Soporte de pulsación sostenida y vibración háptica en dispositivos móviles.

### [v2.1.0] - 2026-09-10
- **Validación con Checksum CRC:** Incorporación de suma de comprobación modular de 8 bits en telemetría y comandos para rechazar paquetes corruptos por interferencia LoRa.
- **Sistema de Confirmación ACK con Timeout:** Indicador de estado de comandos con confirmación visual de recepción por parte del rover.

### [v2.0.0] - 2026-09-10
- **Dashboard Web Moderno:** Monitoreo de 24 sensores (Batería, C1-C10, EA1-EA6, EI1-EI4, IMU X/Y/Z, RSSI) con reducción inteligente LTTB y exportación a formato CSV.
