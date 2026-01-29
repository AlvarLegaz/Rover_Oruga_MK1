#ifndef WEB_SERVER_ROVER_H
#define WEB_SERVER_ROVER_H

#include <WebServer.h>
#include <WiFi.h>

// Esto le dice a todos los archivos: "Hay una variable booleana llamada cameraSupported en otro lado"
extern bool cameraSupported; 
extern WebServer server;
extern const char* ap_ssid;
extern const char* ap_password;

// ========================================
// SOLUCIÓN 1: HTML CON AJUSTE AUTOMÁTICO DE FPS
// ========================================
const char STREAM_ONLY_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Rover Stream</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    
    body { 
      background: #000; 
      display: flex; 
      justify-content: center; 
      align-items: center; 
      height: 100vh; 
      overflow: hidden;
      font-family: 'Courier New', monospace;
    }
    
    #container {
      position: relative;
      max-width: 100%;
      max-height: 100%;
    }
    
    img { 
      max-width: 100%; 
      max-height: 100vh; 
      object-fit: contain; 
      border: 2px solid #00ff00;
      box-shadow: 0 0 20px rgba(0, 255, 0, 0.3);
    }
    
    #statusBar { 
      position: absolute; 
      top: 10px; 
      left: 10px; 
      background: rgba(0, 0, 0, 0.8);
      padding: 10px 15px;
      border-radius: 5px;
      border: 1px solid #00ff00;
      color: #0f0; 
      font-size: 14px;
      z-index: 10;
      min-width: 200px;
    }
    
    .status-row {
      display: flex;
      justify-content: space-between;
      margin: 3px 0;
    }
    
    .label { color: #0f0; opacity: 0.7; }
    .value { color: #0f0; font-weight: bold; }
    
    .mode-ap { color: #ff9900; }
    .mode-sta { color: #00ff00; }
    
    #loading {
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      color: #0f0;
      font-size: 20px;
      animation: pulse 1.5s infinite;
    }
    
    @keyframes pulse {
      0%, 100% { opacity: 1; }
      50% { opacity: 0.3; }
    }
  </style>
</head>
<body>
  <div id="container">
    <div id="statusBar">
      <div class="status-row">
        <span class="label">FPS:</span>
        <span class="value" id="fps">0</span>
      </div>
      <div class="status-row">
        <span class="label">Latency:</span>
        <span class="value" id="latency">0ms</span>
      </div>
      <div class="status-row">
        <span class="label">Mode:</span>
        <span class="value" id="mode">---</span>
      </div>
      <div class="status-row">
        <span class="label">Errors:</span>
        <span class="value" id="errors">0</span>
      </div>
    </div>
    <div id="loading">⏳ Cargando stream...</div>
    <img id='stream' style="display:none;" />
  </div>
  
  <script>
    // ========================================
    // SOLUCIÓN 1: DETECCIÓN AUTOMÁTICA DE MODO
    // ========================================
    
    const isAPMode = window.location.hostname === '192.168.4.1';
    const refreshRate = isAPMode ? 500 : 150; // AP: 2 FPS | STA: 6.6 FPS
    
    console.log(`🚀 Modo detectado: ${isAPMode ? 'AP' : 'STATION'}`);
    console.log(`📊 Refresh rate: ${refreshRate}ms (${Math.round(1000/refreshRate)} FPS teórico)`);
    
    // Variables de estado
    let lastUpdate = 0;
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
    const errorsEl = document.getElementById('errors');
    
    // Configurar modo visual
    modeEl.textContent = isAPMode ? 'AP' : 'STATION';
    modeEl.className = isAPMode ? 'value mode-ap' : 'value mode-sta';
    
    // Función para actualizar el stream
    function updateStream() {
      const startTime = Date.now();
      const newSrc = '/capture?t=' + startTime;
      
      // Crear nueva imagen temporal para precargar
      const tempImg = new Image();
      
      tempImg.onload = function() {
        // Frame cargado exitosamente
        img.src = this.src;
        img.style.display = 'block';
        loading.style.display = 'none';
        
        const now = Date.now();
        const latency = now - startTime;
        
        // Actualizar latencia
        latencyEl.textContent = latency + 'ms';
        
        // Calcular FPS real
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
        // Error al cargar frame
        errorCount++;
        errorsEl.textContent = errorCount;
        console.warn(`❌ Error cargando frame #${frameCount}`);
        
        // Si hay muchos errores, mostrar mensaje
        if (errorCount > 10) {
          loading.textContent = '⚠️ Problemas de conexión';
          loading.style.display = 'block';
        }
      };
      
      // Timeout de seguridad (doble del refresh rate)
      setTimeout(() => {
        if (!tempImg.complete) {
          console.warn('⏱️ Timeout cargando frame');
          tempImg.src = ''; // Cancelar carga
        }
      }, refreshRate * 2);
      
      tempImg.src = newSrc;
    }
    
    // Iniciar stream
    console.log('🎬 Iniciando stream...');
    setInterval(updateStream, refreshRate);
    
    // Primera carga inmediata
    updateStream();
    
    // Log de estadísticas cada 5 segundos
    setInterval(() => {
      console.log(`📊 Stats: ${fps} FPS | ${errorCount} errores | ${frameCount} frames totales`);
    }, 5000);
  </script>
</body>
</html>
)rawliteral";

void setupWebServer();
void handleCapture();
void handleTelemetry();
void handleHealth();
void handleConfig(); 
void handleSave();

#endif
