#ifndef INFO_PAGE_H
#define INFO_PAGE_H

const char INFO_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>ESP32 Camera Server</title>
<style>
body{
  font-family: monospace;
  background:#0d0d0d;
  color:#e0e0e0;
  padding:20px;
  line-height:1.6;
  font-size:16px;
}

h1{
  color:#ffffff;
  border-bottom:1px solid #333;
  padding-bottom:10px;
  font-size:22px;
}

p{
  max-width:700px;
}

ul{
  margin-top:20px;
  padding-left:20px;
}

li{
  margin:8px 0;
}

a{
  color:#58a6ff;
  text-decoration:none;
}

a:hover{
  text-decoration:underline;
}

.warn{
  margin-top:20px;
  padding:12px;
  border-left:4px solid #888;
  background:#111;
}

strong{
  color:#fff;
}

footer{
  margin-top:40px;
  font-size:0.85rem;
  opacity:0.6;
  border-top:1px solid #333;
  padding-top:10px;
}
</style>
</head>

<body>

<h1>Servidor Video y Telemetría ESP32</h1>

<p>
Este dispositivo expone un conjunto pequeño de endpoints HTTP.
Hace dos cosa. 1) Captura imagenes y las envia por red. 2) Toma datos de telemetría de un reducido número de sensores.
El resto es soporte. Si algo falla lo normal es que sea la red o el cliente.
</p>

<p>
Usa el stream HTTP para video en tiempo real cuando quieras verlo desde navegador.
Usa el stream UDP cuando priorices FPS y baja latencia desde un cliente propio.
El cliente UDP debe enviar /udp/ping periodicamente para indicar que sigue vivo.
Si /udp/start recibe telemetry_port, tambien envia telemetria JSON por UDP a ese puerto.
Usa /telemetry por HTTP para depuracion y como respaldo.
No esperes multiples clientes ni comportamiento de servidor completo.
Esto es un sistema embebido.
</p>

<div class="warn">
<p>
El nivel de seguridad es prácticamente nulo: no hay HTTPS, ni autenticación, ni cifrado.
Básicamente, se está confiando en que nadie mire… lo que en la práctica significa que el sistema está completamente roto en términos de seguridad.
</p>
</div>

<ul>
<li><a href='/'>/</a> Pagina principal</li>
<li><a href='/config'>/config</a> Configuracion de red WiFi</li>
<li><a href='/save'>/save</a> Guardar configuracion WiFi</li>
<li><a href='/info'>/info</a> Esta pagina</li>

<li><a href='/telemetry'>/telemetry</a> Telemetria completa JSON por HTTP (system + gps + imu)</li>
<li><a href='/system'>/system</a> Estado general JSON (bateria + temperatura)</li>
<li><a href='/gps'>/gps</a> Datos GPS JSON</li>
<li><a href='/imu'>/imu</a> Datos inerciales JSON (MPU9250 + BMP280)</li>

<li><a href='/luces/on'>/luces/on</a> Encender flash LED</li>
<li><a href='/luces/off'>/luces/off</a> Apagar flash LED</li>

<li><a href='/capture'>/capture</a> Captura unica en formato JPEG</li>
<li><a href='/stream'>/stream</a> Stream MJPEG alta calidad</li>
<li><a href='/stream_low'>/stream_low</a> Stream MJPEG baja resolucion y altos FPS para tiempo real</li>

<li><a href='/udp/start?port=5000&amp;mode=low&amp;telemetry_port=5001'>/udp/start?port=5000&mode=low&telemetry_port=5001</a> Iniciar video UDP LOW y telemetria UDP hacia la IP del cliente</li>
<li><a href='/udp/start?port=5000&amp;mode=high&amp;telemetry_port=5001'>/udp/start?port=5000&mode=high&telemetry_port=5001</a> Iniciar video UDP HIGH y telemetria UDP hacia la IP del cliente</li>
<li><a href='/udp/stop'>/udp/stop</a> Detener stream UDP activo</li>
<li><a href='/udp/status'>/udp/status</a> Estado del stream UDP, incluyendo telemetria_udp_active, telemetry_port y last_ping_age_ms</li>
<li><a href='/udp/ping'>/udp/ping</a> Keepalive del cliente UDP. Mantiene vivo video y telemetria; solo se acepta desde la IP que lo inicio</li>
</ul>

<footer style={{ marginTop: "2rem", fontSize: "0.9rem", opacity: 0.7 }}>
  Desarrollado por Álvar Legaz. CIFP Politécnico Cartagena.
</footer>

</body>
</html>
)rawliteral";

#endif