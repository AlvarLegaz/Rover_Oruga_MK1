#ifndef STREAM_PAGE_H
#define STREAM_PAGE_H

#include <Arduino.h>

const char STREAM_ONLY_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Rover HUD Control</title>

  <style>
    *{
      margin:0;
      padding:0;
      box-sizing:border-box;
    }

    body{
      background:#000;
      display:flex;
      justify-content:center;
      align-items:center;
      width:100vw;
      height:100vh;
      overflow:hidden;
      font-family:Segoe UI, Roboto, Helvetica, Arial, sans-serif;
    }

    #container{
      position:relative;
      width:100vw;
      height:100vh;
    }

    #stream{
      width:100%;
      height:100%;
      object-fit:cover;
      display:none;
    }

    #loading{
      position:absolute;
      top:50%;
      left:50%;
      transform:translate(-50%,-50%);
      color:#00ff00;
      font-size:14px;
      letter-spacing:2px;
      z-index:20;
      text-transform:uppercase;
    }

    #statusBar{
      position:absolute;
      top:20px;
      left:20px;
      min-width:220px;
      padding:14px 18px;
      border-radius:12px;
      color:#00ff00;
      z-index:30;

      background:rgba(0,0,0,0.35);
      border:1px solid rgba(0,255,0,0.25);

      backdrop-filter:blur(8px);
      -webkit-backdrop-filter:blur(8px);
    }

    .row{
      display:flex;
      justify-content:space-between;
      margin:6px 0;
      font-size:12px;
      font-weight:600;
      letter-spacing:1px;
      text-transform:uppercase;
    }

    .label{
      color:rgba(0,255,0,0.65);
      margin-right:16px;
    }

    .value{
      color:#00ff00;
      text-shadow:0 0 8px rgba(0,255,0,0.45);
    }

    .mode-ap{
      color:#ff9900;
    }

    .mode-sta{
      color:#00ff00;
    }

    #container::after{
      content:"";
      position:absolute;
      inset:0;
      pointer-events:none;
      background:
        linear-gradient(
          rgba(255,255,255,0.00) 50%,
          rgba(0,0,0,0.05) 50%
        );
      background-size:100% 4px;
      z-index:10;
    }
  </style>
</head>

<body>

<div id="container">

  <div id="statusBar">

    <div class="row">
      <span class="label">Signal</span>
      <span class="value" id="mode">---</span>
    </div>

    <div class="row">
      <span class="label">Res</span>
      <span class="value" id="resolution">0x0</span>
    </div>

    <div class="row">
      <span class="label">Rate</span>
      <span class="value"><span id="fps">0</span> FPS</span>
    </div>

    <div class="row">
      <span class="label">Ping</span>
      <span class="value" id="latency">0ms</span>
    </div>

    <div class="row">
      <span class="label">Loss</span>
      <span class="value" id="errors">0</span>
    </div>

  </div>

  <div id="loading">Connecting to Rover...</div>

  <img id="stream">

</div>

<script>

const isAPMode =
  window.location.hostname === "192.168.4.1";

const refreshRate =
  isAPMode ? 500 : 150;

let fps = 0;
let errors = 0;
let framesThisSecond = 0;
let lastSecond = Date.now();

const img       = document.getElementById("stream");
const loading   = document.getElementById("loading");
const fpsEl     = document.getElementById("fps");
const latencyEl = document.getElementById("latency");
const modeEl    = document.getElementById("mode");
const resEl     = document.getElementById("resolution");
const errEl     = document.getElementById("errors");

modeEl.textContent = isAPMode ? "AP MODE" : "STATION";
modeEl.className   = isAPMode ? "value mode-ap" : "value mode-sta";

function updateFPS(now){
  framesThisSecond++;

  if(now - lastSecond >= 1000){
    fps = framesThisSecond;
    framesThisSecond = 0;
    lastSecond = now;
    fpsEl.textContent = fps;
  }
}

function updateStream(){

  const start = Date.now();
  const src = "/capture?t=" + start;

  const temp = new Image();

  temp.onload = function(){

    const now = Date.now();

    resEl.textContent =
      this.naturalWidth + "x" + this.naturalHeight;

    img.src = this.src;
    img.style.display = "block";
    loading.style.display = "none";

    latencyEl.textContent =
      (now - start) + "ms";

    updateFPS(now);
  };

  temp.onerror = function(){
    errors++;
    errEl.textContent = errors;

    if(errors > 10){
      loading.style.display = "block";
    }
  };

  setTimeout(function(){
    if(!temp.complete){
      temp.src = "";
    }
  }, refreshRate * 2);

  temp.src = src;
}

setInterval(updateStream, refreshRate);
updateStream();

</script>

</body>
</html>
)rawliteral";

#endif