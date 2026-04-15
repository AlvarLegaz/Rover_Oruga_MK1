#include "web_server_rover.h"
#include "camera_driver_OV2640.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


extern Preferences preferences;
WebServer server(80);

SemaphoreHandle_t camMutex = NULL;

void setupWebServer() {

  camMutex = xSemaphoreCreateMutex(); 

  // --- RUTAS ACTIVAS ---
  
  // 1. Página principal con interfaz HTML (usa /capture para imágenes)
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", STREAM_ONLY_HTML);
  });

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

  String json = "{";
  json += "\"endpoints\":[";

  json += "{\"path\":\"/\",\"desc\":\"Interfaz web\"},";
  json += "{\"path\":\"/capture\",\"desc\":\"Imagen JPEG\"},";
  json += "{\"path\":\"/stream\",\"desc\":\"Stream MJPEG\"},";
  json += "{\"path\":\"/stream_low\",\"desc\":\"Stream MJPEG bajo consumo\"},";
  json += "{\"path\":\"/telemetry\",\"desc\":\"Datos JSON\"},";
  json += "{\"path\":\"/config\",\"desc\":\"Config WiFi\"},";
  json += "{\"path\":\"/save\",\"desc\":\"Guardar WiFi\"},";
  json += "{\"path\":\"/info\",\"desc\":\"Lista de endpoints\"}";

  json += "]";
  json += "}";

  server.send(200, "application/json", json);
}

void handleConfig() {
  int n = WiFi.scanNetworks();  // Escanear redes WiFi

  String html = "<html><body style='font-family:sans-serif; background:#1a1a1a; color:white; padding:20px;'>";
  html += "<h2>Configuracion WiFi</h2>";

  // ===== LISTA DE REDES =====
  html += "<h3>Redes disponibles:</h3>";

  if (n == 0) {
    html += "<p>No se encontraron redes</p>";
  } else {
    html += "<ul style='list-style:none; padding:0;'>";

    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

      html += "<li onclick=\"document.getElementsByName('ssid')[0].value='";
      html += ssid;
      html += "'\" ";
      html += "style='margin-bottom:8px; padding:10px; background:#2c2c2c; border-radius:6px; cursor:pointer;'>";

      html += ssid;
      html += " (";
      html += rssi;
      html += " dBm) ";
      html += open ? "abierta" : "cerrada";
      html += "</li>";
    }

    html += "</ul>";
  }

  // ===== FORMULARIO =====
  html += "<form action='/save' method='POST'>";
  html += "SSID:<br><input type='text' name='ssid' style='width:100%; padding:10px; border-radius:6px; border:none;'><br><br>";
  html += "Pass:<br><input type='password' name='pass' style='width:100%; padding:10px; border-radius:6px; border:none;'><br><br>";
  html += "<input type='submit' value='GUARDAR' style='width:100%; padding:15px; background:#e67e22; color:white; border:none; border-radius:6px;'>";
  html += "</form>";

  html += "<p style='font-size:12px; opacity:0.6;'>Toca una red para copiar el nombre automaticamente</p>";

  html += "</body></html>";

  server.send(200, "text/html", html);

  WiFi.scanDelete();  // Liberar memoria
}


void handleTelemetry() {
  // Enviamos el JSON que pediste anteriormente
  String json = "{\"temperatura\":24.6, \"humedad\":58.2, \"gps\":{\"lat\":-34.6, \"lon\":-58.3}}";
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

  WiFiClient client = server.client();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println("Cache-Control: no-cache");
  client.println("Connection: close");
  client.println();

  Serial.println("🎬 Cliente conectado al stream MJPEG");

  setCameraNormalMode(); 

  const int targetFPS = (WiFi.getMode() == WIFI_AP) ? 5 : 10;
  const int frameInterval = 1000 / targetFPS;

  unsigned long lastFrameTime = 0;
  unsigned long lastClientActivity = millis();
  const unsigned long CLIENT_TIMEOUT = 10000;

  unsigned long statsStartTime = millis();
  int frameCount = 0;

  while (client.connected()) {

    // 🚨 Timeout cliente
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

    esp_task_wdt_reset();
  }

  client.stop();
  Serial.printf("📡 Cliente desconectado (%d frames enviados)\n", frameCount);
}

void handleStreamLow() {

  if (!cameraSupported) {
    server.send(503, "text/plain", "Camara no disponible");
    return;
  }

  setCameraLowStreamMode();

  WiFiClient client = server.client();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println("Cache-Control: no-cache");
  client.println("Connection: close");
  client.println();

  Serial.println("📉 Cliente conectado a STREAM LOW");

  const int targetFPS = 10;
  const int frameInterval = 1000 / targetFPS;

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

    esp_task_wdt_reset();
  }

  client.stop();
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

