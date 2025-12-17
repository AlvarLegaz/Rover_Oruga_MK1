#include <WiFi.h>
#include <WebServer.h>
#include "camera_driver_OV2640.h"

// ================== CONFIGURACIÓN ==================
const char *ssid = "MOVISTAR-WIFI6-48A8";
const char *password= "aMYFm4cR4f3c79T4Xc3X";

// Para modo AP
const char *ap_ssid = "ROVER-ORUGA-TEL";
const char *ap_password = "12345678";

// Cambia este valor para seleccionar el modo:
// true  -> AP
// false -> STA
bool useAPmode = false;

void handleHealth();
void handleCapture();
void handleTelemetry();

WebServer server(80);

bool cameraSupported = false;

void setup() {
  Serial.begin(115200);

  cameraSupported = initCamera();

  if (!cameraSupported) {
    Serial.println("Cámara no soportada o fallo al iniciar");
  }

   // ================== MODO WIFI ==================
  
 if (useAPmode) {
    // Modo Access Point
    WiFi.softAP(ap_ssid, ap_password);
    Serial.println("Modo AP iniciado");
    Serial.print("IP del AP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    // Modo Estación
    WiFi.begin(ssid, password);
    Serial.print("Conectando a WiFi");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nWiFi conectado");
    Serial.println(WiFi.localIP());
  }

  // Endpoint
  server.on("/health", handleHealth);
  server.on("/capture", handleCapture);
  server.on("/stream", handleStreamPage);
  server.on("/telemetry", handleTelemetry);

  server.begin();
  Serial.println("Servidor HTTP listo");
}

void loop() {
  server.handleClient();

}

void handleHealth() {
  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"camera\":";
  json += cameraSupported ? "\"initialized\"" : "\"not_initialized\"";
  json += "}";

  server.send(200, "application/json", json);
}

// Manejador del endpoint /capture
void handleCapture() {

  if (!cameraSupported) {
    server.send(500, "text/plain", "Error de camara");
    return;
  }

  uint8_t *img_buf = nullptr;
  size_t img_len = 0;
  uint16_t width = 0, height = 0;

  if (getImage(&img_buf, &img_len, &width, &height) == ESP_OK) {
    server.setContentLength(img_len);
    server.send(200, "image/jpeg", "");  // Inicia la respuesta sin cuerpo
    server.sendContent((const char*)img_buf, img_len);
    free(img_buf);
  } else {
    if(img_buf) free(img_buf);  // liberar si algo se asignó
    server.send(500, "text/plain", "Error al capturar imagen");
  }
}

void handleStreamPage() {
  // HTML muy simple con refresco cada 200ms
  String html = "<!DOCTYPE html><html><head><title>ESP32-CAM Stream</title>";
  html += "<meta http-equiv='refresh' content='1'>"; // 0.2 segundos
  html += "</head><body>";
  html += "<h2>ESP32-CAM</h2>";
  html += "<img src='/capture' style='width:640px;height:480px;' />";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleTelemetry() {
  // ---- Lecturas de sensores (ejemplo) ----
  float temperatura = 24.6;   // °C
  float humedad = 58.2;       // %

  // GPS
  float latitud = -34.6037;
  float longitud = -58.3816;
  float altitud = 25.4;       // metros

  // Orientación (IMU / brújula)
  float roll = 1.2;   // grados
  float pitch = -0.8;
  float yaw = 182.5;

  // ---- Construcción del JSON ----
  String json = "{";
  json += "\"temperatura\":" + String(temperatura, 2) + ",";
  json += "\"humedad\":" + String(humedad, 2) + ",";
  json += "\"gps\":{";
  json +=   "\"lat\":" + String(latitud, 6) + ",";
  json +=   "\"lon\":" + String(longitud, 6) + ",";
  json +=   "\"alt\":" + String(altitud, 2);
  json += "},";
  json += "\"orientacion\":{";
  json +=   "\"roll\":" + String(roll, 2) + ",";
  json +=   "\"pitch\":" + String(pitch, 2) + ",";
  json +=   "\"yaw\":" + String(yaw, 2);
  json += "}";
  json += "}";

  // ---- Envío HTTP ----
  server.send(200, "application/json", json);
}
