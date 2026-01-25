#include "web_server_rover.h"
#include "camera_driver_OV2640.h"
#include <Arduino.h>
#include <Preferences.h>

extern Preferences preferences;
WebServer server(80);

void setupWebServer() {
  // --- RUTAS ACTIVAS ---
  
  // 1. El video directo
  server.on("/stream", HTTP_GET, []() {
    server.send(200, "text/html", STREAM_ONLY_HTML);
  });

  // 2. La configuración de red
  server.on("/config", HTTP_GET, handleConfig);

  // 3. Endpoints de datos
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/telemetry", HTTP_GET, handleTelemetry);
  server.on("/save", HTTP_POST, handleSave);

  // --- RUTA RAIZ DESACTIVADA ---
  server.on("/", HTTP_GET, []() {
    server.send(404, "text/plain", "Acceso denegado.");
  });
  
  server.begin();
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