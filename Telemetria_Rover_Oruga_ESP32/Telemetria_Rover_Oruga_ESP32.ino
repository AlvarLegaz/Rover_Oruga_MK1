#include <WiFi.h>
#include <Preferences.h>
#include "camera_driver_OV2640.h"
#include "web_server_rover.h"

Preferences preferences;
bool cameraSupported = false;
bool useAPmode = false;

// Configuración IP para Modo AP
IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

void setup() {
  // 1. Leer pin de modo ANTES de iniciar Serial para evitar ruidos en RX
  pinMode(3, INPUT_PULLUP);
  delay(100);
  bool forceAP = (digitalRead(3) == LOW);

  Serial.begin(115200);
  
  // 2. Cargar credenciales de memoria Flash 
  preferences.begin("wifi-conf", true); // true = solo lectura
  String ssid_stored = preferences.getString("ssid", "MOVISTAR-WIFI6-48A8"); 
  String pass_stored = preferences.getString("pass", "aMYFm4cR4f3c79T4Xc3X");
  preferences.end();

  WiFi.setTxPower(WIFI_POWER_19_5dBm); 
  cameraSupported = initCamera(); 

  if (forceAP) {
    useAPmode = true;
    WiFi.softAPConfig(local_IP, gateway, subnet); 
    WiFi.softAP("ROVER-CONFIG-MODE", "12345678"); 
    Serial.println("MODO CONFIGURACIÓN ACTIVO (IP: 192.168.4.1)");
  } else {
    WiFi.begin(ssid_stored.c_str(), pass_stored.c_str()); 
    Serial.print("Conectando a: "); Serial.println(ssid_stored);
    
    int cont = 0;
    while (WiFi.status() != WL_CONNECTED && cont < 20) {
      delay(500); Serial.print("."); cont++;
    } 
    
    if(WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConectado. IP: "); Serial.println(WiFi.localIP());
    } else {
      Serial.println("\nFallo conexión. Reinicia en modo AP para configurar.");
    }
  }

  setupWebServer();
}

void loop() {
  server.handleClient(); 
  delay(1);
}