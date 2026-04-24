#include <WiFi.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "camera_driver_OV2640.h"
#include "web_server_rover.h"
#include "Telemetry.h"

extern Telemetry telemetry; // El objeto vive en el .cpp del servidor

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
  // 1. Detección temprana de hardware
  bool forceAP = checkBootMode(); 

  // 2. Sistemas base
  Serial.begin(115200);
  initWatchdog();
  
  // 3. Conectividad y Cámara
  initWiFi(forceAP);
  cameraSupported = initCamera(); 
  configureCameraByConnection();

  // 4. Servidor y Telemetría
  initWebServerResources(); // Inicializa mutex de cámara 
  telemetry.begin();        // Arranca tarea de sensores y GPS 
  setupWebServer();         // Registra rutas y arranca servidor 
  
  printStatusReport();
}

void loop() {
  // ========================================
  // SOLUCIÓN 4: RESETEAR WATCHDOG
  // ========================================
  esp_task_wdt_reset(); // Indicar que el sistema está funcionando
  
  server.handleClient(); 
  delay(1);
}

// --- SUBFUNCIONES DE ARRANQUE ---

bool checkBootMode() {
  pinMode(12, INPUT_PULLUP);
  delay(100);
  return (digitalRead(12) == LOW);
}

void initWatchdog() {
  esp_task_wdt_deinit();
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 30000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  Serial.println("Watchdog activado ✓");
}

void initWiFi(bool forceAP) {
  preferences.begin("wifi-conf", true);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", ""); 
  preferences.end(); 

  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  if (forceAP || ssid.length() == 0) {
    startAPMode();
  } else {
    if (!connectToWiFi(ssid, pass)) {
      startAPMode(); // Si falla la conexión, fallback a AP
    }
  }
}

void startAPMode() {
  uint64_t chipid = ESP.getEfuseMac(); 
  sprintf(apName, "ROVER-%04X", (uint16_t)(chipid & 0xFFFF)); 
  useAPmode = true;
  WiFi.softAPConfig(local_IP, gateway, subnet); 
  WiFi.softAP(apName, "12345678");
  Serial.printf("Modo AP: %s | IP: 192.168.4.1\n", apName);
}

bool connectToWiFi(String ssid, String pass) {
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.print("Conectando a WiFi");
  
  int cont = 0;
  while (WiFi.status() != WL_CONNECTED && cont < 20) {
    delay(500);
    Serial.print(".");
    cont++;
    esp_task_wdt_reset(); 
  }
  Serial.println("");
  return (WiFi.status() == WL_CONNECTED);
}

void configureCameraByConnection() {
  if (!cameraSupported) return;
  
  if (useAPmode) {
    setCameraLowStreamMode(); // Menos carga para el AP [cite: 5]
  } else {
    setCameraNormalMode();
  }
}

void printStatusReport() {
  Serial.println("\n--- SISTEMA LISTO ---");
  Serial.print("IP Local: "); Serial.println(useAPmode ? "192.168.4.1" : WiFi.localIP().toString());
  Serial.println("Telemetría: ONLINE");
  Serial.println("Servidor Web: ONLINE");
}
