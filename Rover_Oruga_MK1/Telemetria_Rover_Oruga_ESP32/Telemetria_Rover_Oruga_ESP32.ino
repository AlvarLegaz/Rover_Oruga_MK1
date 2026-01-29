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

void setup() {
  // 1. Leer pin de modo ANTES de iniciar Serial para evitar ruidos en RX
  pinMode(3, INPUT_PULLUP);
  delay(100);
  bool forceAP = (digitalRead(3) == false);

  Serial.begin(115200);
  
  // ========================================
  // SOLUCIÓN 4: WATCHDOG TIMER
  // ========================================
  Serial.println("Inicializando Watchdog (30s timeout)...");
  esp_task_wdt_init(30, true); // 30 segundos antes de reinicio automático
  esp_task_wdt_add(NULL); // Añadir tarea actual al watchdog
  Serial.println("Watchdog activado ✓");
  
  // 2. Cargar credenciales de memoria Flash 
  preferences.begin("wifi-conf", true); // true = solo lectura
  String ssid_stored = preferences.getString("ssid", ""); 
  String pass_stored = preferences.getString("pass", "");
  preferences.end();

  WiFi.setTxPower(WIFI_POWER_19_5dBm); 
  cameraSupported = initCamera(); 

  if (forceAP) {
    useAPmode = true;
    WiFi.softAPConfig(local_IP, gateway, subnet); 
    WiFi.softAP("ROVER-TELEMETRIA-MODE", "12345678"); 
    Serial.println("╔═══════════════════════════════════════╗");
    Serial.println("║  MODO CONFIGURACIÓN ACTIVO (AP)      ║");
    Serial.println("║  IP: 192.168.4.1                     ║");
    Serial.println("║  SSID: ROVER-TELEMETRIA              ║");
    Serial.println("║  Pass: 12345678                      ║");
    Serial.println("╚═══════════════════════════════════════╝");
    
    // Reducir calidad en modo AP para evitar cuelgues
    if (cameraSupported) {
      sensor_t* s = esp_camera_sensor_get();
      s->set_framesize(s, FRAMESIZE_CIF); // 400x296 (menor que VGA)
      s->set_quality(s, 18); // Más compresión (10=mejor, 63=peor)
      Serial.println("Cámara ajustada para modo AP (CIF, Q18)");
    }
    
  } else {
    // Validar que hay credenciales guardadas
    if (ssid_stored.length() == 0) {
      Serial.println("⚠ No hay credenciales guardadas. Activa modo AP (pin 3 a GND)");
      Serial.println("Continuando sin WiFi...");
    } else {
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
      } else {
        Serial.println("\n⚠ Fallo conexión WiFi.");
        Serial.println("Reinicia en modo AP (pin 3 a GND) para configurar.");
      }
    }
  }

  setupWebServer();
  Serial.println("\nServidor Web iniciado ✓");
  Serial.println("Endpoints disponibles:");
  Serial.println("  /stream   - Video en vivo");
  Serial.println("  /config   - Configuración WiFi");
  Serial.println("  /capture  - Captura individual");
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
