#include "web_server_rover.h"
#include "camera_driver_OV2640.h"
#include <Arduino.h>

WebServer server(80);

// El HTML vive aquí, fuera del .ino
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>Rover Oruga Dash</title>
<style>
  body { font-family: sans-serif; background: #1a1a1a; color: white; text-align: center; margin: 0; padding: 10px; }
  .container { width: 100%; max-width: 800px; margin: auto; }
  .video-box { width: 100%; border-radius: 8px; border: 2px solid #444; background: #000; }
  .video-box img { width: 100%; height: auto; }
  .telemetry-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 10px; margin-top: 15px; }
  .card { background: #333; padding: 15px; border-radius: 8px; border-bottom: 4px solid #007bff; }
</style>
</head><body>
  <div class='container'>
    <h2>ROVER ORUGA</h2>
    <div class='video-box'><img id='video' src='/capture' /></div>
    <div class='telemetry-grid'>
      <div class='card'><h3>Temp</h3><p id='temp'>--</p></div>
      <div class='card'><h3>Batería</h3><p id='bat'>--</p></div>
    </div>
  </div>
  <script>
    setInterval(() => { document.getElementById('video').src = '/capture?t=' + Date.now(); }, 150);
    setInterval(() => {
      fetch('/telemetry').then(r => r.json()).then(data => {
        document.getElementById('temp').innerText = data.temperatura + '°C';
      });
    }, 1000);
  </script>
</body></html>
)rawliteral";

void setupWebServer() {
  server.on("/stream", HTTP_GET, []() {
    server.send(200, "text/html", INDEX_HTML);
  });
  
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/telemetry", HTTP_GET, handleTelemetry);
  server.on("/health", HTTP_GET, handleHealth);
  
  server.begin();
  Serial.println("Servidor Web desacoplado iniciado");
}

void handleHealth() {
  String statusStr = cameraSupported ? "initialized" : "not_initialized";
  String json = "{\"status\":\"ok\",\"camera\":\"" + statusStr + "\"}";
  server.send(200, "application/json", json);
}

void handleCapture() {
  if (!cameraSupported) return;

  // 1. Capturamos y descartamos un frame antiguo para limpiar el buffer
  camera_fb_t* fb_old = getCameraFrame();
  releaseCameraFrame(fb_old);

  // 2. Capturamos el frame actual
  camera_fb_t* fb = getCameraFrame();
  if (!fb) {
    server.send(500, "text/plain", "Fallo");
    return;
  }

  // 3. Enviamos de forma optimizada
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  
  // Enviamos por bloques para no saturar la conexión
  uint8_t *fb_ptr = fb->buf;
  size_t fb_len = fb->len;
  while (fb_len > 0) {
    size_t chunk = (fb_len > 1024) ? 1024 : fb_len;
    server.sendContent((const char *)fb_ptr, chunk);
    fb_ptr += chunk;
    fb_len -= chunk;
  }

  releaseCameraFrame(fb);
}

void handleStreamPage() {
  String html = "<!DOCTYPE html><html><head><title>Rover Stream</title>";
  html += "<style>body{ background:#222; color:white; text-align:center; font-family:sans-serif; }</style>";
  html += "</head><body>";
  html += "<h2>ROVER ORUGA - VISTA EN VIVO</h2>";
  html += "<img id='video' src='/capture' style='width:640px; border:2px solid #555;'>";
  
  // JavaScript para actualizar solo la imagen cada 200ms
  html += "<script>";
  html += "setInterval(function(){";
  html += "  document.getElementById('video').src = '/capture?t=' + Date.now();";
  html += "}, 200);";
  html += "</script>";
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleTelemetry() {
  // Datos simulados (puedes conectar sensores reales aquí)
  String json = "{";
  json += "\"temperatura\":24.6,";
  json += "\"humedad\":58.2,";
  json += "\"gps\":{\"lat\":-34.6037, \"lon\":-58.3816}";
  json += "}";
  server.send(200, "application/json", json);
}