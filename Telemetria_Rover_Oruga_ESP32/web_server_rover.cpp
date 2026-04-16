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

const int targetFPS_high  = 10;
const int targetFPS_low = 20;
const int web_port = 80;

extern Preferences preferences;
WebServer server(web_port);

Telemetry telemetry;

SemaphoreHandle_t camMutex = NULL;
static volatile bool streamingActive = false;

void setupWebServer() {

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

  WiFiClient client = server.client();

  // Cabecera HTTP
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  // HTML inicio
  client.print(CONFIG_HEADER);

  if (n == 0) {
    client.print("<p>No se encontraron redes</p>");
  } else {

    char line[256];

    for (int i = 0; i < n; ++i) {

      const char* ssid = WiFi.SSID(i).c_str();
      int rssi = WiFi.RSSI(i);
      bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

      snprintf(line, sizeof(line),
        "<li onclick=\"document.getElementsByName('ssid')[0].value='%s'\" "
        "style='margin-bottom:8px; padding:10px; background:#2c2c2c; border-radius:6px; cursor:pointer;'>"
        "%s (%d dBm) %s"
        "</li>",
        ssid,
        ssid,
        rssi,
        open ? "abierta" : "cerrada"
      );

      client.print(line);
    }
  }

  // HTML final
  client.print(CONFIG_FOOTER);

  WiFi.scanDelete();
}


void handleTelemetry() {
  char json[160];

  telemetry.toJSON(json, sizeof(json));

  server.send(200, "application/json", json);
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

void handleStream() {
  if (!cameraSupported) {
    server.send(503, "text/plain", "Camara no disponible");
    return;
  }

  // Evitar múltiples clientes
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

  Serial.println("🎬 Cliente conectado al stream MJPEG");

  xSemaphoreTake(camMutex, portMAX_DELAY);
  setCameraNormalMode(); 
  xSemaphoreGive(camMutex);

  const int frameInterval = 1000 / targetFPS_high;

  unsigned long lastFrameTime = 0;
  unsigned long lastClientActivity = millis();
  const unsigned long CLIENT_TIMEOUT = 10000;

  unsigned long statsStartTime = millis();
  int frameCount = 0;

  while (client.connected()) {

    // Timeout cliente
    if (millis() - lastClientActivity > CLIENT_TIMEOUT) {
      Serial.println("⏱️ Cliente inactivo, cerrando conexión");
      break;
    }

    unsigned long now = millis();

    // 🚀 CONTROL NO BLOQUEANTE
    if (now - lastFrameTime < frameInterval) {
      delay(1); // yield
      continue;
    }

    camera_fb_t* fb = NULL;

    // 🔒 MUTEX
    if (xSemaphoreTake(camMutex, pdMS_TO_TICKS(100))) {

      fb = getCameraFrame();

      if (!fb) {
        Serial.println("❌ Error capturando frame");
        xSemaphoreGive(camMutex);
        continue;
      }

      client.println("--frame");
      client.println("Content-Type: image/jpeg");
      client.print("Content-Length: ");
      client.println(fb->len);
      client.println();

      size_t written = client.write(fb->buf, fb->len);
      client.println();

      releaseCameraFrame(fb);
      xSemaphoreGive(camMutex); // 🔓 SIEMPRE

      if (written > 0) {
        lastClientActivity = millis();
        lastFrameTime = now;   // 👈 SOLO si se envía
      } else {
        Serial.println("❌ Cliente no recibe datos");
        break;
      }

      frameCount++;

      // 📊 FPS REAL
      if (frameCount % 30 == 0) {
        unsigned long elapsed = millis() - statsStartTime;
        float fps = (frameCount * 1000.0) / elapsed;
        Serial.printf("📊 Stream REAL: %.1f FPS | %d frames\n", fps, frameCount);
      }

    } else {
      // otro cliente usando cámara
      delay(1);
      continue;
    }

    //CLAVE: dejar respirar al sistema
    vTaskDelay(2);
    esp_task_wdt_reset();
  }

  client.stop();
  streamingActive = false;

  Serial.printf("📡 Cliente desconectado (%d frames enviados)\n", frameCount);
}

void handleStreamLow() {

  if (!cameraSupported) {
    server.send(503, "text/plain", "Camara no disponible");
    return;
  }

  // 🚫 Evitar múltiples clientes
  if (streamingActive) {
    server.send(503, "text/plain", "Stream ocupado");
    return;
  }

  streamingActive = true;
  
  xSemaphoreTake(camMutex, portMAX_DELAY);
  setCameraLowStreamMode();
  xSemaphoreGive(camMutex);

  WiFiClient client = server.client();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println("Cache-Control: no-cache");
  client.println("Connection: close");
  client.println();

  Serial.println("📉 Cliente conectado a STREAM LOW");

  
  const int frameInterval = 1000 / targetFPS_low;

  unsigned long lastFrameTime = 0;
  unsigned long lastClientActivity = millis();
  const unsigned long CLIENT_TIMEOUT = 10000;

  unsigned long statsStartTime = millis(); // 👈 para FPS real
  int frameCount = 0;

  while (client.connected()) {

    // 🚨 Timeout cliente
    if (millis() - lastClientActivity > CLIENT_TIMEOUT) {
      Serial.println("⏱️ Cliente inactivo (LOW), cerrando");
      break;
    }

    unsigned long now = millis();

    // 🚀 CONTROL NO BLOQUEANTE
    if (now - lastFrameTime < frameInterval) {
      delay(1); // yield (muy importante)
      continue;
    }

    camera_fb_t* fb = NULL;

    // 🔒 MUTEX: acceso exclusivo a cámara
    if (xSemaphoreTake(camMutex, pdMS_TO_TICKS(100))) {

      fb = getCameraFrame();

      if (!fb) {
        xSemaphoreGive(camMutex);
        continue;
      }

      client.println("--frame");
      client.println("Content-Type: image/jpeg");
      client.print("Content-Length: ");
      client.println(fb->len);
      client.println();

      size_t written = client.write(fb->buf, fb->len);
      client.println();

      releaseCameraFrame(fb);
      xSemaphoreGive(camMutex); // 🔓 liberar SIEMPRE

      if (written > 0) {
        lastClientActivity = millis();
        lastFrameTime = now;   // 👈 SOLO si se envía correctamente
      } else {
        Serial.println("❌ Error enviando (LOW)");
        break;
      }

      frameCount++;

      // 📊 FPS real (corregido)
      if (frameCount % 30 == 0) {
        unsigned long elapsed = millis() - statsStartTime;
        float fps = (frameCount * 1000.0) / elapsed;
        Serial.printf("📊 LOW Stream REAL: %.1f FPS\n", fps);
      }

    } else {
      // No pudo coger cámara (otro cliente la usa)
      delay(1);
      continue;
    }

    //CLAVE: dejar respirar al sistema
    vTaskDelay(2);
    esp_task_wdt_reset();
  }

  client.stop();
  streamingActive = false;
  Serial.println("📡 Cliente desconectado de STREAM LOW");
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

