// web_server_rover.cpp
// ==================================================
// Includes
// ==================================================

#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "web_server_rover.h"
#include "camera_driver_OV2640.h"
#include "Telemetry.h"
#include "info_page.h"
#include "config_page.h"


// ==================================================
// Configuración y estado global
// ==================================================

enum CameraMode {
    CAM_LOW,
    CAM_HIGH
};

const int web_port       = 80;

const int targetFPS_high = 10;
const int targetFPS_low  = 8;

extern Preferences preferences;

WebServer server(web_port);
Telemetry telemetry;

SemaphoreHandle_t camMutex = nullptr;

volatile bool streamingActive     = false;
volatile bool streamingHighActive = false;
volatile CameraMode currentMode   = CAM_LOW;

// ==================================================
// Prototipos privados
// ==================================================

static void ensureCameraMode(CameraMode mode);

static void streamTask(void* param);
static void streamLowTask(void* param);

// ==================================================
// Arranque servidor web
// ==================================================

void initWebServerResources() {
    if (camMutex == nullptr) {
        camMutex = xSemaphoreCreateMutex();
    }
}

void setupWebServer() {

    server.on("/", HTTP_GET, handleInfo);
    server.on("/info", HTTP_GET, handleInfo);

    server.on("/telemetry", HTTP_GET, handleTelemetry);
    server.on("/system", HTTP_GET, handleSystem);
    server.on("/gps", HTTP_GET, handleGPS);
    server.on("/imu", HTTP_GET, handleIMU);

    server.on("/luces/on", HTTP_GET, handleLightsOn);
    server.on("/luces/off", HTTP_GET, handleLightsOff);

    server.on("/capture", HTTP_GET, handleCapture);
    server.on("/stream", HTTP_GET, handleStream);
    server.on("/stream_low", HTTP_GET, handleStreamLow);

    server.on("/config", HTTP_GET, handleConfig);
    server.on("/save", HTTP_POST, handleSave);

    server.begin();
}

// ==================================================
// Endpoints generales
// ==================================================

void handleInfo() {
    server.send_P(200, "text/html", INFO_PAGE);
}

// ==================================================
// API JSON
// ==================================================

void handleTelemetry() 
{
    char json[420];
    telemetry.toJSON(json, sizeof(json));
    server.send(200, "application/json", json);
}

void handleSystem()
{
    char json[128];
    telemetry.toJSONSystem(json,sizeof(json));
    server.send(200,"application/json",json);
}

void handleGPS()
{
    char json[256];
    telemetry.toJSONGPS(json,sizeof(json));
    server.send(200,"application/json",json);
}

void handleIMU()
{
    char json[256];
    telemetry.toJSONIMU(json,sizeof(json));
    server.send(200,"application/json",json);
}

// ==================================================
// CONTROL LUCES
// ==================================================

void handleLightsOn() {
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);
    server.send(200, "text/plain", "Luces ON");
}

void handleLightsOff() {
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
    server.send(200, "text/plain", "Luces OFF");
}

// ==================================================
// Captura simple
// ==================================================

void handleCapture() {
    // Evita capturar mientras el stream está usando la cámara
    if (streamingActive) {
        server.send(503, "text/plain", "Camara ocupada: stream activo");
        return;
    }

    // Protege el acceso a esp_camera_fb_get()
    if (!xSemaphoreTake(camMutex, pdMS_TO_TICKS(1000))) {
        server.send(503, "text/plain", "Camara ocupada");
        return;
    }

    camera_fb_t* fb = getCameraFrame();

    if (!fb) {
        xSemaphoreGive(camMutex);
        server.send(500, "text/plain", "Error capturando imagen");
        return;
    }

    server.sendHeader("Content-Type", "image/jpeg");
    server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);

    releaseCameraFrame(fb);
    xSemaphoreGive(camMutex);
}

// ==================================================
// Stream HIGH
// ==================================================

