# ==========================================================
# visor.py
# VISOR ROVER - MAPA ONLINE + GRID TACTICO OFFLINE AUTO
#
# REQUISITOS:
# pip install requests pillow tkintermapview
# ==========================================================

import tkinter as tk
import tkintermapview as tkm
from tkinter import Frame, Label, Entry, Button
from PIL import Image, ImageTk
from io import BytesIO

import requests
import threading
import time
import math

from horizonte_widget import HorizonteWidget

# ==========================================================
# CONFIG
# ==========================================================

WINDOW_TITLE = "VISOR ROVER"

STREAM_WIDTH = 640
STREAM_HEIGHT = 400

MAP_WIDTH = 640
MAP_HEIGHT = 280

LEFT_PANEL_WIDTH = 280
RIGHT_PANEL_WIDTH = 240

TELEMETRY_INTERVAL_MS = 500
RECONNECT_DELAY = 2

STREAM_ENDPOINT = "/stream"
TELEMETRY_ENDPOINT = "/telemetry"

LIGHT_ON_ENDPOINT = "/luces/on"
LIGHT_OFF_ENDPOINT = "/luces/off"

# ==========================================================
# COLORS
# ==========================================================

BG = "#10140d"
PANEL = "#1c2218"
TEXT = "#d8e2cf"
ACCENT = "#7aff5c"

# ==========================================================
# APP
# ==========================================================

