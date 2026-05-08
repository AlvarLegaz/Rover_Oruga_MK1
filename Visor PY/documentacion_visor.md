# Documentación del visor del rover

## 1. Qué hace `visor.py`

`visor.py` es el cliente de control y visualización del rover.

Su trabajo principal es juntar en una sola interfaz:

- vídeo de la cámara;
- telemetría del sistema;
- mapa con posición GPS;
- brújula;
- horizonte artificial;
- control básico, como encender y apagar luces;
- modo táctico a pantalla completa.

La interfaz está hecha con `tkinter`. No es una aplicación web. No pretende ser bonita. Pretende funcionar, mostrar datos y no estorbar.

El visor se conecta al ESP32 usando la IP configurada en la barra superior. Por defecto suele ser:

```text
192.168.4.1
```

Hay dos selectores importantes:

```text
Stream:      LOW / estable | HIGH / calidad
Transporte:  HTTP | UDP
```

No son lo mismo.

```text
LOW/HIGH = calidad o modo de cámara
HTTP/UDP = transporte de datos
```

Con la versión actual:

```text
Transporte HTTP = vídeo por HTTP + telemetría por HTTP
Transporte UDP  = vídeo por UDP  + telemetría por UDP
```

El endpoint HTTP `/telemetry` se mantiene siempre como respaldo y para depuración.

---

## 2. Modo HTTP

En modo HTTP, el visor usa los endpoints clásicos del ESP32.

Para vídeo:

```text
/stream_low
/stream
```

Normalmente:

```text
/stream_low = LOW / estable
/stream     = HIGH / calidad
```

Para telemetría:

```text
/telemetry
```

El vídeo HTTP llega como MJPEG. El cliente recibe bytes, busca el inicio y final de cada JPEG:

```text
FFD8 = inicio JPEG
FFD9 = final JPEG
```

Cuando encuentra un JPEG completo, lo decodifica con Pillow y lo pinta en pantalla.

La telemetría HTTP se consulta periódicamente. El visor pide `/telemetry`, recibe JSON y actualiza los paneles.

### Ventajas de HTTP

- Fácil de probar desde navegador.
- Simple de depurar.
- No requiere protocolo propio para la telemetría.
- Si llega un frame MJPEG por TCP, llega entero.
- Sirve como modo de respaldo si UDP falla.

### Problemas de HTTP

- Usa TCP.
- TCP retransmite paquetes perdidos.
- Para vídeo en tiempo real eso puede ser un problema.
- Si la red va mal, TCP intenta ser “correcto” en vez de ser “rápido”.
- Un cliente lento puede bloquear o degradar el stream.
- Hacer polling HTTP de telemetría mete más tráfico de control y más trabajo al servidor web.

Para un rover, ver algo tarde puede ser peor que perder un frame.

---

## 3. Modo UDP

En modo UDP, el selector `Transporte` cambia a:

```text
UDP
```

Eso significa:

```text
vídeo por UDP
telemetría por UDP
keepalive por HTTP
```

El visor primero llama a un endpoint HTTP de control:

```text
/udp/start?port=5000&mode=low&telemetry_port=5001
/udp/start?port=5000&mode=high&telemetry_port=5001
```

El ESP32 obtiene la IP del cliente desde la petición HTTP y empieza a mandar datos UDP a esa IP.

Puertos usados:

```text
5000 -> vídeo UDP
5001 -> telemetría UDP
```

Cuando se detiene:

```text
/udp/stop
```

Para consultar estado:

```text
/udp/status
```

Para mantener viva la sesión:

```text
/udp/ping
```

---

## 4. Por qué el control sigue usando HTTP

Aunque vídeo y telemetría vayan por UDP, el control de sesión sigue usando HTTP.

Endpoints de control:

```text
/udp/start
/udp/stop
/udp/status
/udp/ping
```

Eso está bien.

UDP no tiene conexión. No hay “cliente conectado” en sentido TCP. El ESP32 no sabe por sí solo si el visor se cerró, si el portátil perdió WiFi o si el programa murió.

Por eso existe `/udp/ping`.

El visor manda:

```text
/udp/ping
```

cada segundo mientras el transporte UDP está activo.

El ESP32 guarda el último momento en que vio al cliente:

```text
lastUdpClientSeenMs = millis()
```

Si pasan varios segundos sin ping, corta el envío UDP automáticamente.

Configuración recomendada:

```text
ping cada 1 segundo
timeout en ESP32: 3000 ms
```

Esto evita que el ESP32 se quede enviando vídeo y telemetría a la nada.

---

