#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "camera_driver_OV2640.h"

// ================== CONFIGURACIÓN WIFI ==================
const char *ssid = "MOVISTAR-WIFI6-48A8";
const char *password = "aMYFm4cR4f3c79T4Xc3X";

// Modo AP
const char *ap_ssid = "ROVER-ORUGA-TEL";
const char *ap_password = "12345678";

// true  -> AP
// false -> STA
bool useAPmode = false;

// ================== SERVIDOR ==================
AsyncWebServer server(80);

// ================== ESTADO ==================
bool cameraSupported = false;

// ================== PROTOTIPOS ==================
void handleHealth(AsyncWebServerRequest *request);
void handleCapture(AsyncWebServerRequest *request);
void handleStreamPage(AsyncWebServerRequest *request);
void handleTelemetry(AsyncWebServerRequest *request);

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);

  // ---- CÁMARA ----
  cameraSupported = initCamera();
  if (!cameraSupported) {
    Serial.println("Error: cámara no soportada o fallo al iniciar");
  }

  // ---- WIFI ----
  if (useAPmode) {
    WiFi.softAP(ap_ssid, ap_password);
    Serial.println("Modo AP iniciado");
    Serial.print("IP AP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    WiFi.begin(ssid, password);
    Serial.print("Conectando a WiFi");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nWiFi conectado");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  }

  // ---- ENDPOINTS ----
  server.on("/health", HTTP_GET, handleHealth);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/stream", HTTP_GET, handleStreamPage);
  server.on("/telemetry", HTTP_GET, handleTelemetry);

  server.begin();
  Serial.println("Servidor ESPAsyncWebServer iniciado");
}

// ================== LOOP ==================
void loop() {
  // No se usa en servidor asíncrono
}

// ================== HANDLERS ==================

void handleHealth(AsyncWebServerRequest *request) {
  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"camera\":";
  json += cameraSupported ? "\"initialized\"" : "\"not_initialized\"";
  json += "}";

  request->send(200, "application/json", json);
}

void handleCapture(AsyncWebServerRequest *request) {

  if (!cameraSupported) {
    request->send(500, "text/plain", "Error de camara");
    return;
  }

  uint8_t *img_buf = nullptr;
  size_t img_len = 0;
  uint16_t width = 0, height = 0;

  if (getImage(&img_buf, &img_len, &width, &height) != ESP_OK || !img_buf) {
    request->send(500, "text/plain", "Error al capturar imagen");
    return;
  }

  AsyncWebServerResponse *response =
      request->beginResponse_P(200, "image/jpeg", img_buf, img_len);

  response->addHeader("Cache-Control", "no-store");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Connection", "close");

  request->send(response);

  free(img_buf);
}

void handleStreamPage(AsyncWebServerRequest *request) {

  String html =
      "<!DOCTYPE html>"
      "<html><head><title>ESP32-CAM Stream</title>"
      "<meta http-equiv='refresh' content='1'>"
      "</head><body>"
      "<h2>ESP32-CAM</h2>"
      "<img src='/capture' width='640' height='480' />"
      "</body></html>";

  request->send(200, "text/html", html);
}

void handleTelemetry(AsyncWebServerRequest *request) {

  // ---- Datos simulados ----
  float temperatura = 24.6;
  float humedad = 58.2;

  float latitud = -34.6037;
  float longitud = -58.3816;
  float altitud = 25.4;

  float roll = 1.2;
  float pitch = -0.8;
  float yaw = 182.5;

  // ---- JSON ----
  String json = "{";
  json += "\"temperatura\":" + String(temperatura, 2) + ",";
  json += "\"humedad\":" + String(humedad, 2) + ",";
  json += "\"gps\":{";
  json += "\"lat\":" + String(latitud, 6) + ",";
  json += "\"lon\":" + String(longitud, 6) + ",";
  json += "\"alt\":" + String(altitud, 2) + "},";
  json += "\"orientacion\":{";
  json += "\"roll\":" + String(roll, 2) + ",";
  json += "\"pitch\":" + String(pitch, 2) + ",";
  json += "\"yaw\":" + String(yaw, 2) + "}";
  json += "}";

  request->send(200, "application/json", json);
}