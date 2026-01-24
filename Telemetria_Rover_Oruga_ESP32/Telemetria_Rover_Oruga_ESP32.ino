#include <WiFi.h>
#include "camera_driver_OV2640.h"
#include "web_server_rover.h"

// --- CONFIGURACIÓN DE RED ---
// Cambia useAPmode a true si quieres que el Rover cree su propia red
const char *ssid = "MOVISTAR-WIFI6-48A8";
const char *password = "aMYFm4cR4f3c79T4Xc3X";
const char *ap_ssid = "ROVER-ORUGA-TEL";
const char *ap_password = "12345678";

bool useAPmode = false;
bool cameraSupported = false;

// Configuración IP Estática para el Modo AP (Punto de Acceso)
IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

void setup() {
  Serial.begin(115200);
  
  // Aumentar potencia de la antena WiFi
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  
  // Inicialización de la cámara
  cameraSupported = initCamera();
  if (cameraSupported) {
    Serial.println("Cámara OV2640 inicializada correctamente");
  } else {
    Serial.println("Error crítico: No se pudo iniciar la cámara");
  }

  // Configuración de la conexión inalámbrica
  if (useAPmode) {
    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP(ap_ssid, ap_password);
    Serial.println("Modo AP iniciado. Conéctate a: " + String(ap_ssid));
    Serial.println("IP del Rover: 192.168.4.1");
  } else {
    WiFi.begin(ssid, password);
    Serial.print("Conectando a WiFi " + String(ssid));
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nWiFi conectado con éxito");
    Serial.print("IP del Rover: ");
    Serial.println(WiFi.localIP());
  }

  // Arranca el servidor web (definido en web_server_rover.cpp)
  setupWebServer();
  Serial.println("Servidor web listo para recibir peticiones");
}

void loop() {
  // Maneja las peticiones de los clientes (Stream, Telemetría, etc.)
  // IMPORTANTE: No añadir setupWebServer() aquí dentro.
  server.handleClient();
  
  // Pequeño delay para estabilidad del sistema
  delay(1);
}