## 5. Por qué UDP no manda un JPEG entero por paquete

Un frame JPEG puede ocupar bastante más que un paquete UDP seguro.

Mandar un JPEG completo en un único datagrama UDP es mala idea.

Si el datagrama es demasiado grande, se fragmenta a nivel IP. Si se pierde un fragmento, se pierde todo. Además, en WiFi con ESP32, los paquetes grandes son una receta bastante buena para tener comportamiento irregular.

Por eso el ESP32 divide cada JPEG en fragmentos.

Cada paquete UDP de vídeo lleva una cabecera:

```text
magic
frameId
packetIndex
packetCount
payloadSize
payload
```

Conceptualmente:

```text
frameId      = identificador del frame JPEG
packetIndex  = índice del fragmento actual
packetCount  = total de fragmentos del frame
payloadSize  = tamaño útil del fragmento
payload      = datos JPEG
```

Ejemplo:

```text
Frame 120:
  paquete 0/5
  paquete 1/5
  paquete 2/5
  paquete 3/5
  paquete 4/5
```

El visor reconstruye el JPEG cuando tiene todos los paquetes.

---

## 6. Estrategia correcta para vídeo UDP

La regla principal es simple:

```text
No esperes paquetes perdidos.
```

Esto no es transferencia de archivos. No estamos descargando un PDF. Estamos conduciendo un rover.

Si falta un paquete, ese frame está roto. Se descarta.

La estrategia del visor debe ser:

```text
1. Recibir paquetes UDP.
2. Agruparlos por frameId.
3. Reconstruir solo frames completos.
4. Mostrar solo frames completos.
5. Descartar frames incompletos.
6. Si llega un frame más nuevo, priorizar el nuevo.
7. No bloquear esperando paquetes viejos.
```

Esto es deliberado.

Un frame viejo completo no vale más que un frame nuevo incompleto durante mucho tiempo. El vídeo en tiempo real necesita baja latencia, no perfección histórica.

---

## 7. Qué hacer cuando se pierde un paquete de vídeo

Ejemplo:

```text
Frame 200:
  llega paquete 0
  llega paquete 1
  llega paquete 2
  falta paquete 3
  llega paquete 4
```

Ese frame no se puede reconstruir.

La decisión correcta:

```text
descartar Frame 200
```

La decisión incorrecta:

```text
esperar el paquete 3 eternamente
```

Esperar paquetes perdidos congela el vídeo. Eso no es robustez. Eso es convertir un sistema de tiempo real en una cola de espera.

---

## 8. Qué hacer cuando llega un frame nuevo

Ejemplo:

```text
Frame 300 incompleto
llega Frame 301
```

La estrategia recomendada:

```text
mantener como máximo 1 o 2 frames pendientes
```

Para un rover, lo razonable es ser agresivo:

```text
si llega un frame nuevo, borra frames viejos incompletos
```

Así la pantalla enseña lo más reciente posible.

---

## 9. Buffer de frames de vídeo

El visor no debe acumular frames sin límite.

Eso sería una forma lenta de romper el programa.

Configuración recomendada:

```text
MAX_PENDING_FRAMES = 2
UDP_FRAME_TIMEOUT = 0.15 segundos
```

Es decir:

- como máximo dos frames en reconstrucción;
- si un frame lleva más de 150 ms incompleto, se borra;
- si se llena el buffer, se eliminan frames antiguos.

Esto mantiene la latencia baja y evita consumo de memoria absurdo.

---

## 10. Comparación de `frameId`

El `frameId` del ESP32 suele ser un `uint16_t`.

Eso significa:

```text
0 ... 65535 ... 0 ... 65535
```

Hay desbordamiento.

Para una primera versión, se puede usar lógica simple. Para una versión más fina, hay que comparar IDs de forma circular.

Una comparación circular típica es:

```python
def is_newer_frame(a, b):
    return ((a - b) & 0xFFFF) < 0x8000
```

Esto evita tratar el frame `0` como viejo justo después del `65535`.

No es un detalle cosmético. Es uno de esos errores pequeños que aparecen cuando todo parece funcionar y luego falla “sin motivo” tras un rato.

---

## 11. Pseudocódigo del receptor de vídeo UDP

La idea limpia es esta:

