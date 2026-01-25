#ifndef WEB_SERVER_ROVER_H
#define WEB_SERVER_ROVER_H

#include <WebServer.h>
#include <WiFi.h>

// Esto le dice a todos los archivos: "Hay una variable booleana llamada cameraSupported en otro lado"
extern bool cameraSupported; 
extern WebServer server;
extern const char* ap_ssid;
extern const char* ap_password;

// --- HTML: SÓLO STREAM (Ruta /stream) ---
const char STREAM_ONLY_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Rover Stream</title>
<style>
  body { background: #000; margin: 0; display: flex; justify-content: center; align-items: center; height: 100vh; overflow: hidden; }
  img { max-width: 100%; max-height: 100%; object-fit: contain; border: 2px solid #222; }
</style></head><body>
  <img id='stream' src='/capture' />
  <script>setInterval(() => { document.getElementById('stream').src = '/capture?t=' + Date.now(); }, 100);</script>
</body></html>
)rawliteral";
void setupWebServer();
void handleCapture();
void handleTelemetry();
void handleHealth();
void handleConfig(); 
void handleSave();

#endif