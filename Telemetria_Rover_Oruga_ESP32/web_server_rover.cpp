// web_server_rover.cpp
// ==================================================
// Includes
// ==================================================

#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <WiFiUdp.h>

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

const uint16_t udpVideoLocalPort = 4210;
const uint16_t udpTelemetryLocalPort = 4211;
const size_t udpVideoPayloadSize = 1000;
const unsigned long udpVideoMaxStreamTimeMs = 10UL * 60UL * 1000UL;
const unsigned long udpClientTimeoutMs = 3000UL;
const unsigned long udpTelemetryIntervalMs = 200UL;

extern Preferences preferences;

WebServer server(web_port);
Telemetry telemetry;

SemaphoreHandle_t camMutex = nullptr;

volatile bool streamingActive     = false;
volatile bool streamingHighActive = false;
volatile bool udpStreamingActive  = false;
volatile bool udpStopRequested    = false;
volatile unsigned long lastUdpClientSeenMs = 0;
volatile CameraMode currentMode   = CAM_LOW;

IPAddress udpClientIp;
uint16_t udpClientPort = 0;
uint16_t udpClientTelemetryPort = 0;
CameraMode udpVideoMode = CAM_LOW;

WiFiUDP udpVideo;
WiFiUDP udpTelemetry;

struct UdpVideoConfig {
    IPAddress clientIp;
    uint16_t clientPort;
    uint16_t telemetryPort;
    CameraMode mode;
};

struct __attribute__((packed)) UdpVideoPacketHeader {
    uint16_t magic;
    uint16_t frameId;
    uint16_t packetIndex;
    uint16_t packetCount;
    uint16_t payloadSize;
};

// ==================================================
// Prototipos privados
// ==================================================

static void ensureCameraMode(CameraMode mode);

static void streamTask(void* param);
static void streamLowTask(void* param);
static void udpStreamTask(void* param);
static bool sendUdpFrame(camera_fb_t* fb, uint16_t frameId, IPAddress clientIp, uint16_t clientPort);
static bool sendUdpTelemetry(IPAddress clientIp, uint16_t telemetryPort);

void handleUdpStart();
void handleUdpStop();
void handleUdpStatus();
void handleUdpPing();

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

    server.on("/udp/start", HTTP_GET, handleUdpStart);
    server.on("/udp/stop", HTTP_GET, handleUdpStop);
    server.on("/udp/status", HTTP_GET, handleUdpStatus);
    server.on("/udp/ping", HTTP_GET, handleUdpPing);

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
// Stream UDP
// ==================================================

