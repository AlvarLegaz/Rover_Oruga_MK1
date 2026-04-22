
# ==========================================================
# VISOR ROVER ORUGA - VERSION ROBUSTA ESP32
# MJPEG ESP32 + TELEMETRIA + MAPA LEAFLET + LUZ
# LISTO PARA COPIAR / PEGAR
#
# pip install requests pillow tkinterweb
# ==========================================================

import tkinter as tk
from tkinter import Frame, Label, Entry, Button
from PIL import Image, ImageTk
from io import BytesIO
from tkinterweb import HtmlFrame

import requests
import threading
import time
import tempfile
import os

# ==========================================================
# CONFIGURACION
# ==========================================================
WINDOW_TITLE = "Visor Rover-Oruga"

STREAM_WIDTH = 640
STREAM_HEIGHT = 400

MAP_WIDTH = 640
MAP_HEIGHT = 300

LEFT_PANEL_WIDTH = 260
RIGHT_PANEL_WIDTH = 220

# tiempos
TELEMETRY_INTERVAL_MS = 500
RECONNECT_DELAY = 2

# ==========================================================
# ENDPOINTS (MODIFICA AQUI)
# ==========================================================
STREAM_ENDPOINT = "/stream"
TELEMETRY_ENDPOINT = "/telemetry"

LIGHT_ON_ENDPOINT = "/light/on"
LIGHT_OFF_ENDPOINT = "/light/off"

