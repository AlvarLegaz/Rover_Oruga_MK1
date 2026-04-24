#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "web_server_rover.h"
#include "camera_driver_OV2640.h"
#include "Telemetry.h"
#include "info_page.h"
#include "config_page.h"
#include "gps.h"

enum CameraMode {
  CAM_LOW,
  CAM_HIGH
};

volatile CameraMode currentMode = CAM_LOW;
volatile bool streamingHighActive = false;

const int targetFPS_high  = 10;
const int targetFPS_low = 20;
const int web_port = 80;

extern Preferences preferences;
WebServer server(web_port);

Telemetry telemetry;

SemaphoreHandle_t camMutex = NULL;
static volatile bool streamingActive = false;

void setupWebServer() {

  initGPS(13, 12, 38400);
  
  camMutex = xSemaphoreCreateMutex(); 

  // --- RUTAS ACTIVAS ---
  

  server.on("/", HTTP_GET, handleInfo);
  server.on("/info", HTTP_GET, handleInfo);
  

  // 2. Endpoints de cámara
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/stream", HTTP_GET, handleStream);  // Stream MJPEG para VLC
  server.on("/stream_low", HTTP_GET, handleStreamLow);
  
  // 3. Telemetría
  server.on("/telemetry", HTTP_GET, handleTelemetry);

  // 4. Configuración WiFi
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/save", HTTP_POST, handleSave);

  server.begin();
}

void handleInfo() {
  server.send_P(200, "text/html", INFO_PAGE);
}

void handleConfig() {

  int n = WiFi.scanNetworks();

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  // Inicio HTML
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
        "<li onclick=\"document.getElementsByName('ssid')[0].value='%s'\" "
        "style='margin-bottom:8px; padding:10px; background:#2c2c2c; border-radius:6px; cursor:pointer;'>"
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

  // Final HTML
  server.sendContent(CONFIG_FOOTER);

  WiFi.scanDelete();
}



void handleCapture() {
  if (!cameraSupported) return;
  camera_fb_t* fb = getCameraFrame();
  if (!fb) { server.send(500, "text/plain", "Error Camara"); return; }
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  server.sendContent((const char *)fb->buf, fb->len);
  releaseCameraFrame(fb);
}

void ensureCameraMode(CameraMode mode) {
  if (currentMode == mode) return;

  if (mode == CAM_HIGH) {
    Serial.println("📷 Cambiando a HIGH");
    setCameraNormalMode();
  } else {
    Serial.println("📷 Cambiando a LOW");
    setCameraLowStreamMode();
  }

  currentMode = mode;
}

void streamTask(void *param) {
  WiFiClient client = *((WiFiClient*)param);
  delete (WiFiClient*)param;

  Serial.println("🎬 Cliente conectado al stream MJPEG");

  streamingHighActive = true;

  // 🔥 Forzar modo HIGH
  xSemaphoreTake(camMutex, portMAX_DELAY);
  ensureCameraMode(CAM_HIGH);
  xSemaphoreGive(camMutex);

  const int frameInterval = 1000 / targetFPS_high;

  unsigned long lastFrameTime = 0;
  unsigned long lastClientActivity = millis();
  const unsigned long CLIENT_TIMEOUT = 10000;

  int frameCount = 0;
  unsigned long statsStartTime = millis();

  while (client.connected()) {

    if (millis() - lastClientActivity > CLIENT_TIMEOUT) {
      Serial.println("⏱️ Cliente inactivo, cerrando");
      break;
    }

    unsigned long now = millis();

    if (now - lastFrameTime < frameInterval) {
      vTaskDelay(1);
      continue;
    }

    camera_fb_t* fb = NULL;

    if (xSemaphoreTake(camMutex, pdMS_TO_TICKS(100))) {

      fb = getCameraFrame();

      if (fb) {
        client.println("--frame");
        client.println("Content-Type: image/jpeg");
        client.print("Content-Length: ");
        client.println(fb->len);
        client.println();

        size_t written = client.write(fb->buf, fb->len);
        client.println();

        releaseCameraFrame(fb);

        if (written > 0) {
          lastClientActivity = millis();
          lastFrameTime = now;
        } else {
          Serial.println("❌ Cliente no recibe datos");
          xSemaphoreGive(camMutex);
          break;
        }

        frameCount++;

        if (frameCount % 30 == 0) {
          float fps = (frameCount * 1000.0) / (millis() - statsStartTime);
          Serial.printf("📊 HIGH FPS: %.1f\n", fps);
        }
      }

      xSemaphoreGive(camMutex);
    }

    vTaskDelay(2);
    esp_task_wdt_reset();
  }

  client.stop();

  // 🔥 Restaurar modo LOW al terminar
  xSemaphoreTake(camMutex, portMAX_DELAY);
  streamingHighActive = false;
  ensureCameraMode(CAM_LOW);
  xSemaphoreGive(camMutex);

  streamingActive = false;

  Serial.println("📡 Cliente desconectado");
  vTaskDelete(NULL);
}