void handleUdpStart() {

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

    if (!server.hasArg("port")) {
        server.send(400, "text/plain", "Falta parametro port. Ejemplo: /udp/start?port=5000&mode=low&telemetry_port=5001");
        return;
    }

    long port = server.arg("port").toInt();

    if (port <= 0 || port > 65535) {
        server.send(400, "text/plain", "Puerto UDP de video no valido");
        return;
    }

    long telemetryPort = 0;

    if (server.hasArg("telemetry_port")) {
        telemetryPort = server.arg("telemetry_port").toInt();

        if (telemetryPort <= 0 || telemetryPort > 65535) {
            server.send(400, "text/plain", "Puerto UDP de telemetria no valido");
            return;
        }
    }

    CameraMode mode = CAM_LOW;

    if (server.hasArg("mode")) {
        String modeArg = server.arg("mode");
        modeArg.toLowerCase();

        if (modeArg == "high") {
            mode = CAM_HIGH;
        } else if (modeArg == "low") {
            mode = CAM_LOW;
        } else {
            server.send(400, "text/plain", "Modo no valido. Usa mode=low o mode=high");
            return;
        }
    }

    UdpVideoConfig* config = new UdpVideoConfig;

    if (config == nullptr) {
        server.send(500, "text/plain", "Sin memoria para iniciar UDP");
        return;
    }

    config->clientIp = server.client().remoteIP();
    config->clientPort = (uint16_t)port;
    config->telemetryPort = (uint16_t)telemetryPort;
    config->mode = mode;

    IPAddress responseIp = config->clientIp;
    uint16_t responsePort = config->clientPort;
    uint16_t responseTelemetryPort = config->telemetryPort;

    udpClientIp = config->clientIp;
    udpClientPort = config->clientPort;
    udpClientTelemetryPort = config->telemetryPort;
    udpVideoMode = mode;
    lastUdpClientSeenMs = millis();

    streamingActive = true;
    udpStreamingActive = true;
    udpStopRequested = false;

    BaseType_t ok = xTaskCreatePinnedToCore(
        udpStreamTask,
        "udpStreamTask",
        8192,
        config,
        0,
        nullptr,
        1
    );

    if (ok != pdPASS) {
        delete config;
        udpStreamingActive = false;
        udpStopRequested = false;
        streamingActive = false;
        lastUdpClientSeenMs = 0;
        udpClientPort = 0;
        udpClientTelemetryPort = 0;
        server.send(500, "text/plain", "No se pudo crear tarea UDP");
        return;
    }

    char response[220];
    snprintf(response, sizeof(response),
        "UDP stream iniciado hacia %u.%u.%u.%u:%u en modo %s. Telemetria UDP: %s%u",
        responseIp[0], responseIp[1], responseIp[2], responseIp[3],
        responsePort,
        mode == CAM_HIGH ? "high" : "low",
        responseTelemetryPort > 0 ? "puerto " : "off ",
        responseTelemetryPort
    );

    server.send(200, "text/plain", response);
}

void handleUdpStop() {

    if (!udpStreamingActive) {
        server.send(200, "text/plain", "UDP stream no estaba activo");
        return;
    }

    udpStopRequested = true;
    server.send(200, "text/plain", "Parando UDP stream");
}

void handleUdpStatus() {

    unsigned long ageMs = 0;

    if (lastUdpClientSeenMs > 0) {
        ageMs = millis() - lastUdpClientSeenMs;
    }

    char json[320];
    snprintf(json, sizeof(json),
        "{\"udp_active\":%s,\"streaming_active\":%s,\"client_ip\":\"%u.%u.%u.%u\",\"client_port\":%u,\"mode\":\"%s\",\"telemetry_udp_active\":%s,\"telemetry_port\":%u,\"telemetry_interval_ms\":%lu,\"last_ping_age_ms\":%lu,\"timeout_ms\":%lu}",
        udpStreamingActive ? "true" : "false",
        streamingActive ? "true" : "false",
        udpClientIp[0], udpClientIp[1], udpClientIp[2], udpClientIp[3],
        udpClientPort,
        udpVideoMode == CAM_HIGH ? "high" : "low",
        udpClientTelemetryPort > 0 ? "true" : "false",
        udpClientTelemetryPort,
        udpTelemetryIntervalMs,
        ageMs,
        udpClientTimeoutMs
    );

    server.sendHeader("Connection", "close");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
}

void handleUdpPing() {

    IPAddress requesterIp = server.client().remoteIP();

    if (!udpStreamingActive) {
        server.sendHeader("Connection", "close");
        server.send(409, "text/plain", "UDP stream no activo");
        return;
    }

    if (requesterIp != udpClientIp) {
        server.sendHeader("Connection", "close");
        server.send(403, "text/plain", "Cliente UDP no autorizado");
        return;
    }

    lastUdpClientSeenMs = millis();

    server.sendHeader("Connection", "close");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/plain", "OK");
}