# ==========================================================
# APP
# ==========================================================
class Visor:

    def __init__(self, root):
        self.root = root
        self.root.title(WINDOW_TITLE)
        self.root.resizable(False, False)

        self.running = False
        self.stream_thread = None

        self.last_lat = 37.7749
        self.last_lon = -122.4194

        total_height = STREAM_HEIGHT + MAP_HEIGHT

        # ==================================================
        # TOP BAR
        # ==================================================
        top = Frame(root, pady=6)
        top.pack(fill="x")

        Label(top, text="IP Rover:").pack(
            side=tk.LEFT,
            padx=(10, 5)
        )

        self.ip_entry = Entry(top, width=18)
        self.ip_entry.pack(side=tk.LEFT)
        self.ip_entry.insert(0, "192.168.4.1")

        Button(
            top,
            text="Iniciar",
            command=self.start_all
        ).pack(side=tk.LEFT, padx=5)

        Button(
            top,
            text="Parar",
            command=self.stop_all
        ).pack(side=tk.LEFT, padx=5)

        # ==================================================
        # MAIN
        # ==================================================
        main = Frame(root)
        main.pack(padx=10, pady=10)

        # ==================================================
        # PANEL IZQUIERDO TELEMETRIA
        # ==================================================
        left = Frame(
            main,
            width=LEFT_PANEL_WIDTH,
            height=total_height,
            bd=2,
            relief="groove"
        )
        left.pack(side=tk.LEFT, padx=(0, 10))
        left.pack_propagate(False)

        Label(
            left,
            text="TELEMETRIA",
            font=("Arial", 10, "bold")
        ).pack(pady=5)

        self.telemetry_display = Label(
            left,
            text="Esperando datos...",
            justify=tk.LEFT,
            anchor="nw",
            font=("Courier", 10)
        )

        self.telemetry_display.pack(
            fill="both",
            expand=True,
            padx=6,
            pady=6
        )

        # ==================================================
        # CENTRO
        # ==================================================
        center = Frame(main)
        center.pack(side=tk.LEFT)

        # STREAM
        stream_frame = Frame(
            center,
            width=STREAM_WIDTH,
            height=STREAM_HEIGHT,
            bd=2,
            relief="groove"
        )
        stream_frame.pack()
        stream_frame.pack_propagate(False)

        self.image_label = Label(
            stream_frame,
            text="Sin señal"
        )
        self.image_label.pack(fill="both", expand=True)

        # MAPA
        map_frame = Frame(
            center,
            width=MAP_WIDTH,
            height=MAP_HEIGHT,
            bd=2,
            relief="groove"
        )
        map_frame.pack(pady=(10, 0))
        map_frame.pack_propagate(False)

        self.map_widget = HtmlFrame(
            map_frame,
            horizontal_scrollbar="auto"
        )
        self.map_widget.pack(fill="both", expand=True)

        self.load_leaflet()

        # ==================================================
        # PANEL DERECHO
        # ==================================================
        right = Frame(
            main,
            width=RIGHT_PANEL_WIDTH,
            height=total_height,
            bd=2,
            relief="groove"
        )
        right.pack(side=tk.LEFT, padx=(10, 0))
        right.pack_propagate(False)

        Label(
            right,
            text="CONTROL LUZ",
            font=("Arial", 10, "bold")
        ).pack(pady=15)

        Button(
            right,
            text="Encender luz",
            width=18,
            height=2,
            command=self.light_on
        ).pack(pady=10)

        Button(
            right,
            text="Apagar luz",
            width=18,
            height=2,
            command=self.light_off
        ).pack(pady=10)

        self.light_status = Label(
            right,
            text="Estado: Desconocido"
        )
        self.light_status.pack(pady=20)

    # ==================================================
    # START / STOP
    # ==================================================
    def start_all(self):
        if self.running:
            return

        self.running = True

        self.stream_thread = threading.Thread(
            target=self.stream_worker,
            daemon=True
        )
        self.stream_thread.start()

        self.update_telemetry_loop()

    def stop_all(self):
        self.running = False

        self.image_label.configure(
            image="",
            text="Parado"
        )

    # ==================================================
    # STREAM MJPEG ESP32 (THREAD)
    # ==================================================
    def stream_worker(self):

        while self.running:

            ip = self.ip_entry.get().strip()
            url = f"http://{ip}{STREAM_ENDPOINT}"

            try:
                self.root.after(
                    0,
                    lambda: self.image_label.configure(
                        image="",
                        text="Conectando stream..."
                    )
                )

                r = requests.get(
                    url,
                    stream=True,
                    timeout=3
                )

                buffer = b""

                for chunk in r.iter_content(
                    chunk_size=1024
                ):

                    if not self.running:
                        return

                    buffer += chunk

                    a = buffer.find(b'\xff\xd8')
                    b = buffer.find(b'\xff\xd9')

                    if a != -1 and b != -1:

                        jpg = buffer[a:b + 2]
                        buffer = buffer[b + 2:]

                        try:
                            img = Image.open(
                                BytesIO(jpg)
                            )

                            img = img.resize(
                                (
                                    STREAM_WIDTH,
                                    STREAM_HEIGHT
                                )
                            )

                            self.root.after(
                                0,
                                self.show_stream_image,
                                img
                            )

                        except:
                            pass

            except:
                self.root.after(
                    0,
                    lambda: self.image_label.configure(
                        image="",
                        text="Esperando ESP32..."
                    )
                )

                time.sleep(RECONNECT_DELAY)

    def show_stream_image(self, img):
        imgtk = ImageTk.PhotoImage(img)

        self.image_label.imgtk = imgtk

        self.image_label.configure(
            image=imgtk,
            text=""
        )

    # ==================================================
    # TELEMETRIA
    # ==================================================
    def update_telemetry_loop(self):

        if not self.running:
            return

        ip = self.ip_entry.get().strip()
        url = f"http://{ip}{TELEMETRY_ENDPOINT}"

        try:
            r = requests.get(url, timeout=1)
            r.raise_for_status()

            data = r.json()

            txt = ""

            for k, v in data.items():
                txt += f"{k}: {v}\n"

            self.telemetry_display.configure(
                text=txt
            )

            lat = data.get("lat")
            lon = data.get("lon")

            if lat is not None and lon is not None:
                self.last_lat = float(lat)
                self.last_lon = float(lon)

                self.update_leaflet(
                    self.last_lat,
                    self.last_lon
                )

        except:
            self.telemetry_display.configure(
                text="Telemetria offline"
            )

        self.root.after(
            TELEMETRY_INTERVAL_MS,
            self.update_telemetry_loop
        )

    # ==================================================
    # LEAFLET
    # ==================================================
    def load_leaflet(self):

        html = f"""
        <!DOCTYPE html>
        <html>
        <head>
        <meta charset="utf-8"/>
        <link rel="stylesheet"
        href="https://unpkg.com/leaflet/dist/leaflet.css"/>
        <script src="https://unpkg.com/leaflet/dist/leaflet.js"></script>

        <style>
        html,body,#map {{
            margin:0;
            padding:0;
            width:100%;
            height:100%;
        }}
        </style>
        </head>

        <body>
        <div id="map"></div>

        <script>

        var map = L.map('map').setView(
            [{self.last_lat},{self.last_lon}],18
        );

        L.tileLayer(
          'https://tile.openstreetmap.org/{{z}}/{{x}}/{{y}}.png',
          {{maxZoom:19}}
        ).addTo(map);

        var marker = L.marker(
            [{self.last_lat},{self.last_lon}]
        ).addTo(map);

        window.moveMarker = function(lat,lon){{
            marker.setLatLng([lat,lon]);
            map.setView([lat,lon]);
        }}

        </script>
        </body>
        </html>
        """

        tmp = tempfile.NamedTemporaryFile(
            delete=False,
            suffix=".html"
        )

        tmp.write(html.encode("utf-8"))
        tmp.close()

        self.map_file = tmp.name

        self.map_widget.load_file(
            self.map_file
        )

    def update_leaflet(self, lat, lon):
        try:
            self.map_widget.html.runjs(
                f"moveMarker({lat},{lon});"
            )
        except:
            pass

    # ==================================================
    # LUZ
    # ==================================================
    def light_on(self):
        ip = self.ip_entry.get().strip()
        url = f"http://{ip}{LIGHT_ON_ENDPOINT}"

        try:
            requests.get(url, timeout=1)

            self.light_status.configure(
                text="Estado: Luz encendida"
            )

        except:
            self.light_status.configure(
                text="Error al encender"
            )

    def light_off(self):
        ip = self.ip_entry.get().strip()
        url = f"http://{ip}{LIGHT_OFF_ENDPOINT}"

        try:
            requests.get(url, timeout=1)

            self.light_status.configure(
                text="Estado: Luz apagada"
            )

        except:
            self.light_status.configure(
                text="Error al apagar"
            )

    # ==================================================
    # CERRAR
    # ==================================================
    def on_close(self):

        self.running = False

        try:
            os.remove(self.map_file)
        except:
            pass

        self.root.destroy()


# ==========================================================
# MAIN
# ==========================================================
if __name__ == "__main__":

    root = tk.Tk()

    app = Visor(root)

    root.protocol(
        "WM_DELETE_WINDOW",
        app.on_close
    )

    root.mainloop()
