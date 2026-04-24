#include <WiFi.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "camera_driver_OV2640.h"
#include "web_server_rover.h"

Preferences preferences;
bool cameraSupported = false;
bool useAPmode = false;

// Configuración IP para Modo AP
IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// NOMBRE AP
char apName[32];


void setup() {
  // 1. Leer pin de modo ANTES de iniciar Serial para evitar ruidos en RX
  pinMode(12, INPUT_PULLUP);
  delay(100);
  bool forceAP = (digitalRead(12) == LOW);

  Serial.begin(115200);
  
  // 1. Configura Watchdog

 // Serial.println("Inicializando Watchdog (30s timeout)...");

  // Desinicializar watchdog por defecto si existe
  esp_task_wdt_deinit();

  // Configurar nuestro watchdog
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 30000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);

  Serial.println("Watchdog activado ✓");
    
  // 2. Cargar credenciales de memoria Flash 
  preferences.begin("wifi-conf", true); // true = solo lectura
  String ssid_stored = preferences.getString("ssid", ""); 
  String pass_stored = preferences.getString("pass", "");
  preferences.end();

  // 3. Establece potencia Wifi.
  WiFi.setTxPower(WIFI_POWER_19_5dBm); 

  // 4. Inicializa cámara
  cameraSupported = initCamera(); 

  // 5. Selecciona modo de trabajo de Wifi.
  if (forceAP || ssid_stored.length() == 0) 
  {
    uint64_t chipid = ESP.getEfuseMac();  
    uint16_t mac_last4 = (uint16_t)(chipid & 0xFFFF);
    sprintf(apName, "ROVER-TELEMETRIA-%04X", mac_last4);
    useAPmode = true;
    WiFi.softAPConfig(local_IP, gateway, subnet); 
    WiFi.softAP(apName, "12345678"); 
    Serial.println("╔═══════════════════════════════════════╗");
    Serial.println("║  MODO CONFIGURACIÓN ACTIVO (AP)       ║");
    Serial.println("║  IP: 192.168.4.1                      ║");

    Serial.print  ("║  SSID: ");
    Serial.print  (apName);
    Serial.println("           ");

    Serial.println("║  Pass: 12345678                       ║");
    Serial.println("╚═══════════════════════════════════════╝");
    
    // Reducir calidad en modo AP para evitar cuelgues
    if (cameraSupported) {
      setCameraLowStreamMode();
    }
    
  } 
  else
  {
    WiFi.begin(ssid_stored.c_str(), pass_stored.c_str()); 
     Serial.print("Conectando a: "); Serial.println(ssid_stored);
      
    int cont = 0;
    while (WiFi.status() != WL_CONNECTED && cont < 20) {
      delay(500); 
      Serial.print("."); 
      cont++;
      esp_task_wdt_reset(); // Resetear watchdog durante conexión
    } 
      
    if(WiFi.status() == WL_CONNECTED) {
      Serial.println("\n╔═══════════════════════════════════════╗");
      Serial.print("║  WiFi Conectado ✓                    ║\n");
      Serial.print("║  IP: ");
      Serial.print(WiFi.localIP());
      Serial.println("              ║");
      Serial.println("╚═══════════════════════════════════════╝");

      // 🔵 ACTIVAR CALIDAD NORMAL AL TENER WIFI ESTABLE
      if (cameraSupported) {
        setCameraNormalMode();
      }
    } else 
    {
       Serial.println("\n⚠ Fallo conexión WiFi.");
       Serial.println("Reinicia en modo AP (pin 3 a GND) para configurar.");
    }
  }

  setupWebServer();
  Serial.println("\nServidor Web iniciado ✓");
  Serial.println("Endpoints disponibles:");
  Serial.println("  /   - Video en vivo desde interfaz web");
  Serial.println("  /config   - Configuración WiFi");
  Serial.println("  /capture  - Captura individual");
  Serial.println("  /stream   - Flujo de video para VLC o similar");
  Serial.println("  /telemetry - Datos de sensores");
}

void loop() {
  // ========================================
  // SOLUCIÓN 4: RESETEAR WATCHDOG
  // ========================================
  esp_task_wdt_reset(); // Indicar que el sistema está funcionando
  
  server.handleClient(); 
  delay(1);
}