void handleStream() {

    if (!cameraSupported) {
        server.send(503, "text/plain", "Camara no disponible");
        return;
    }

    if (camMutex == nullptr) {
        server.send(500, "text/plain", "Mutex camara no inicializado");
        return;
    }

    if (streamingActive) {
        server.send(503, "text/plain", "Stream ocupado");
        return;
    }

    WiFiClient client = server.client();

    if (!client || !client.connected()) {
        server.send(400, "text/plain", "Cliente no conectado");
        return;
    }

    streamingActive = true;

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println("Cache-Control: no-cache, no-store, must-revalidate");
    client.println("Pragma: no-cache");
    client.println("Connection: close");
    client.println();

    WiFiClient* clientCopy = new WiFiClient(client);

    if (clientCopy == nullptr) {
        streamingActive = false;
        client.stop();
        return;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        streamTask,
        "streamTask",
        8192,
        clientCopy,
        1,
        nullptr,
        1
    );

    if (ok != pdPASS) {
        delete clientCopy;
        streamingActive = false;
        client.stop();
        return;
    }
}

static void streamTask(void* param) {

    WiFiClient client = *((WiFiClient*)param);
    delete (WiFiClient*)param;

    streamingHighActive = true;

    const int frameInterval = 1000 / targetFPS_high;

    const unsigned long noWriteTimeoutMs = 5000;
    const unsigned long maxStreamTimeMs  = 10UL * 60UL * 1000UL;

    unsigned long streamStartTime = millis();
    unsigned long lastFrameTime   = 0;
    unsigned long lastGoodWrite   = millis();

    // Cambia a modo HIGH protegido por mutex
    if (xSemaphoreTake(camMutex, pdMS_TO_TICKS(1000))) {
        ensureCameraMode(CAM_HIGH);
        xSemaphoreGive(camMutex);
    } else {
        goto cleanup;
    }

    while (client.connected()) {

        unsigned long now = millis();

        // Si el stream dura demasiado, se corta para evitar estados zombies
        if (now - streamStartTime > maxStreamTimeMs) {
            break;
        }

        // Si no se consigue escribir nada útil durante varios segundos, salir
        if (now - lastGoodWrite > noWriteTimeoutMs) {
            break;
        }

        // Limitador FPS
        if (now - lastFrameTime < (unsigned long)frameInterval) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        lastFrameTime = now;

        camera_fb_t* fb = nullptr;

        if (!xSemaphoreTake(camMutex, pdMS_TO_TICKS(250))) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        fb = getCameraFrame();

        if (!fb) {
            xSemaphoreGive(camMutex);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        bool ok = true;

        ok = ok && client.connected();
        ok = ok && client.print("--frame\r\n");
        ok = ok && client.print("Content-Type: image/jpeg\r\n");
        ok = ok && client.print("Content-Length: ");
        ok = ok && client.print(fb->len);
        ok = ok && client.print("\r\n\r\n");

        size_t written = 0;

        if (ok && client.connected()) {
            written = client.write(fb->buf, fb->len);
        }

        ok = ok && (written == fb->len);

        if (ok && client.connected()) {
            ok = ok && client.print("\r\n");
        }

        releaseCameraFrame(fb);
        xSemaphoreGive(camMutex);

        if (!ok) {
            break;
        }

        lastGoodWrite = millis();

        vTaskDelay(pdMS_TO_TICKS(2));
    }

cleanup:

    client.stop();

    // Limpieza garantizada del estado de cámara/stream
    if (camMutex != nullptr && xSemaphoreTake(camMutex, pdMS_TO_TICKS(1000))) {
        streamingHighActive = false;
        ensureCameraMode(CAM_LOW);
        xSemaphoreGive(camMutex);
    } else {
        streamingHighActive = false;
    }

    streamingActive = false;

    vTaskDelete(nullptr);
}

// ==================================================
// Stream LOW
// ==================================================

void handleStreamLow() {

    if (!cameraSupported) {
        server.send(503, "text/plain", "Camara no disponible");
        return;
    }

    if (camMutex == nullptr) {
        server.send(500, "text/plain", "Mutex camara no inicializado");
        return;
    }

    if (streamingActive) {
        server.send(503, "text/plain", "Stream ocupado");
        return;
    }

    WiFiClient client = server.client();

    if (!client || !client.connected()) {
        server.send(400, "text/plain", "Cliente no conectado");
        return;
    }

    streamingActive = true;

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println("Cache-Control: no-cache, no-store, must-revalidate");
    client.println("Pragma: no-cache");
    client.println("Connection: close");
    client.println();

    WiFiClient* clientCopy = new WiFiClient(client);

    if (clientCopy == nullptr) {
        streamingActive = false;
        client.stop();
        return;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        streamLowTask,
        "streamLowTask",
        8192,
        clientCopy,
        1,
        nullptr,
        1
    );

    if (ok != pdPASS) {
        delete clientCopy;
        streamingActive = false;
        client.stop();
        return;
    }
}

static void streamLowTask(void* param) {

    WiFiClient client = *((WiFiClient*)param);
    delete (WiFiClient*)param;

    const int frameInterval = 1000 / targetFPS_low;

    const unsigned long noWriteTimeoutMs = 5000;
    const unsigned long maxStreamTimeMs  = 10UL * 60UL * 1000UL;

    unsigned long streamStartTime = millis();
    unsigned long lastFrameTime   = 0;
    unsigned long lastGoodWrite   = millis();

    // Asegura modo LOW protegido por mutex
    if (xSemaphoreTake(camMutex, pdMS_TO_TICKS(1000))) {
        ensureCameraMode(CAM_LOW);
        xSemaphoreGive(camMutex);
    } else {
        goto cleanup;
    }

    while (client.connected()) {

        unsigned long now = millis();

        if (now - streamStartTime > maxStreamTimeMs) {
            break;
        }

        if (now - lastGoodWrite > noWriteTimeoutMs) {
            break;
        }

        if (now - lastFrameTime < (unsigned long)frameInterval) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        lastFrameTime = now;

        camera_fb_t* fb = nullptr;

        if (!xSemaphoreTake(camMutex, pdMS_TO_TICKS(250))) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        fb = getCameraFrame();

        if (!fb) {
            xSemaphoreGive(camMutex);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        bool ok = true;

        ok = ok && client.connected();
        ok = ok && client.print("--frame\r\n");
        ok = ok && client.print("Content-Type: image/jpeg\r\n");
        ok = ok && client.print("Content-Length: ");
        ok = ok && client.print(fb->len);
        ok = ok && client.print("\r\n\r\n");

        size_t written = 0;

        if (ok && client.connected()) {
            written = client.write(fb->buf, fb->len);
        }

        ok = ok && (written == fb->len);

        if (ok && client.connected()) {
            ok = ok && client.print("\r\n");
        }

        releaseCameraFrame(fb);
        xSemaphoreGive(camMutex);

        if (!ok) {
            break;
        }

        lastGoodWrite = millis();

        vTaskDelay(pdMS_TO_TICKS(2));
    }

cleanup:

    client.stop();

    streamingActive = false;

    vTaskDelete(nullptr);
}

// ==================================================
// Config WiFi
// ==================================================

void handleConfig() {

    int n = WiFi.scanNetworks();

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    server.sendContent(CONFIG_HEADER);

    if (n == 0) {

        server.sendContent("<p>No se encontraron redes</p>");

    } else {

        char line[256];

        for (int i = 0; i < n; ++i) {

            String ssid = WiFi.SSID(i);
            int rssi = WiFi.RSSI(i);
            bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

            snprintf(line, sizeof(line),
                "<li onclick=\"document.getElementsByName('ssid')[0].value='%s'\">"
                "%s (%d dBm) %s"
                "</li>",
                ssid.c_str(),
                ssid.c_str(),
                rssi,
                open ? "abierta" : "cerrada"
            );

            server.sendContent(line);
        }
    }

    server.sendContent(CONFIG_FOOTER);

    WiFi.scanDelete();
}

void handleSave() {

    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.end();

    server.send(200, "text/plain", "Guardado. Reiniciando...");
    delay(1000);
    ESP.restart();
}

// ==================================================
// Helpers internos
// ==================================================

static void ensureCameraMode(CameraMode mode) {

    if (currentMode == mode) {
        return;
    }

    if (mode == CAM_HIGH) {
        setCameraNormalMode();
    } else {
        setCameraLowStreamMode();
    }

    currentMode = mode;
}


