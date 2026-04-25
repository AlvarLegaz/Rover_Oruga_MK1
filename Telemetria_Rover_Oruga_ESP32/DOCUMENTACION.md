# Arquitectura e hilos del proyecto ESP32 Rover MK1

## Resumen general

Proyecto de rover basado en **ESP32-CAM AI Thinker** con cámara OV2640, GPS, sensores inerciales y servidor web embebido.

El sistema está diseñado para operar de forma autónoma en red local, ofreciendo vídeo en tiempo real, telemetría JSON y configuración remota vía navegador.

## Funciones principales

- Captura de imagen JPEG.
- Streaming MJPEG en tiempo real.
- Posicionamiento GPS.
- Sensores inerciales MPU9250:
  - acelerómetro
  - giroscopio
  - magnetómetro
- Sensor ambiental BMP280:
  - temperatura
  - presión
  - altitud estimada
- API REST JSON modular.
- Panel web integrado.
- Configuración WiFi persistente en memoria Flash.
- Arquitectura multitarea con FreeRTOS.

---

# Hardware principal

## Controladora

- ESP32-CAM AI Thinker

## Sensores

- Cámara OV2640
- GPS UART
- MPU9250 (IMU 9 ejes)
- BMP280 (barómetro)

## Pines utilizados

### GPS

| Función | GPIO |
|--------|------|
| RX/TX | GPIO13 / GPIO12 |

### I2C Sensores

| Función | GPIO |
|--------|------|
| SDA | GPIO15 |
| SCL | GPIO14 |

---

# Archivos principales

## `Telemetria_Rover_Oruga_ESP32.ino`

Archivo principal del proyecto.

Responsable de:

- arranque del sistema
- WiFi AP / Station
- inicialización hardware
- watchdog
- arranque servicios

---

## `web_server_rover.cpp / .h`

Servidor HTTP principal.

Responsable de:

- rutas web
- API JSON
- páginas HTML
- tareas dinámicas de streaming

---

## `camera_driver_OV2640.cpp / .h`

Driver de cámara.

Responsable de:

- inicialización OV2640
- captura frames
- cambio de modos:
  - LOW LATENCY
  - NORMAL
- control resolución y calidad JPEG

---

## `gps.cpp / gps.h`

Driver GPS.

Responsable de:

- lectura UART2
- parseo NMEA
- mantenimiento estructura `GPSData`

---

## `imu.cpp / imu.h`

Driver sensores inerciales.

Responsable de:

- inicialización I2C
- lectura MPU9250
- lectura BMP280
- cálculo:

  - roll
  - pitch
  - yaw
  - heading
  - altitud

---

## `Telemetry.cpp / .h`

Capa unificada de telemetría.

Responsable de:

- recolectar sensores
- sincronizar acceso a datos
- generar JSON modular

---

# Arquitectura multitarea FreeRTOS

---

## 1. loopTask (Core 1)

Tarea principal Arduino.

Responsable de:

- `server.handleClient()`
- mantenimiento general
- watchdog principal

Prioridad: baja

---

## 2. gpsTask (Core 1)

Lectura continua GPS.

Responsable de:

- vaciado UART
- parseo continuo

Prioridad: media

---

## 3. telemetryTask (Core 1)

Actualización periódica sensores.

Responsable de:

- temperatura
- batería
- GPS snapshot
- IMU update
- BMP280 update

Frecuencia típica:

- 500 ms

---

## 4. streamTask (Core 0)

Streaming MJPEG alta calidad.

Responsable de:

- captura cámara
- envío frames HTTP

Temporal: sí

---

## 5. streamLowTask (Core 0)

Streaming MJPEG baja latencia.

Responsable de:

- vídeo tiempo real
- máximos FPS posibles

Temporal: sí

---

# Gestión de concurrencia

## Mutex usados

### `gpsMutex`

Protege estructura GPS.

### `camMutex`

Acceso exclusivo a cámara.

Evita conflictos entre:

- `/capture`
- `/stream`
- `/stream_low`

### `mutex` interno Telemetry

Protege snapshot de sensores.

---

# Watchdog (WDT)

Sistema protegido con watchdog para detectar bloqueos críticos.

## Nota importante

Las tareas de streaming no usan ya `esp_task_wdt_reset()` innecesario.

Esto evita errores:

```text
task_wdt: task not found