void handleTelemetry() {

  xSemaphoreTake(camMutex, portMAX_DELAY);

  if (!streamingHighActive) ensureCameraMode(CAM_LOW);

  xSemaphoreGive(camMutex);

  char json[420];

  if (gps_fix) {

    snprintf(json, sizeof(json),
      "{"
      "\"gps\":{"
      "\"fix\":true,"
      "\"lat\":%.6f,"
      "\"lon\":%.6f,"
      "\"speed\":%.1f,"
      "\"course\":%.1f,"
      "\"dir\":\"%s\","
      "\"sats\":%d"
      "}"
      "}",
      gps_lat,
      gps_lon,
      gps_speed,
      gps_course,
      courseToText(gps_course),
      gps_sats
    );

  } else {

    snprintf(json, sizeof(json),
      "{"
      "\"gps\":{"
      "\"fix\":false,"
      "\"lat\":null,"
      "\"lon\":null,"
      "\"speed\":0,"
      "\"course\":0,"
      "\"dir\":\"---\","
      "\"sats\":%d"
      "}"
      "}",
      gps_sats
    );
  }

  server.send(200, "application/json", json);
}


void handleStream() {
  if (!cameraSupported) {
    server.send(503, "text/plain", "Camara no disponible");
    return;
  }

  if (streamingActive) {
    server.send(503, "text/plain", "Stream ocupado");
    return;
  }

  streamingActive = true;

  WiFiClient client = server.client();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println("Cache-Control: no-cache");
  client.println("Connection: close");
  client.println();

  WiFiClient* clientCopy = new WiFiClient(client);

  xTaskCreatePinnedToCore(
    streamTask,
    "streamTask",
    8192,
    clientCopy,
    1,
    NULL,
    1
  );
}

void streamLowTask(void *param) {
  WiFiClient client = *((WiFiClient*)param);
  delete (WiFiClient*)param;

  Serial.println("📉 Cliente conectado a STREAM LOW");

  // 🔥 IMPORTANTE: LOW no activa streamingHighActive
  xSemaphoreTake(camMutex, portMAX_DELAY);
  ensureCameraMode(CAM_LOW);
  xSemaphoreGive(camMutex);

  const int frameInterval = 1000 / targetFPS_low;

  unsigned long lastFrameTime = 0;
  unsigned long lastClientActivity = millis();
  const unsigned long CLIENT_TIMEOUT = 10000;

  int frameCount = 0;
  unsigned long statsStartTime = millis();

  while (client.connected()) {

    if (millis() - lastClientActivity > CLIENT_TIMEOUT) {
      Serial.println("⏱️ Cliente inactivo (LOW), cerrando");
      break;
    }

    unsigned long now = millis();

    if (now - lastFrameTime < frameInterval) {
      vTaskDelay(1);
      continue;
    }

    camera_fb_t* fb = NULL;

    if (xSemaphoreTake(camMutex, pdMS_TO_TICKS(100))) {

      // 🔥 asegurar modo LOW (por si alguien cambió a HIGH)
      if (!streamingHighActive) {
        ensureCameraMode(CAM_LOW);
      }

      fb = getCameraFrame();

      if (fb) {
        client.println("--frame");
        client.println("Content-Type: image/jpeg");
        client.print("Content-Length: ");
        client.println(fb->len);
        client.println();

        size_t written = client.write(fb->buf, fb->len);
        client.println();

        releaseCameraFrame(fb);

        if (written > 0) {
          lastClientActivity = millis();
          lastFrameTime = now;
        } else {
          Serial.println("❌ Error enviando (LOW)");
          xSemaphoreGive(camMutex);
          break;
        }

        frameCount++;

        if (frameCount % 30 == 0) {
          float fps = (frameCount * 1000.0) / (millis() - statsStartTime);
          Serial.printf("📊 LOW FPS: %.1f\n", fps);
        }
      }

      xSemaphoreGive(camMutex);
    }

    vTaskDelay(2);
    esp_task_wdt_reset();
  }

  client.stop();
  streamingActive = false;

  Serial.println("📡 Cliente desconectado de STREAM LOW");
  vTaskDelete(NULL);
}

void handleStreamLow() {

  if (!cameraSupported) {
    server.send(503, "text/plain", "Camara no disponible");
    return;
  }

  if (streamingActive) {
    server.send(503, "text/plain", "Stream ocupado");
    return;
  }

  streamingActive = true;

  WiFiClient client = server.client();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println("Cache-Control: no-cache");
  client.println("Connection: close");
  client.println();

  WiFiClient* clientCopy = new WiFiClient(client);

  xTaskCreatePinnedToCore(
    streamLowTask,
    "streamLowTask",
    8192,
    clientCopy,
    1,
    NULL,
    1
  );
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    preferences.begin("wifi-conf", false);
    preferences.putString("ssid", server.arg("ssid"));
    preferences.putString("pass", server.arg("pass"));
    preferences.end();
    server.send(200, "text/plain", "Configuracion guardada. Reiniciando...");
    delay(2000);
    ESP.restart();
  }
}