static void udpStreamTask(void* param) {

    UdpVideoConfig config = *((UdpVideoConfig*)param);
    delete (UdpVideoConfig*)param;

    const int targetFPS = config.mode == CAM_HIGH ? targetFPS_high : targetFPS_low;
    const uint32_t frameInterval = 1000 / targetFPS;

    unsigned long streamStartTime    = millis();
    unsigned long lastFrameTime      = 0;
    unsigned long lastTelemetryTime  = 0;
    uint16_t frameId                 = 0;

    udpVideo.begin(udpVideoLocalPort);

    if (config.telemetryPort > 0) {
        udpTelemetry.begin(udpTelemetryLocalPort);
    }

    if (xSemaphoreTake(camMutex, pdMS_TO_TICKS(1000))) {
        ensureCameraMode(config.mode);
        xSemaphoreGive(camMutex);
    } else {
        goto cleanup;
    }

    while (!udpStopRequested) {

        unsigned long now = millis();

        if (now - streamStartTime > udpVideoMaxStreamTimeMs) {
            break;
        }

        if (lastUdpClientSeenMs == 0 || now - lastUdpClientSeenMs > udpClientTimeoutMs) {
            break;
        }

        if (config.telemetryPort > 0 && now - lastTelemetryTime >= udpTelemetryIntervalMs) {
            lastTelemetryTime = now;
            sendUdpTelemetry(config.clientIp, config.telemetryPort);
        }

        if (now - lastFrameTime < frameInterval) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        lastFrameTime = now;

        camera_fb_t* fb = nullptr;

        if (!xSemaphoreTake(camMutex, pdMS_TO_TICKS(100))) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        fb = getCameraFrame();

        if (!fb) {
            xSemaphoreGive(camMutex);
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        sendUdpFrame(fb, frameId++, config.clientIp, config.clientPort);

        releaseCameraFrame(fb);
        xSemaphoreGive(camMutex);

        taskYIELD();
    }

cleanup:

    udpVideo.stop();

    if (config.telemetryPort > 0) {
        udpTelemetry.stop();
    }

    if (camMutex != nullptr && xSemaphoreTake(camMutex, pdMS_TO_TICKS(1000))) {
        ensureCameraMode(CAM_LOW);
        xSemaphoreGive(camMutex);
    }

    udpStopRequested = false;
    udpStreamingActive = false;
    streamingActive = false;
    lastUdpClientSeenMs = 0;
    udpClientPort = 0;
    udpClientTelemetryPort = 0;

    vTaskDelete(nullptr);
}

static bool sendUdpFrame(camera_fb_t* fb, uint16_t frameId, IPAddress clientIp, uint16_t clientPort) {

    if (!fb || fb->len == 0) {
        return false;
    }

    const uint16_t packetCount = (fb->len + udpVideoPayloadSize - 1) / udpVideoPayloadSize;

    for (uint16_t packetIndex = 0; packetIndex < packetCount; packetIndex++) {

        if (udpStopRequested) {
            return false;
        }

        size_t offset = packetIndex * udpVideoPayloadSize;
        size_t remaining = fb->len - offset;
        size_t payloadSize = remaining > udpVideoPayloadSize ? udpVideoPayloadSize : remaining;

        UdpVideoPacketHeader header;
        header.magic = 0x5256; // 'RV'
        header.frameId = frameId;
        header.packetIndex = packetIndex;
        header.packetCount = packetCount;
        header.payloadSize = payloadSize;

        if (!udpVideo.beginPacket(clientIp, clientPort)) {
            return false;
        }

        udpVideo.write((const uint8_t*)&header, sizeof(header));
        udpVideo.write(fb->buf + offset, payloadSize);

        if (!udpVideo.endPacket()) {
            return false;
        }

        taskYIELD();
    }

    return true;
}

static bool sendUdpTelemetry(IPAddress clientIp, uint16_t telemetryPort) {

    if (telemetryPort == 0) {
        return false;
    }

    char json[512];
    telemetry.toJSON(json, sizeof(json));

    if (!udpTelemetry.beginPacket(clientIp, telemetryPort)) {
        return false;
    }

    udpTelemetry.write((const uint8_t*)json, strlen(json));

    if (!udpTelemetry.endPacket()) {
        return false;
    }

    taskYIELD();
    return true;
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