class Visor:

    def __init__(self, root):

        self.root = root
        self.root.title(WINDOW_TITLE)
        self.root.configure(bg=BG)
        self.root.resizable(False, False)

        self.running = False
        self.stream_thread = None
        self.stream_session = requests.Session()

        self.last_frame_time = 0
        self.max_fps = 15

        self.last_lat = 37.59643052360859
        self.last_lon = -0.976892145443811
        self.last_course = 0

        self.marker = None
        self.map_online = True

        self.tactical_window = None
        self.tactical_label = None
        self.tactical_hud = None
        self.crosshair = None

        total_height = STREAM_HEIGHT + MAP_HEIGHT

        # ==================================================
        # TOP BAR
        # ==================================================

        top = Frame(root, bg=BG, pady=8)
        top.pack(fill="x")

        Label(top, text="IP Rover:", bg=BG, fg=TEXT).pack(
            side=tk.LEFT, padx=(10, 5)
        )

        self.ip_entry = Entry(top, width=18)
        self.ip_entry.pack(side=tk.LEFT)
        self.ip_entry.insert(0, "192.168.4.1")

        Button(
            top,
            text="Iniciar",
            width=10,
            bg=PANEL,
            fg=TEXT,
            command=self.start_all
        ).pack(side=tk.LEFT, padx=5)

        Button(
            top,
            text="Parar",
            width=10,
            bg=PANEL,
            fg=TEXT,
            command=self.stop_all
        ).pack(side=tk.LEFT, padx=5)

        # ==================================================
        # MAIN
        # ==================================================

        main = Frame(root, bg=BG)
        main.pack(padx=10, pady=10)

        # ==================================================
        # LEFT PANEL
        # ==================================================

        left = Frame(
            main,
            width=LEFT_PANEL_WIDTH,
            height=total_height,
            bg=PANEL,
            bd=2,
            relief="groove"
        )
        left.pack(side=tk.LEFT, padx=(0, 10))
        left.pack_propagate(False)

        Label(
            left,
            text="TELEMETRIA",
            bg=PANEL,
            fg=ACCENT,
            font=("Arial", 11, "bold")
        ).pack(pady=6)

        self.telemetry_display = Label(
            left,
            text="Esperando datos...",
            justify=tk.LEFT,
            anchor="nw",
            bg=PANEL,
            fg=TEXT,
            font=("Courier", 10)
        )

        self.telemetry_display.pack(
            fill="both",
            expand=True,
            padx=8,
            pady=8
        )

        # ==================================================
        # CENTER
        # ==================================================

        center = Frame(main, bg=BG)
        center.pack(side=tk.LEFT)

        # VIDEO
        stream_frame = Frame(
            center,
            width=STREAM_WIDTH,
            height=STREAM_HEIGHT,
            bg="black",
            bd=2,
            relief="groove"
        )
        stream_frame.pack()
        stream_frame.pack_propagate(False)

        self.image_label = Label(
            stream_frame,
            text="Sin señal",
            bg="black",
            fg=TEXT
        )
        self.image_label.pack(fill="both", expand=True)

        self.image_label.bind(
            "<Double-Button-1>",
            self.open_tactical_mode
        )

        # MAP PANEL
        map_frame = Frame(
            center,
            width=MAP_WIDTH,
            height=MAP_HEIGHT,
            bd=2,
            relief="groove"
        )
        map_frame.pack(pady=(10, 0))
        map_frame.pack_propagate(False)

        # ONLINE MAP
        self.map = tkm.TkinterMapView(
            map_frame,
            width=MAP_WIDTH,
            height=MAP_HEIGHT
        )
        self.map.pack(fill="both", expand=True)

        self.map.set_tile_server(
            "https://a.tile.openstreetmap.org/{z}/{x}/{y}.png"
        )

        self.map.set_position(
            self.last_lat,
            self.last_lon
        )

        self.map.set_zoom(16)

        self.marker = self.map.set_marker(
            self.last_lat,
            self.last_lon,
            text="ROVER"
        )

        # OFFLINE GRID
        self.grid = tk.Canvas(
            map_frame,
            width=MAP_WIDTH,
            height=MAP_HEIGHT,
            bg="black",
            highlightthickness=0
        )

        # ==================================================
        # RIGHT PANEL
        # ==================================================

        right = Frame(
            main,
            width=RIGHT_PANEL_WIDTH,
            height=total_height,
            bg=PANEL,
            bd=2,
            relief="groove"
        )
        right.pack(side=tk.LEFT, padx=(10, 0))
        right.pack_propagate(False)

        Label(
            right,
            text="CONTROL",
            bg=PANEL,
            fg=ACCENT,
            font=("Arial", 11, "bold")
        ).pack(pady=10)

        Button(
            right,
            text="LUZ ON",
            width=18,
            height=2,
            bg="#324225",
            fg="white",
            command=self.light_on
        ).pack(pady=8)

        Button(
            right,
            text="LUZ OFF",
            width=18,
            height=2,
            bg="#4a2b2b",
            fg="white",
            command=self.light_off
        ).pack(pady=8)

        self.light_status = Label(
            right,
            text="Luz: ---",
            bg=PANEL,
            fg=TEXT
        )
        self.light_status.pack(pady=15)

        Label(
            right,
            text="BRUJULA",
            bg=PANEL,
            fg=ACCENT,
            font=("Arial", 10, "bold")
        ).pack(pady=(15, 5))

        self.compass = tk.Canvas(
            right,
            width=180,
            height=180,
            bg=PANEL,
            highlightthickness=0
        )
        self.compass.pack()

        self.draw_compass(0)

        Label(
            right,
            text="HORIZONTE",
            bg=PANEL,
            fg=ACCENT,
            font=("Arial", 10, "bold")
        ).pack(pady=(18, 5))

        self.horizon = HorizonteWidget(right, 180, 180)
        self.horizon.pack()

        self.root.bind(
            "<Escape>",
            self.close_tactical_mode
        )

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

        try:
            self.stream_session.close()
        except:
            pass

        self.image_label.configure(
            image="",
            text="Parado"
        )

    # ==================================================
    # STREAM
    # ==================================================

    def stream_worker(self):

        while self.running:

            ip = self.ip_entry.get().strip()
            url = f"http://{ip}{STREAM_ENDPOINT}"

            response = None

            try:
                response = self.stream_session.get(
                    url,
                    stream=True,
                    timeout=(3, 5)
                )

                response.raise_for_status()

                buffer = b""

                for chunk in response.iter_content(chunk_size=512):

                    if not self.running:
                        return

                    if not chunk:
                        continue

                    buffer += chunk

                    # Evita crecimiento infinito de memoria
                    if len(buffer) > 400000:
                        buffer = buffer[-150000:]

                    # Procesa TODOS los frames disponibles
                    while True:

                        a = buffer.find(b'\xff\xd8')   # JPEG start
                        b = buffer.find(b'\xff\xd9', a + 2)  # JPEG end

                        if a == -1 or b == -1:
                            break

                        jpg = buffer[a:b+2]
                        buffer = buffer[b+2:]

                        now = time.time()

                        # Limitador FPS local
                        if now - self.last_frame_time < (1 / self.max_fps):
                            continue

                        self.last_frame_time = now

                        try:
                            img = Image.open(BytesIO(jpg))
                            img = img.convert("RGB")
                            img = img.resize(
                                (STREAM_WIDTH, STREAM_HEIGHT),
                                Image.Resampling.LANCZOS
                            )

                            self.root.after(
                                0,
                                self.show_stream_image,
                                img
                            )

                        except:
                            pass

            except:
                time.sleep(RECONNECT_DELAY)

            finally:
                try:
                    if response:
                        response.close()
                except:
                    pass

    # ==================================================
    # FRAME
    # ==================================================

    def show_stream_image(self, img):

        if not self.running:
            return

        imgtk = ImageTk.PhotoImage(img)

        self.image_label.imgtk = imgtk
        self.image_label.configure(
            image=imgtk,
            text=""
        )

        if self.tactical_label:

            sw = self.tactical_window.winfo_width()
            sh = self.tactical_window.winfo_height()

            if sw < 300:
                sw = 1280
            if sh < 300:
                sh = 720

            big = img.resize((sw, sh))
            bigtk = ImageTk.PhotoImage(big)

            self.tactical_label.imgtk = bigtk
            self.tactical_label.configure(image=bigtk)

            self.tactical_hud.configure(
                text=(
                    f"VIDEO LINK   "
                    f"GPS {self.last_lat:.5f},{self.last_lon:.5f}   "
                    f"HDG {self.last_course:.1f}°"
                )
            )

    # ==================================================
    # TELEMETRY
    # ==================================================

    def update_telemetry_loop(self):

        if not self.running:
            return

        ip = self.ip_entry.get().strip()
        url = f"http://{ip}{TELEMETRY_ENDPOINT}"

        try:

            r = requests.get(url, timeout=1)
            data = r.json()

            self.telemetry_display.configure(
                text=self.format_dict(data)
            )

            gps = data.get("gps", {})

            lat = gps.get("lat")
            lon = gps.get("lon")

            if lat is not None and lon is not None:

                self.last_lat = float(lat)
                self.last_lon = float(lon)

                self.update_map(
                    self.last_lat,
                    self.last_lon
                )

            self.last_course = float(
                gps.get("course", 0)
            )

            self.draw_compass(
                self.last_course
            )

            imu = data.get("imu", {})

            pitch = float(imu.get("pitch", 0))
            roll = float(imu.get("roll", 0))

            self.horizon.set_attitude(pitch, roll)

          

        except:
            self.telemetry_display.configure(
                text="Telemetria offline"
            )

        self.root.after(
            TELEMETRY_INTERVAL_MS,
            self.update_telemetry_loop
        )

    def format_dict(self, d, indent=0):

        txt = ""

        for k, v in d.items():

            if isinstance(v, dict):
                txt += " " * indent + f"{k}:\n"
                txt += self.format_dict(v, indent + 2)
            else:
                txt += " " * indent + f"{k}: {v}\n"

        return txt

    # ==================================================
    # MAPA AUTO ONLINE/OFFLINE
    # ==================================================

    def update_map(self, lat, lon):

        try:
            if self.map_online:

                self.marker.set_position(lat, lon)
                self.map.set_position(lat, lon)

        except:
            self.activate_offline_grid()

        if not self.map_online:
            self.draw_grid(lat, lon)

    def activate_offline_grid(self):

        self.map_online = False

        self.map.pack_forget()
        self.grid.pack(fill="both", expand=True)

    def draw_grid(self, lat, lon):

        c = self.grid
        c.delete("all")

        w = MAP_WIDTH
        h = MAP_HEIGHT

        # grid
        for x in range(0, w, 40):
            c.create_line(x, 0, x, h, fill="#003300")

        for y in range(0, h, 40):
            c.create_line(0, y, w, y, fill="#003300")

        # center rover
        cx = w // 2
        cy = h // 2

        c.create_oval(
            cx-6, cy-6,
            cx+6, cy+6,
            fill="red",
            outline=""
        )

        # heading
        rad = math.radians(self.last_course - 90)

        x2 = cx + math.cos(rad) * 40
        y2 = cy + math.sin(rad) * 40

        c.create_line(
            cx, cy, x2, y2,
            fill="#00ff00",
            width=3,
            arrow=tk.LAST
        )

        c.create_text(
            10, 10,
            anchor="nw",
            fill="#00ff00",
            font=("Consolas", 10),
            text="TACTICAL GRID OFFLINE"
        )

        c.create_text(
            10, 30,
            anchor="nw",
            fill="#00ff00",
            font=("Consolas", 10),
            text=f"LAT {lat:.6f}"
        )

        c.create_text(
            10, 50,
            anchor="nw",
            fill="#00ff00",
            font=("Consolas", 10),
            text=f"LON {lon:.6f}"
        )

        c.create_text(
            10, 70,
            anchor="nw",
            fill="#00ff00",
            font=("Consolas", 10),
            text=f"HDG {self.last_course:.1f}"
        )

    # ==================================================
    # BRUJULA
    # ==================================================

    def draw_compass(self, deg):

        c = self.compass
        c.delete("all")

        cx = 90
        cy = 90
        r = 70

        c.create_oval(
            cx-r, cy-r,
            cx+r, cy+r,
            outline=ACCENT,
            width=2
        )

        c.create_text(cx, 10, text="N", fill=TEXT)
        c.create_text(cx, 170, text="S", fill=TEXT)
        c.create_text(10, cy, text="W", fill=TEXT)
        c.create_text(170, cy, text="E", fill=TEXT)

        rad = math.radians(deg - 90)

        x = cx + math.cos(rad) * 55
        y = cy + math.sin(rad) * 55

        c.create_line(
            cx, cy, x, y,
            fill="red",
            width=4,
            arrow=tk.LAST
        )

        c.create_text(
            cx,
            cy+95,
            text=f"{deg:.1f}°",
            fill=TEXT
        )

    # ==================================================
    # FULLSCREEN TACTICO
    # ==================================================


    def open_tactical_mode(self, event=None):

        if self.tactical_window:
            return

        self.zoom_factor = 1.0

        self.tactical_window = tk.Toplevel(self.root)
        self.tactical_window.configure(bg="black")
        self.tactical_window.attributes("-fullscreen", True)

        # VIDEO FULLSCREEN
        self.tactical_label = Label(
            self.tactical_window,
            bg="black",
            bd=0,
            highlightthickness=0
        )

        self.tactical_label.place(
            x=0,
            y=0,
            relwidth=1,
            relheight=1
        )

        # HUD SUPERIOR
        self.tactical_hud = Label(
            self.tactical_window,
            text="VIDEO LINK",
            bg="#001100",
            fg="#00ff00",
            font=("Consolas", 14, "bold"),
            padx=12,
            pady=6
        )

        self.tactical_hud.place(x=20, y=20)

        # EVENTOS
        self.tactical_window.bind(
            "<Escape>",
            self.close_tactical_mode
        )

        self.tactical_window.bind(
            "<Double-Button-1>",
            self.close_tactical_mode
        )

        # Zoom rueda ratón Windows
        self.tactical_window.bind(
            "<MouseWheel>",
            self.tactical_zoom
        )

        # Zoom Linux
        self.tactical_window.bind(
            "<Button-4>",
            self.tactical_zoom
        )

        self.tactical_window.bind(
            "<Button-5>",
            self.tactical_zoom
        )

    def close_tactical_mode(self, event=None):

        if self.tactical_window:
            self.tactical_window.destroy()

        self.tactical_window = None
        self.tactical_label = None
        self.tactical_hud = None
        self.crosshair = None

    def tactical_zoom(self, event):

        if not self.tactical_window:
            return

        # Windows
        if hasattr(event, "delta"):

            if event.delta > 0:
                self.zoom_factor += 0.15
            else:
                self.zoom_factor -= 0.15

        else:
            # Linux
            if event.num == 4:
                self.zoom_factor += 0.15
            elif event.num == 5:
                self.zoom_factor -= 0.15

        # límites
        if self.zoom_factor < 1.0:
            self.zoom_factor = 1.0

        if self.zoom_factor > 4.0:
            self.zoom_factor = 4.0    

    def draw_crosshair(self):

        c = self.crosshair
        c.delete("all")

        cx = 60
        cy = 60

        c.create_line(0, cy, 45, cy, fill="#00ff00", width=2)
        c.create_line(75, cy, 120, cy, fill="#00ff00", width=2)

        c.create_line(cx, 0, cx, 45, fill="#00ff00", width=2)
        c.create_line(cx, 75, cx, 120, fill="#00ff00", width=2)

        c.create_oval(
            52, 52, 68, 68,
            outline="#00ff00",
            width=2
        )

    # ==================================================
    # LUCES
    # ==================================================

    def light_on(self):
        self.call_endpoint(LIGHT_ON_ENDPOINT, "Luz ON")

    def light_off(self):
        self.call_endpoint(LIGHT_OFF_ENDPOINT, "Luz OFF")

    def call_endpoint(self, ep, txt):

        ip = self.ip_entry.get().strip()
        url = f"http://{ip}{ep}"

        try:
            requests.get(url, timeout=1)
            self.light_status.configure(text=txt)
        except:
            self.light_status.configure(text="Error")

    # ==================================================
    # CLOSE
    # ==================================================

    def on_close(self):

        self.running = False
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