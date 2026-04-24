# Arquitectura e hilos del proyecto ESP32 Rover

## Resumen general

El proyecto implementa un rover basado en ESP32 con cámara, GPS y servidor web integrado. 

Funciones principales:
* Captura de imagen y streaming MJPEG.
* Lectura de posición GPS en tiempo real.
* Entrega de telemetría por HTTP en formato JSON.
* Interfaz web local para monitorización.
* Configuración WiFi persistente (NVS).

El sistema está diseñado bajo una arquitectura de **separación de responsabilidades** para maximizar la estabilidad en entornos RTOS.

---

## Archivos principales y relaciones

### `Telemetria_Rover_Oruga_ESP32.ino`
Punto de entrada y orquestador del sistema.
* **Responsable de:** Inicialización de hardware, configuración de Watchdog (WDT), gestión de conectividad (Modo AP/Station) y arranque de los servicios de telemetría y web.

### `web_server_rover.cpp / .h`
Núcleo de la interfaz de usuario y API.
* **Responsable de:** Registro de rutas HTTP, despacho de páginas HTML (`info_page.h`, `config_page.h`) y gestión de tareas dinámicas de video.

### `gps.cpp / gps.h`
Driver de posicionamiento.
* **Responsable de:** Manejo de la UART2, parseo de sentencias NMEA y mantenimiento de un snapshot seguro de la posición GPS protegido por Mutex.

### `Telemetry.cpp / .h`
Capa de abstracción de datos.
* **Responsable de:** Recolectar datos de múltiples fuentes (GPS, sensores analógicos, estado del sistema) y empaquetarlos en formato JSON para el frontend.

---

## Gestión de Hilos (FreeRTOS Tasks)



### 1. loopTask (Core 1)
Tarea estándar de Arduino.
* **Función:** Gestiona el `server.handleClient()` y el reseteo del Watchdog principal.
* **Prioridad:** 1 (Baja).

### 2. gpsTask (Core 1)
Hilo de alta frecuencia para el sensor de posición.
* **Función:** Lectura del buffer serie para evitar pérdida de datos UART.
* **Estado:** Permanente.
* **Prioridad:** Superior a la telemetría.

### 3. telemetryTask (Core 1)
Hilo de actualización de estado.
* **Función:** Actualiza los valores de sensores cada 500ms.
* **Estado:** Permanente.

### 4. streamTask / streamLowTask (Core 0)
Tareas dinámicas para el manejo de video.
* **Función:** Captura y envío de frames MJPEG. Se ejecutan en el **Core 0** para evitar que el procesamiento de imagen interfiera con la navegación o el GPS.
* **Estado:** Temporal (se destruyen al cerrar la conexión).

---

## Seguridad y Sincronización

Para garantizar que el sistema sea **estable** y no sufra "crashes" por acceso simultáneo a memoria, se implementan los siguientes mecanismos:

### Mutex (Semáforos)
* **`gpsMutex`**: Protege la estructura `GPSData` durante la escritura (desde `gpsTask`) y lectura (desde `Telemetry`).
* **`camMutex`**: Controla el acceso exclusivo al sensor de la cámara (OV2640). Evita conflictos si se solicita una captura mientras el stream está activo.
* **`mutex` (en Telemetry)**: Protege la generación del string JSON.

### Watchdog (WDT)
* Se utiliza un **Task Watchdog** configurado a 30 segundos para reiniciar el sistema automáticamente si el bucle principal se bloquea (ej. fallo crítico de red).

---

## Persistencia y Configuración
* **Almacenamiento:** Uso de la librería `Preferences` en la partición NVS de la Flash.
* **Lógica de Conexión:** 1. Si el **Pin 12** está en `LOW` al arrancar: Modo AP forzado.
    2. Si no hay credenciales guardadas: Modo AP automático.
    3. Si hay credenciales: Intenta conectar a la red local; si falla en 10 segundos, activa Modo AP de rescate.

---

## Endpoints de la API

| Ruta | Función | Retorno |
| :--- | :--- | :--- |
| `/` | Interfaz de control principal | HTML/Dashboard |
| `/telemetry` | Datos de sensores en tiempo real | JSON |
| `/stream` | Video en alta calidad (10 FPS) | MJPEG Stream |
| `/stream_low` | Video en baja calidad (20 FPS) | MJPEG Stream |
| `/capture` | Foto instantánea | JPEG |
| `/config` | Panel de configuración WiFi | HTML |