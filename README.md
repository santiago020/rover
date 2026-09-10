# 🚀 ROVER // Estación de Control y Telemetría Web

Estación de control y monitoreo en tiempo real para rover explorador. **100% gratuita**, sin necesidad de servidores, sin tarjetas de crédito y lista para publicar en **GitHub Pages** en 2 minutos.

---

## 🌟 Características

- **Control del Rover**:
  - D-Pad táctil para móvil con captura de puntero (`setPointerCapture`) y vibración háptica.
  - Soporte de teclado para escritorio (flechas del cursor y barra espaciadora para STOP).
  - Selector de modo (Automático / Manual) y campo para comandos personalizados.
  - Botón de parada de emergencia (**STOP**).
- **Telemetría de 24 Sensores en Vivo**:
  - Nivel y voltaje de batería LiPo con indicador de tendencia y alertas de batería baja/crítica.
  - 10 sensores de corriente (C1 a C10) con sparklines individuales y filtro de ruido.
  - 6 encoders absolutos (EA1 a EA6) para posición angular.
  - 4 encoders incrementales (EI1 a EI4) con estado visual digital.
  - Acelerómetro y giroscopio IMU (ejes X, Y, Z).
  - Indicador de calidad de señal LoRa (RSSI) y estados del sistema.
- **Gráficas en Tiempo Real**:
  - Gráficas de alta frecuencia con reducción inteligente de puntos (LTTB) para no ralentizar el navegador.
  - Selector de rangos de tiempo (30s, 1m, 5m, 15m).
  - **Exportar a CSV**: descarga en cualquier momento el historial capturado en la sesión para abrirlo en Excel, MATLAB o Python.
- **⚙️ Panel de Ajustes en Vivo**:
  - Cambia broker MQTT, credenciales o tópicos directamente desde la interfaz sin tocar el código.
  - Se guarda automáticamente en tu navegador (`localStorage`).
- **🧪 Modo Demo / Simulación**:
  - Botón en la barra superior que genera telemetría sintética realista con un solo clic. Permite probar y demostrar toda la interfaz, gráficas y controles sin encender el rover físico.

---

## 🌐 Cómo Publicar GRATIS en GitHub Pages (Paso a Paso)

No necesitas instalar Node.js ni configurar servidores.

### Paso 1: Crear el repositorio en GitHub
1. Entra a [github.com](https://github.com) e inicia sesión (o crea tu cuenta gratis).
2. Haz clic en el botón verde **New** (Nuevo repositorio).
3. Nómbralo `rover` (o el nombre que quieras), déjalo como **Public** y haz clic en **Create repository**.

### Paso 2: Subir los archivos
En la página del repositorio creado:
1. Haz clic en **uploading an existing file** (subir archivos existentes).
2. Arrastra a la ventana los archivos de esta carpeta:
   - `index.html` *(fundamental)*
   - `rover.html`
   - `README.md`
   - `.gitignore`
3. Haz clic en el botón verde **Commit changes** (Guardar cambios).

### Paso 3: Activar GitHub Pages
1. En tu repositorio, entra a la pestaña **Settings** (Configuración) arriba a la derecha.
2. En el menú lateral izquierdo, haz clic en **Pages**.
3. En la sección **Build and deployment** -> **Branch**:
   - Cambia `None` por `main`.
   - Deja la carpeta en `/ (root)`.
   - Haz clic en **Save** (Guardar).
4. Espera entre 30 y 60 segundos y refresca la página.
5. Verás un recuadro verde con tu enlace público:
   👉 `https://<tu-usuario>.github.io/rover/`

**¡Listo!** Ya puedes abrir ese enlace desde cualquier computadora, tablet o teléfono móvil en cualquier parte del mundo.

---

## ⚙️ Conexión con tu Broker MQTT (HiveMQ Cloud)

El dashboard viene preconfigurado con las credenciales de HiveMQ Cloud. Si necesitas modificarlas:
1. Abre tu enlace de GitHub Pages.
2. Haz clic en el botón **⚙️ Ajustes** en la barra superior.
3. Ingresa tu URL de Broker WebSocket (ej: `wss://xxxxx.s1.eu.hivemq.cloud:8884/mqtt`), usuario y contraseña.
4. Haz clic en **Guardar y Conectar**.

---

## 📡 Protocolo de Paquete (ESP32 / LoRa)

El paquete que el ESP32 envía por MQTT debe tener el siguiente formato delimitado por barras (`|`):
```text
BAT=12.45|C1=1.20|C2=0.85|EA1=90.0|EI1=1|IMUX=0.02|IMUY=-0.01|IMUZ=0.98|RSSI=-72|ESP32M=OK|ESTADO=DETENIDO|CRC=A4
```

### Checksum (CRC)
El CRC es opcional pero muy recomendado para evitar datos corruptos por ruido de radio:
```c
// Código en C/C++ para tu ESP32:
uint8_t crc = 0;
for (size_t i = 0; i < payload_len; i++) {
    crc = (crc + (uint8_t)payload[i]) % 256;
}
// Se envía al final del paquete como: |CRC=%02X
```

---

## 📁 Carpeta `worker_opcional/`

Dentro de `worker_opcional/` se encuentran los archivos para persistencia 24/7 en base de datos InfluxDB y servidor Fly.io. No es necesario para operar el rover ni para publicar en GitHub Pages. Se conserva como respaldo por si en el futuro deseas guardar telemetría continua en la nube sin tener el navegador abierto.
