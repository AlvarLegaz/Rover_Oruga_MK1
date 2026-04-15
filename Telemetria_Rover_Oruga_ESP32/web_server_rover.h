#ifndef WEB_SERVER_ROVER_H
#define WEB_SERVER_ROVER_H

#include <WebServer.h>
#include <WiFi.h>

// Variables externas definidas en el archivo principal (.ino)
extern bool cameraSupported; 
extern WebServer server;

// ========================================
// INTERFAZ HUD TRANSPARENTE CON RESOLUCIÓN
// ========================================
const char STREAM_ONLY_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Rover HUD Control</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    
    body { 
      background: #000; 
      display: flex; 
      justify-content: center; 
      align-items: center; 
      height: 100vh; 
      overflow: hidden;
      font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
    }
    
    #container {
      position: relative;
      width: 100vw;
      height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
    }
    
    img { 
      width: 100%; 
      height: 100%; 
      object-fit: cover; /* El video llena toda la pantalla */
      z-index: 1;
    }
    
    /* Panel HUD Transparente */
    #statusBar { 
      position: absolute; 
      top: 20px; 
      left: 20px; 
      background: rgba(0, 0, 0, 0.3); /* Fondo semi-transparente */
      backdrop-filter: blur(8px);    /* Efecto de cristal esmerilado */
      -webkit-backdrop-filter: blur(8px);
      padding: 15px 20px;
      border-radius: 12px;
      border: 1px solid rgba(0, 255, 0, 0.3);
      color: #00ff00; 
      z-index: 10;
      min-width: 200px;
      box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.8);
    }
    
    .status-row {
      display: flex;
      justify-content: space-between;
      margin: 6px 0;
      font-size: 12px;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 1.5px;
    }
    
    .label { color: rgba(0, 255, 0, 0.6); }
    .value { color: #00ff00; text-shadow: 0 0 8px rgba(0, 255, 0, 0.6); }
    
    .mode-ap { color: #ff9900 !important; text-shadow: 0 0 8px rgba(255, 153, 0, 0.6) !important; }
    .mode-sta { color: #00ff00 !important; }
    
    #loading {
      position: absolute;
      color: #00ff00;
      font-size: 14px;
      letter-spacing: 2px;
      z-index: 5;
      text-transform: uppercase;
    }

    /* Líneas de escaneo sutiles */
    #container::after {
      content: " ";
      position: absolute;
      top: 0; left: 0; bottom: 0; right: 0;
      background: linear-gradient(rgba(18, 16, 16, 0) 50%, rgba(0, 0, 0, 0.05) 50%);
      background-size: 100% 4px;
      pointer-events: none;
      z-index: 2;
    }
  </style>
</head>
<body>
  <div id="container">
    <div id="statusBar">
      <div class="status-row">
        <span class="label">Signal</span>
        <span class="value" id="mode">---</span>
      </div>
      <div class="status-row">
        <span class="label">Res</span>
        <span class="value" id="resolution">0x0</span>
      </div>
      <div class="status-row">
        <span class="label">Rate</span>
        <span class="value"><span id="fps">0</span> FPS</span>
      </div>
      <div class="status-row">
        <span class="label">Ping</span>
        <span class="value" id="latency">0ms</span>
      </div>
      <div class="status-row">
        <span class="label">Loss</span>
        <span class="value" id="errors">0</span>
      </div>
    </div>
    <div id="loading">Connecting to Rover...</div>
    <img id='stream' style="display:none;" />
  </div>
  
  <script>
    // Configuración dinámica basada en la IP de acceso 
    const isAPMode = window.location.hostname === '192.168.4.1';
    const refreshRate = isAPMode ? 500 : 150; 
    
    let fps = 0;
    let errorCount = 0;
    let frameCount = 0;
    let lastSecond = Date.now();
    let framesThisSecond = 0;
    
    const img = document.getElementById('stream');
    const loading = document.getElementById('loading');
    const fpsEl = document.getElementById('fps');
    const latencyEl = document.getElementById('latency');
    const modeEl = document.getElementById('mode');
    const resEl = document.getElementById('resolution');
    const errorsEl = document.getElementById('errors');
    
    modeEl.textContent = isAPMode ? 'AP MODE' : 'STATION';
    modeEl.className = isAPMode ? 'value mode-ap' : 'value mode-sta';
    
    function updateStream() {
      const startTime = Date.now();
      const newSrc = '/capture?t=' + startTime;
      
      const tempImg = new Image();
      
      tempImg.onload = function() {
        // Actualizar imagen y resolución detectada 
        resEl.textContent = `${this.naturalWidth}x${this.naturalHeight}`;
        img.src = this.src;
        img.style.display = 'block';
        loading.style.display = 'none';
        
        const now = Date.now();
        latencyEl.textContent = (now - startTime) + 'ms';
        
        framesThisSecond++;
        if (now - lastSecond >= 1000) {
          fps = framesThisSecond;
          framesThisSecond = 0;
          lastSecond = now;
          fpsEl.textContent = fps;
        }
        frameCount++;
      };
      
      tempImg.onerror = function() {
        errorCount++;
        errorsEl.textContent = errorCount;
        if (errorCount > 15) loading.style.display = 'block';
      };
      
      // Cancelar si tarda demasiado (timeout dinámico)
      setTimeout(() => {
        if (!tempImg.complete) tempImg.src = ''; 
      }, refreshRate * 2);
      
      tempImg.src = newSrc;
    }
    
    // Iniciar el bucle de refresco
    setInterval(updateStream, refreshRate);
    updateStream();
  </script>
</body>
</html>
)rawliteral";

// Prototipos de funciones para el servidor web
void setupWebServer();
void handleInfo();
void handleCapture();
void handleStream();
void handleStreamLow();
void handleTelemetry();
void handleConfig(); 
void handleSave();

#endif