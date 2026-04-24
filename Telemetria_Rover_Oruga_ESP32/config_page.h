#ifndef CONFIG_PAGE_H
#define CONFIG_PAGE_H

const char CONFIG_HEADER[] PROGMEM = R"rawliteral(
<html>
<body style='font-family:sans-serif; background:#1a1a1a; color:white; padding:20px;'>
<h2>Configuracion WiFi</h2>
<h3>Redes disponibles:</h3>
<ul style='list-style:none; padding:0;'>
)rawliteral";

const char CONFIG_FOOTER[] PROGMEM = R"rawliteral(
</ul>

<form action='/save' method='POST'>
SSID:<br>
<input type='text' name='ssid' style='width:100%; padding:10px; border-radius:6px; border:none;'><br><br>

Pass:<br>
<input type='password' name='pass' style='width:100%; padding:10px; border-radius:6px; border:none;'><br><br>

<input type='submit' value='GUARDAR' style='width:100%; padding:15px; background:#e67e22; color:white; border:none; border-radius:6px;'>
</form>

<p style='font-size:12px; opacity:0.6;'>Toca una red para copiar el nombre automaticamente</p>

</body>
</html>
)rawliteral";

#endif