```python
pending_frames = {}
last_displayed_frame_id = None

MAX_PENDING_FRAMES = 2
UDP_FRAME_TIMEOUT = 0.15

def handle_udp_packet(frame_id, packet_index, packet_count, payload):
    now = time.time()

    # 1. Borrar frames caducados.
    for fid, frame in list(pending_frames.items()):
        if now - frame["created_at"] > UDP_FRAME_TIMEOUT:
            del pending_frames[fid]

    # 2. Ignorar paquetes de frames ya mostrados.
    if last_displayed_frame_id is not None:
        if frame_id == last_displayed_frame_id:
            return

    # 3. Crear entrada del frame si no existe.
    if frame_id not in pending_frames:
        pending_frames[frame_id] = {
            "created_at": now,
            "packet_count": packet_count,
            "chunks": {}
        }

    frame = pending_frames[frame_id]

    # 4. Validar coherencia.
    if frame["packet_count"] != packet_count:
        del pending_frames[frame_id]
        return

    if packet_index >= packet_count:
        return

    # 5. Guardar fragmento.
    frame["chunks"][packet_index] = payload

    # 6. Si el frame está completo, reconstruir.
    if len(frame["chunks"]) == frame["packet_count"]:
        jpg = b"".join(
            frame["chunks"][i]
            for i in range(frame["packet_count"])
        )

        del pending_frames[frame_id]
        last_displayed_frame_id = frame_id

        show_jpeg(jpg)

    # 7. Limitar frames pendientes.
    while len(pending_frames) > MAX_PENDING_FRAMES:
        oldest = min(
            pending_frames,
            key=lambda fid: pending_frames[fid]["created_at"]
        )
        del pending_frames[oldest]
```

Ese es el comportamiento correcto: recibe, reconstruye, pinta y tira basura.

Nada de romanticismo con paquetes perdidos.

---

## 12. Telemetría UDP

La telemetría UDP va separada del vídeo.

```text
vídeo UDP      -> puerto 5000
telemetría UDP -> puerto 5001
```

No se mezclan en el mismo puerto. Eso evita tener que meter tipos de paquete y simplifica el cliente.

La telemetría se manda como JSON.

Ejemplo conceptual:

```json
{
  "seq": 1523,
  "uptime_ms": 123456,
  "uptime_s": 123,
  "uptime": "0d 00:02:03",
  "gps": {
    "lat": 37.59643,
    "lon": -0.97689,
    "course": 123.4
  },
  "imu": {
    "pitch": 1.2,
    "roll": -3.4
  },
  "bat": 12.1
}
```

Para telemetría, perder un paquete no importa demasiado. Si se pierde una lectura de IMU o GPS, llega otra después.

La regla es la misma que con el vídeo:

```text
usar lo más reciente
descartar lo viejo
no esperar retransmisiones
```

---

## 13. Estrategia correcta para telemetría UDP

La telemetría UDP debe tratarse como estado, no como historial.

Si llega un paquete nuevo:

```text
se parsea el JSON
se actualiza la interfaz
se reemplaza el estado anterior
```

Si se pierde un paquete:

```text
no se hace nada
```

Si no llega telemetría durante un tiempo:

```text
mostrar Telemetría offline
```

No hay que acumular paquetes de telemetría. No hay que ordenar una cola enorme. No hay que reproducir telemetría antigua.

Para un visor de conducción, el último dato válido es el que importa.

---

## 14. Frecuencia recomendada de telemetría UDP

Punto de partida razonable:

```text
telemetría UDP: 5 Hz
```

Eso actualiza IMU, brújula, GPS y sistema con suficiente fluidez para el visor sin saturar al ESP32.

El HTTP `/telemetry` puede quedarse a 1 Hz como respaldo.

Ejemplo de estrategia:

```text
Transporte HTTP:
  /telemetry cada 1000 ms

Transporte UDP:
  escuchar puerto 5001
  no hacer polling HTTP continuo
```

---

## 15. Qué NO debe hacer el receptor UDP

No debe hacer esto:

```text
esperar a que llegue un paquete perdido
```

No debe hacer esto:

```text
guardar 20 frames pendientes
```

No debe hacer esto:

```text
pintar JPEGs incompletos
```

No debe hacer esto:

```text
bloquear el hilo de interfaz esperando red
```

No debe hacer esto:

```text
usar UDP como si fuera TCP mal implementado
```

No debe hacer esto:

```text
acumular telemetría vieja
```

Si se necesita fiabilidad total, se usa TCP. Si se usa UDP, se acepta que se pierden cosas y se diseña alrededor de eso.

---

## 16. Hilos de recepción

Los receptores UDP deben ejecutarse en hilos separados.

Tkinter no debe bloquearse esperando paquetes de red.

Estructura correcta:

```text
hilo vídeo UDP:
  recibe paquetes
  reconstruye JPEG
  manda imagen al hilo principal con root.after()

hilo telemetría UDP:
  recibe JSON
  parsea datos
  manda actualización al hilo principal con root.after()

hilo keepalive:
  manda /udp/ping cada 1 segundo

hilo principal Tkinter:
  pinta imagen
  actualiza interfaz
```

Tkinter no es thread-safe. Pintar directamente desde un hilo de red es pedir problemas.

La forma correcta:

```python
self.root.after(0, self.show_stream_image, img)
```

Para telemetría:

```python
self.root.after(0, self.apply_telemetry_data, data)
```

---

## 17. Al cambiar entre HTTP y UDP

Cuando se cambia de transporte:

```text
HTTP -> UDP
UDP  -> HTTP
```

hay que cerrar lo anterior antes de abrir lo nuevo.

Orden correcto:

```text
1. Marcar running = False.
2. Cerrar sesión HTTP o sockets UDP.
3. Detener hilo de vídeo.
4. Detener hilo de telemetría.
5. Detener hilo de ping.
6. Limpiar buffers.
7. Reiniciar el nuevo transporte.
```

Orden incorrecto:

```text
abrir UDP mientras HTTP sigue cerrando
```

o:

```text
abrir HTTP mientras UDP sigue mandando
```

Eso genera estados intermedios raros, endpoints ocupados y pantallas negras.

---

## 18. Al cambiar entre HIGH y LOW

Cambiar entre `HIGH` y `LOW` no es solo cambiar una URL.

En el ESP32 implica tocar configuración de cámara.

Por eso el visor debe:

```text
1. parar stream actual;
2. esperar un poco;
3. arrancar stream nuevo.
```

Si no se espera, el ESP32 puede seguir teniendo la cámara ocupada.

Síntoma típico:

```text
pantalla negra
503 Stream ocupado
reconexión lenta
```

La solución es no correr. Cerrar bien antes de abrir otra cosa.

---

## 19. Relación entre UDP, HTTP y endpoints de control

Aunque el transporte UDP esté activo, siguen existiendo endpoints HTTP de control.

Ejemplos:

```text
/udp/start
/udp/stop
/udp/status
/udp/ping
/telemetry
/luces/on
/luces/off
```

Esto está bien.

Lo que no está bien es saturar el ESP32 con UDP hasta que no pueda atender HTTP.

Por eso el firmware debe limitar FPS UDP.

Ejemplo:

```cpp
const int targetFPS_high = 10;
const int targetFPS_low  = 8;
```

Y calcular el intervalo:

```cpp
1000 / targetFPS
```

Más FPS no siempre es mejor. Si el rover se vuelve menos controlable, has ganado números y perdido sistema.

---

## 20. Qué significa “FPS bueno”

Hay varios FPS posibles:

```text
FPS capturados
FPS enviados
FPS recibidos
FPS pintados
```

No son lo mismo.

El visor debe mostrar FPS recibidos o pintados, pero hay que saber qué se está midiendo.

Para conducción, lo importante es:

```text
latencia baja
imagen reciente
conexión estable
telemetría actual
```

No sirve presumir de 25 FPS si van con medio segundo de retraso.

---

## 21. Recomendación práctica

Para empezar:

```text
UDP LOW:
  8 FPS

UDP HIGH:
  10 FPS

UDP payload:
  800-1000 bytes

MAX_PENDING_FRAMES:
  2

UDP_FRAME_TIMEOUT:
  0.15 s

Telemetría UDP:
  5 Hz

Ping:
  cada 1 s

Timeout cliente UDP:
  3 s
```

Luego se prueba.

No se adivina.

Se prueba en el entorno real: misma distancia, mismo WiFi, mismo rover, misma cámara, misma alimentación.

---

## 22. Resumen operativo

Usa `Transporte HTTP` cuando quieras:

```text
compatibilidad
depuración
ver desde navegador
simplicidad
```

En HTTP:

```text
vídeo      -> /stream_low o /stream
telemetría -> /telemetry
```

Usa `Transporte UDP` cuando quieras:

```text
menor latencia
menos bloqueo por retransmisiones
vídeo más adecuado para conducción
telemetría más fluida
```

En UDP:

```text
vídeo      -> UDP 5000
telemetría -> UDP 5001
control    -> /udp/start, /udp/stop, /udp/status, /udp/ping
```

Pero si usas UDP, úsalo bien:

```text
frames completos o nada
descartar lo viejo
no esperar pérdidas
buffer pequeño
timeouts cortos
UI nunca bloqueada
ping para saber si el cliente sigue vivo
telemetría como estado actual, no como historial
```

Eso es todo.

El resto son adornos.
