# ==========================================================
# visor.py
# VISOR ROVER - MAPA ONLINE + GRID TACTICO OFFLINE AUTO
#
# REQUISITOS:
# pip install requests pillow tkintermapview pygame
# ==========================================================

import tkinter as tk
import tkintermapview as tkm
from tkinter import Frame, Label, Entry, Button, Checkbutton, OptionMenu, StringVar, BooleanVar
from PIL import Image, ImageTk, ImageDraw
from io import BytesIO

import requests
import threading
import socket
import struct
import json
import time
import math
import subprocess
import platform
import re
import os
from datetime import datetime

# Mando/gamepad: opcional. Si pygame no esta instalado, el visor funciona igual.
try:
    import pygame
    _GAMEPAD_AVAILABLE = True
except ImportError:
    pygame = None
    _GAMEPAD_AVAILABLE = False

# Grabacion de video: opcional. Si no esta instalado opencv, el boton REC
# avisara al usuario pero el visor seguira funcionando con normalidad.
try:
    import cv2
    import numpy as np
    _RECORDING_AVAILABLE = True
except ImportError:
    _RECORDING_AVAILABLE = False

try:
    from horizonte_widget import HorizonteWidget
except ImportError:
    class HorizonteWidget(tk.Canvas):
        """Fallback mínimo si no existe horizonte_widget.py."""
        def __init__(self, parent, width, height):
            super().__init__(parent, width=width, height=height, bg="#10140d", highlightthickness=0)
            self.width = width
            self.height = height
            self.set_attitude(0, 0)

        def set_attitude(self, pitch, roll):
            self.delete("all")
            cx = self.width // 2
            cy = self.height // 2
            self.create_oval(10, 10, self.width-10, self.height-10, outline="#7aff5c", width=2)
            self.create_line(25, cy, self.width-25, cy, fill="#d8e2cf", width=2)
            self.create_text(cx, self.height-18, text=f"P {pitch:.1f}  R {roll:.1f}", fill="#d8e2cf")

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

TELEMETRY_INTERVAL_MS = 1000
RECONNECT_DELAY = 0.5
FPS_AVG_WINDOW_SEC = 2.0

STREAM_ENDPOINT = "/stream"
TELEMETRY_ENDPOINT = "/telemetry"
DEFAULT_HTTP_PORT = "8000"

UDP_START_ENDPOINT = "/udp/start"
UDP_STOP_ENDPOINT = "/udp/stop"
UDP_PING_ENDPOINT = "/udp/ping"
UDP_VIDEO_PORT = 4210
UDP_TELEMETRY_PORT = 4211
UDP_MAGIC = 0xCAFE
UDP_HEADER_FORMAT = "!HHHHH"
UDP_HEADER_SIZE = struct.calcsize(UDP_HEADER_FORMAT)
UDP_FRAME_TIMEOUT_SEC = 0.35
UDP_SOCKET_TIMEOUT_SEC = 1.0
UDP_PING_INTERVAL_SEC = 4.0
UDP_TELEMETRY_SOCKET_TIMEOUT_SEC = 1.0

# Watchdog UDP: si se pierden a la vez video y telemetria,
# el visor reinicia la sesion UDP automaticamente.
UDP_WATCHDOG_INTERVAL_MS = 1000
UDP_VIDEO_STALL_TIMEOUT_SEC = 4.0
UDP_TELEMETRY_STALL_TIMEOUT_SEC = 4.0
UDP_RESTART_DELAY_SEC = 0.6

# Opciones del desplegable del visor.
# Cambia los endpoints si en tu firmware usan otros nombres.
STREAM_OPTIONS = {
    # En tu firmware actual: /stream_low = LOW y /stream = HIGH.
    "LOW / estable": "/stream_low",
    "HIGH / calidad": "/stream",
}
DEFAULT_STREAM_MODE = "LOW / estable"

TRANSPORT_OPTIONS = {
    "HTTP / estable": "HTTP",
    "UDP / baja latencia": "UDP",
}
DEFAULT_VIDEO_TRANSPORT = "HTTP / estable"

LIGHT_ON_ENDPOINT = "/luces/on"
LIGHT_OFF_ENDPOINT = "/luces/off"

# Envio de control al rover.
# GET: http://IP:PUERTO/control?avanzar=0..255&retroceder=0..255&izquierda=0..255&derecha=0..255
CONTROL_ENDPOINT = "/control"
CONTROL_SEND_INTERVAL_MS = 100
CONTROL_HTTP_TIMEOUT_SEC = 0.15

# ==========================================================
# RECORDING
# ==========================================================

RECORDINGS_DIR = "recordings"
# FPS al que se reproducira el AVI. Lo ideal es que coincida con
# self.max_fps del limitador local. Si el rover envia mas lento,
# el video se reproducira acelerado; si envia mas rapido, lento.
RECORDING_FPS = 15
# MJPG dentro de AVI: practicamente sin re-encode (el frame ya es JPEG)
# y compatible con cualquier reproductor.
RECORDING_FOURCC = "MJPG"
RECORDING_EXTENSION = ".avi"

# ==========================================================
# COLORS
# ==========================================================

BG = "#10140d"
PANEL = "#1c2218"
TEXT = "#d8e2cf"
ACCENT = "#7aff5c"

# ==========================================================
# MANDO ESM-9101
# ==========================================================

GAMEPAD_INTERVAL_MS = 50
ZONA_MUERTA = 0.15

# Botones principales segun el mapeo medido:
# A = indice 2, B = indice 1, X = indice 3, Y = indice 0
BOTON_Y = 0
BOTON_A = 2
BOTON_X = 3
BOTON_B = 1

# Cruceta real: HAT 0
# Stick izquierdo real: ejes 0 y 1
EJE_STICK_IZQ_X = 0
EJE_STICK_IZQ_Y = 1

# Stick derecho y gatillos. Si tu mando usa otros ejes, cambia aqui.
EJE_STICK_DER_X = 2
EJE_STICK_DER_Y = 3
EJE_GATILLO_IZQ = 4
EJE_GATILLO_DER = 5

# Teclado: flechas del cursor. Si una flecha esta pulsada,
# tiene prioridad sobre el mando para las barras de movimiento.
KEYBOARD_PRIORITY_LABEL = "TECLADO"
GAMEPAD_PRIORITY_LABEL = "MANDO"

# ==========================================================
# FUNCIONES DE LECTURA DEL MANDO
# ==========================================================

def aplicar_zona_muerta(valor, zona_muerta=ZONA_MUERTA):

    if abs(valor) < zona_muerta:
        return 0.0

    return valor


def leer_eje_seguro(mando, eje):

    if eje < mando.get_numaxes():
        return mando.get_axis(eje)

    return 0.0


def leer_boton_seguro(mando, boton):

    if boton < mando.get_numbuttons():
        return bool(mando.get_button(boton))

    return False


def normalizar_gatillo(valor):

    valor_normalizado = (valor + 1) / 2
    return max(0.0, min(1.0, valor_normalizado))


def valor_0_1_a_byte(valor):
    """Convierte un valor 0.0..1.0 a entero 0..255."""

    try:
        valor = float(valor)
    except (TypeError, ValueError):
        valor = 0.0

    valor = max(0.0, min(1.0, valor))
    return int(round(valor * 255))


def leer_mando(mando):
    """
    Lee el estado completo del mando.

    Devuelve:
    - botones A, B, X, Y
    - cruceta real HAT 0
    - stick izquierdo ejes 0 y 1
    - stick derecho ejes 2 y 3
    - gatillos ejes 4 y 5
    """

    # Importante en Tkinter: vaciamos la cola de eventos de pygame
    # para que los ejes/botones del mando se refresquen continuamente.
    try:
        pygame.event.pump()
        pygame.event.get([
            pygame.JOYAXISMOTION,
            pygame.JOYBUTTONDOWN,
            pygame.JOYBUTTONUP,
            pygame.JOYHATMOTION,
            pygame.JOYDEVICEADDED,
            pygame.JOYDEVICEREMOVED,
        ])
    except Exception:
        pass

    botones = {
        "A": leer_boton_seguro(mando, BOTON_A),
        "B": leer_boton_seguro(mando, BOTON_B),
        "X": leer_boton_seguro(mando, BOTON_X),
        "Y": leer_boton_seguro(mando, BOTON_Y),
    }

    if mando.get_numhats() > 0:
        hat = mando.get_hat(0)
    else:
        hat = (0, 0)

    cruceta = {
        "arriba": hat == (0, 1),
        "abajo": hat == (0, -1),
        "izquierda": hat == (-1, 0),
        "derecha": hat == (1, 0),
        "valor": hat,
    }

    stick_izq_x = aplicar_zona_muerta(
        leer_eje_seguro(mando, EJE_STICK_IZQ_X)
    )

    stick_izq_y = aplicar_zona_muerta(
        leer_eje_seguro(mando, EJE_STICK_IZQ_Y)
    )

    stick_izquierdo = {
        "x": stick_izq_x,
        "y": stick_izq_y,
        "avanzar": max(0.0, -stick_izq_y),
        "retroceder": max(0.0, stick_izq_y),
        "izquierda": max(0.0, -stick_izq_x),
        "derecha": max(0.0, stick_izq_x),
        "arriba": stick_izq_y < -ZONA_MUERTA,
        "abajo": stick_izq_y > ZONA_MUERTA,
        "izquierda_activo": stick_izq_x < -ZONA_MUERTA,
        "derecha_activo": stick_izq_x > ZONA_MUERTA,
    }

    stick_der_x = aplicar_zona_muerta(
        leer_eje_seguro(mando, EJE_STICK_DER_X)
    )

    stick_der_y = aplicar_zona_muerta(
        leer_eje_seguro(mando, EJE_STICK_DER_Y)
    )

    stick_derecho = {
        "x": stick_der_x,
        "y": stick_der_y,
        "arriba": stick_der_y < -ZONA_MUERTA,
        "abajo": stick_der_y > ZONA_MUERTA,
        "izquierda": stick_der_x < -ZONA_MUERTA,
        "derecha": stick_der_x > ZONA_MUERTA,
    }

    gatillo_izq_raw = leer_eje_seguro(mando, EJE_GATILLO_IZQ)
    gatillo_der_raw = leer_eje_seguro(mando, EJE_GATILLO_DER)

    gatillo_izq = normalizar_gatillo(gatillo_izq_raw)
    gatillo_der = normalizar_gatillo(gatillo_der_raw)

    gatillos = {
        "izquierdo": gatillo_izq,
        "derecho": gatillo_der,
        "izquierdo_raw": gatillo_izq_raw,
        "derecho_raw": gatillo_der_raw,
        "izquierdo_pulsado": gatillo_izq > 0.5,
        "derecho_pulsado": gatillo_der > 0.5,
    }

    return {
        "botones": botones,
        "cruceta": cruceta,
        "stick_izquierdo": stick_izquierdo,
        "stick_derecho": stick_derecho,
        "gatillos": gatillos,
    }

# ==========================================================
# APP
# ==========================================================

class Visor:

    def __init__(self, root):

        self.root = root
        self.root.title(WINDOW_TITLE)
        self.root.configure(bg=BG)
        self.root.resizable(True, True)
        self.root.minsize(900, 620)

        self.running = False
        self.stream_thread = None
        self.stream_session = requests.Session()
        self.udp_socket = None
        self.udp_telemetry_socket = None
        self.udp_ping_thread = None
        self.udp_telemetry_thread = None
        self.last_udp_video_time = 0.0
        self.last_udp_telemetry_time = 0.0
        self.udp_restart_in_progress = False

        self.stream_mode = StringVar(value=DEFAULT_STREAM_MODE)
        self.video_transport = StringVar(value=DEFAULT_VIDEO_TRANSPORT)
        self.rotate_display_image = BooleanVar(value=False)
        self.active_stream_endpoint = STREAM_OPTIONS[DEFAULT_STREAM_MODE]

        self.last_frame_time = 0
        self.max_fps = 15

        # FPS reales recibidos desde el stream MJPEG.
        self.rx_fps = 0.0
        self.rx_frame_count = 0
        self.rx_fps_window_start = time.time()

        self.last_lat = 37.59643052360859
        self.last_lon = -0.976892145443811
        self.last_course = 0

        self.marker = None
        self.map_online = True

        self.tactical_window = None
        self.tactical_label = None
        self.tactical_hud = None
        self.crosshair = None

        # Estado de grabacion de video
        self.recording = False
        self.video_writer = None
        self.recording_path = None
        self.recording_start_time = 0.0
        self.recording_frame_count = 0
        self.recording_size = None

        # Estado del mando/gamepad
        self.gamepad = None
        self.gamepad_name = "Sin mando"
        self.gamepad_estado = None
        self.gamepad_tick = 0

        # Estado de teclado para flechas.
        # Estas entradas tendran prioridad sobre el mando.
        self.key_up = False
        self.key_down = False
        self.key_left = False
        self.key_right = False
        self.control_origen = GAMEPAD_PRIORITY_LABEL

        # Estado de envio de control al endpoint /control
        self.control_payload = {
            "avanzar": 0,
            "retroceder": 0,
            "izquierda": 0,
            "derecha": 0,
        }
        self.control_last_send_time = 0.0
        self.control_request_in_flight = False
        self.control_send_count = 0
        self.control_error_count = 0
        self.control_last_status = "Control: ---"

        # Dimensiones dinámicas de la zona central. Se actualizan al maximizar
        # o al cambiar manualmente el tamaño de la ventana.
        self.stream_display_width = STREAM_WIDTH
        self.stream_display_height = STREAM_HEIGHT
        self.map_display_width = MAP_WIDTH
        self.map_display_height = MAP_HEIGHT
        self.last_window_size = (0, 0)
        self.last_display_img = None

        total_height = STREAM_HEIGHT + MAP_HEIGHT

        # ==================================================
        # TOP BAR
        # ==================================================

        self.top = Frame(root, bg=BG, pady=8)
        top = self.top
        top.pack(fill="x")

        Label(top, text="IP Rover:", bg=BG, fg=TEXT).pack(
            side=tk.LEFT, padx=(10, 5)
        )

        self.ip_entry = Entry(top, width=15)
        self.ip_entry.pack(side=tk.LEFT)
        self.ip_entry.insert(0, "192.168.4.1")

        Label(top, text="Puerto HTTP:", bg=BG, fg=TEXT).pack(
            side=tk.LEFT, padx=(8, 5)
        )

        self.http_port_entry = Entry(top, width=6)
        self.http_port_entry.pack(side=tk.LEFT)
        self.http_port_entry.insert(0, DEFAULT_HTTP_PORT)

        Checkbutton(
            top,
            text="Rotar imagen",
            variable=self.rotate_display_image,
            bg=BG,
            fg=TEXT,
            selectcolor=PANEL,
            activebackground=BG,
            activeforeground=ACCENT,
            highlightthickness=0
        ).pack(side=tk.LEFT, padx=(12, 5))

        Label(top, text="Stream:", bg=BG, fg=TEXT).pack(
            side=tk.LEFT, padx=(12, 5)
        )

        stream_menu = OptionMenu(
            top,
            self.stream_mode,
            *STREAM_OPTIONS.keys(),
            command=self.change_stream_mode
        )
        stream_menu.configure(
            width=14,
            bg=PANEL,
            fg=TEXT,
            activebackground=PANEL,
            activeforeground=ACCENT,
            highlightthickness=0
        )
        stream_menu["menu"].configure(bg=PANEL, fg=TEXT)
        stream_menu.pack(side=tk.LEFT, padx=5)

        Label(top, text="Transporte:", bg=BG, fg=TEXT).pack(
            side=tk.LEFT, padx=(12, 5)
        )

        transport_menu = OptionMenu(
            top,
            self.video_transport,
            *TRANSPORT_OPTIONS,
            command=self.change_video_transport
        )
        transport_menu.configure(
            width=17,
            bg=PANEL,
            fg=TEXT,
            activebackground=PANEL,
            activeforeground=ACCENT,
            highlightthickness=0
        )
        transport_menu["menu"].configure(bg=PANEL, fg=TEXT)
        transport_menu.pack(side=tk.LEFT, padx=5)

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

        self.fps_status = Label(
            top,
            text="FPS RX: --",
            bg=BG,
            fg=ACCENT,
            font=("Courier", 10, "bold")
        )
        self.fps_status.pack(side=tk.LEFT, padx=(12, 5))

        # ==================================================
        # MAIN
        # ==================================================

        self.main = Frame(root, bg=BG)
        main = self.main
        main.pack(padx=10, pady=10, fill="both", expand=True)

        # ==================================================
        # LEFT PANEL
        # ==================================================

        self.left_panel = Frame(
            main,
            width=LEFT_PANEL_WIDTH,
            height=total_height,
            bg=PANEL,
            bd=2,
            relief="groove"
        )
        left = self.left_panel
        left.pack(side=tk.LEFT, padx=(0, 10), fill="y")
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
            side=tk.TOP,
            fill="both",
            expand=True,
            padx=8,
            pady=(8, 4)
        )

        self.create_gamepad_panel(left)

        # ==================================================
        # CENTER
        # ==================================================

        self.center_panel = Frame(main, bg=BG)
        center = self.center_panel
        center.pack(side=tk.LEFT, fill="both", expand=True)

        # VIDEO
        self.stream_frame = Frame(
            center,
            width=STREAM_WIDTH,
            height=STREAM_HEIGHT,
            bg="black",
            bd=2,
            relief="groove"
        )
        stream_frame = self.stream_frame
        stream_frame.pack(fill="both", expand=True)
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
        self.map_frame = Frame(
            center,
            width=MAP_WIDTH,
            height=MAP_HEIGHT,
            bd=2,
            relief="groove"
        )
        map_frame = self.map_frame
        map_frame.pack(pady=(10, 0), fill="x")
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

        self.right_panel = Frame(
            main,
            width=RIGHT_PANEL_WIDTH,
            height=total_height,
            bg=PANEL,
            bd=2,
            relief="groove"
        )
        right = self.right_panel
        right.pack(side=tk.LEFT, padx=(10, 0), fill="y")
        right.pack_propagate(False)

        Label(
            right,
            text="CONTROL",
            bg=PANEL,
            fg=ACCENT,
            font=("Arial", 11, "bold")
        ).pack(pady=6)

        Button(
            right,
            text="LUZ ON",
            width=18,
            height=2,
            bg="#324225",
            fg="white",
            command=self.light_on
        ).pack(pady=5)

        Button(
            right,
            text="LUZ OFF",
            width=18,
            height=2,
            bg="#4a2b2b",
            fg="white",
            command=self.light_off
        ).pack(pady=5)

        self.light_status = Label(
            right,
            text="Luz: ---",
            bg=PANEL,
            fg=TEXT
        )
        self.light_status.pack(pady=(8, 2))

        # Boton de grabacion de video
        self.record_button = Button(
            right,
            text="● REC",
            width=18,
            height=2,
            bg="#3a2424",
            fg="white",
            command=self.toggle_recording
        )
        self.record_button.pack(pady=5)

        self.record_status = Label(
            right,
            text="Grabacion: ---",
            bg=PANEL,
            fg=TEXT,
            font=("Courier", 9)
        )
        self.record_status.pack(pady=(2, 4))

        self.wifi_status = Label(
            right,
            text="WiFi AP: --",
            bg=PANEL,
            fg=ACCENT,
            font=("Courier", 10, "bold")
        )
        self.wifi_status.pack(pady=(0, 6))

        Label(
            right,
            text="BRUJULA",
            bg=PANEL,
            fg=ACCENT,
            font=("Arial", 10, "bold")
        ).pack(pady=(6, 2))

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
        ).pack(pady=(6, 2))

        self.horizon = HorizonteWidget(right, 180, 180)
        self.horizon.pack()

        self.root.bind(
            "<Escape>",
            self.close_tactical_mode
        )

        self.configurar_teclado_control()

        self.update_wifi_signal_loop()
        self.init_gamepad()
        self.update_gamepad_loop()

        self.root.bind("<Configure>", self.on_window_resize)
        self.root.after(200, self.resize_layout_to_window)

    # ==================================================
    # REDIMENSIONADO DE VENTANA
    # ==================================================

    def on_window_resize(self, event=None):
        """
        Reescala la zona de vídeo y mapa cuando se maximiza la ventana
        o se cambia su tamaño manualmente.
        """
        if event is not None and event.widget is not self.root:
            return

        w = self.root.winfo_width()
        h = self.root.winfo_height()

        if (w, h) == self.last_window_size:
            return

        self.last_window_size = (w, h)
        self.root.after_idle(self.resize_layout_to_window)

    def resize_layout_to_window(self):
        try:
            root_w = max(self.root.winfo_width(), 900)
            root_h = max(self.root.winfo_height(), 620)

            top_h = max(self.top.winfo_height(), 45)
            content_h = max(360, root_h - top_h - 35)

            side_w = LEFT_PANEL_WIDTH + RIGHT_PANEL_WIDTH + 40
            center_w = max(420, root_w - side_w - 35)

            map_h = max(170, int(content_h * 0.35))
            video_h = max(240, content_h - map_h - 12)

            self.stream_display_width = center_w
            self.stream_display_height = video_h
            self.map_display_width = center_w
            self.map_display_height = map_h

            self.left_panel.configure(height=content_h)
            self.right_panel.configure(height=content_h)

            self.stream_frame.configure(
                width=self.stream_display_width,
                height=self.stream_display_height
            )

            self.map_frame.configure(
                width=self.map_display_width,
                height=self.map_display_height
            )

            try:
                self.map.configure(
                    width=self.map_display_width,
                    height=self.map_display_height
                )
            except Exception:
                pass

            try:
                self.grid.configure(
                    width=self.map_display_width,
                    height=self.map_display_height
                )
            except Exception:
                pass

            if not self.map_online:
                self.draw_grid(self.last_lat, self.last_lon)

            if self.last_display_img is not None:
                self.render_stream_image(self.last_display_img)

        except Exception:
            pass

    def get_stream_display_size(self):
        w = max(1, int(getattr(self, "stream_display_width", STREAM_WIDTH)))
        h = max(1, int(getattr(self, "stream_display_height", STREAM_HEIGHT)))
        return w, h

    def render_stream_image(self, img):
        w, h = self.get_stream_display_size()

        try:
            if img.size != (w, h):
                img = img.resize((w, h), Image.Resampling.LANCZOS)

            imgtk = ImageTk.PhotoImage(img)
            self.image_label.imgtk = imgtk
            self.image_label.configure(image=imgtk, text="")
        except tk.TclError:
            pass

    # ==================================================
    # MANDO / STICK IZQUIERDO + FLECHAS DE TECLADO
    # ==================================================

    def configurar_teclado_control(self):
        """
        Captura flechas del teclado para usarlas como control manual.
        Las flechas tienen prioridad sobre el mando.
        """

        self.root.bind_all("<KeyPress-Up>", self.on_control_key_press)
        self.root.bind_all("<KeyPress-Down>", self.on_control_key_press)
        self.root.bind_all("<KeyPress-Left>", self.on_control_key_press)
        self.root.bind_all("<KeyPress-Right>", self.on_control_key_press)

        self.root.bind_all("<KeyRelease-Up>", self.on_control_key_release)
        self.root.bind_all("<KeyRelease-Down>", self.on_control_key_release)
        self.root.bind_all("<KeyRelease-Left>", self.on_control_key_release)
        self.root.bind_all("<KeyRelease-Right>", self.on_control_key_release)

        # Si la ventana pierde foco, evitamos que quede una flecha "enganchada".
        self.root.bind_all("<FocusOut>", self.reset_keyboard_control)

        try:
            self.root.focus_set()
        except Exception:
            pass

    def on_control_key_press(self, event):

        if event.keysym == "Up":
            self.key_up = True
        elif event.keysym == "Down":
            self.key_down = True
        elif event.keysym == "Left":
            self.key_left = True
        elif event.keysym == "Right":
            self.key_right = True

    def on_control_key_release(self, event):

        if event.keysym == "Up":
            self.key_up = False
        elif event.keysym == "Down":
            self.key_down = False
        elif event.keysym == "Left":
            self.key_left = False
        elif event.keysym == "Right":
            self.key_right = False

    def reset_keyboard_control(self, event=None):

        self.key_up = False
        self.key_down = False
        self.key_left = False
        self.key_right = False

    def leer_teclado_movimiento(self):
        """
        Devuelve un diccionario compatible con stick_izquierdo usando flechas.
        Flecha arriba = avanzar.
        Flecha abajo = retroceder.
        Flecha izquierda = izquierda.
        Flecha derecha = derecha.
        """

        eje_x = 0.0
        eje_y = 0.0

        if self.key_left and not self.key_right:
            eje_x = -1.0
        elif self.key_right and not self.key_left:
            eje_x = 1.0

        if self.key_up and not self.key_down:
            eje_y = -1.0
        elif self.key_down and not self.key_up:
            eje_y = 1.0

        return {
            "x": eje_x,
            "y": eje_y,
            "avanzar": 1.0 if self.key_up and not self.key_down else 0.0,
            "retroceder": 1.0 if self.key_down and not self.key_up else 0.0,
            "izquierda": 1.0 if self.key_left and not self.key_right else 0.0,
            "derecha": 1.0 if self.key_right and not self.key_left else 0.0,
            "arriba": self.key_up and not self.key_down,
            "abajo": self.key_down and not self.key_up,
            "izquierda_activo": self.key_left and not self.key_right,
            "derecha_activo": self.key_right and not self.key_left,
        }

    def hay_teclado_movimiento(self):

        return self.key_up or self.key_down or self.key_left or self.key_right

    def aplicar_prioridad_teclado(self, stick_mando):
        """
        Si hay una flecha pulsada, devuelve movimiento de teclado.
        Si no hay flechas pulsadas, devuelve el stick izquierdo del mando.
        """

        if self.hay_teclado_movimiento():
            self.control_origen = KEYBOARD_PRIORITY_LABEL
            return self.leer_teclado_movimiento()

        self.control_origen = GAMEPAD_PRIORITY_LABEL

        if stick_mando is None:
            return {
                "x": 0.0,
                "y": 0.0,
                "avanzar": 0.0,
                "retroceder": 0.0,
                "izquierda": 0.0,
                "derecha": 0.0,
                "arriba": False,
                "abajo": False,
                "izquierda_activo": False,
                "derecha_activo": False,
            }

        return stick_mando

    def stick_a_control_payload(self, stick):
        """
        Convierte el estado del stick/flechas a parametros 0..255
        para el endpoint /control.
        """

        if not stick:
            stick = {}

        return {
            "avanzar": valor_0_1_a_byte(stick.get("avanzar", 0.0)),
            "retroceder": valor_0_1_a_byte(stick.get("retroceder", 0.0)),
            "izquierda": valor_0_1_a_byte(stick.get("izquierda", 0.0)),
            "derecha": valor_0_1_a_byte(stick.get("derecha", 0.0)),
        }

    def actualizar_control_payload(self, stick):
        """Actualiza los valores que se dibujan y se envian al rover."""

        self.control_payload = self.stick_a_control_payload(stick)

    def maybe_send_control(self):
        """
        Envia GET /control de forma periodica sin bloquear Tkinter.
        Se llama desde el bucle de lectura del mando/teclado.
        """

        now = time.time()
        min_interval = CONTROL_SEND_INTERVAL_MS / 1000.0

        if self.control_request_in_flight:
            return

        if now - self.control_last_send_time < min_interval:
            return

        self.control_last_send_time = now
        payload = dict(self.control_payload)
        self.control_request_in_flight = True

        threading.Thread(
            target=self.send_control_worker,
            args=(payload,),
            daemon=True
        ).start()

    def send_control_worker(self, payload):
        """Hilo auxiliar para enviar el GET /control sin congelar la interfaz."""

        ok = False
        status = None
        err = None

        try:
            url = f"{self.get_http_base_url()}{CONTROL_ENDPOINT}"
            with requests.get(
                url,
                params=payload,
                timeout=CONTROL_HTTP_TIMEOUT_SEC
            ) as response:
                status = response.status_code
                ok = 200 <= status < 300
        except Exception as e:
            err = type(e).__name__

        self.root.after(0, self.finish_control_send, payload, ok, status, err)

    def finish_control_send(self, payload, ok, status, err):
        """Actualiza el estado visual tras terminar el envio /control."""

        self.control_request_in_flight = False

        if ok:
            self.control_send_count += 1
            self.control_last_status = (
                f"/control OK #{self.control_send_count}  "
                f"A:{payload['avanzar']:03d} R:{payload['retroceder']:03d} "
                f"I:{payload['izquierda']:03d} D:{payload['derecha']:03d}"
            )
            fg = ACCENT
        else:
            self.control_error_count += 1
            detalle = f"HTTP {status}" if status is not None else str(err)
            self.control_last_status = f"/control ERROR #{self.control_error_count}: {detalle}"
            fg = "#ff7070"

        if hasattr(self, "control_http_status"):
            self.control_http_status.configure(
                text=self.control_last_status,
                fg=fg
            )

    def create_gamepad_panel(self, parent):

        frame = Frame(
            parent,
            bg=PANEL,
            bd=1,
            relief="groove"
        )
        frame.pack(
            side=tk.BOTTOM,
            fill="x",
            padx=8,
            pady=(4, 8)
        )

        Label(
            frame,
            text="CONTROL - STICK / FLECHAS",
            bg=PANEL,
            fg=ACCENT,
            font=("Arial", 9, "bold")
        ).pack(pady=(5, 2))

        self.gamepad_status = Label(
            frame,
            text="Mando: buscando...",
            bg=PANEL,
            fg=TEXT,
            font=("Courier", 8),
            justify=tk.LEFT
        )
        self.gamepad_status.pack(anchor="w", padx=8)

        self.control_http_status = Label(
            frame,
            text="/control: ---",
            bg=PANEL,
            fg=TEXT,
            font=("Courier", 8),
            justify=tk.LEFT
        )
        self.control_http_status.pack(anchor="w", padx=8, pady=(2, 0))

        self.gamepad_canvas = tk.Canvas(
            frame,
            width=250,
            height=150,
            bg="#11160f",
            highlightthickness=0
        )
        self.gamepad_canvas.pack(padx=6, pady=(3, 7))

    def init_gamepad(self):

        if not _GAMEPAD_AVAILABLE:
            if hasattr(self, "gamepad_status"):
                self.gamepad_status.configure(
                    text="Mando: falta pygame",
                    fg="#ff7070"
                )
            return

        try:
            pygame.init()

            # Refresco de la capa joystick. En algunos mandos Windows/DirectInput
            # el estado no empieza a cambiar hasta reinicializar joystick.
            if not pygame.joystick.get_init():
                pygame.joystick.init()

            pygame.event.pump()

            if pygame.joystick.get_count() == 0:
                self.gamepad = None
                self.gamepad_name = "Sin mando"
                return

            self.gamepad = pygame.joystick.Joystick(0)
            self.gamepad.init()
            self.gamepad_name = self.gamepad.get_name()

        except Exception as e:
            self.gamepad = None
            self.gamepad_name = f"Error mando: {type(e).__name__}"

    def update_gamepad_loop(self):

        self.gamepad_tick += 1

        if not _GAMEPAD_AVAILABLE:
            stick = self.aplicar_prioridad_teclado(None)
            self.gamepad_status.configure(
                text=(
                    f"Origen: {self.control_origen} | Mando: falta pygame\n"
                    f"X:{stick['x']:.2f}  Y:{stick['y']:.2f}  t:{self.gamepad_tick}"
                ),
                fg="#ffd35a" if self.control_origen == KEYBOARD_PRIORITY_LABEL else "#ff7070"
            )
            self.actualizar_control_payload(stick)
            self.draw_stick_left_bars(stick)
            self.maybe_send_control()
            self.root.after(GAMEPAD_INTERVAL_MS, self.update_gamepad_loop)
            return

        try:
            # Mantiene viva la cola de eventos de pygame aunque la ventana sea Tkinter.
            pygame.event.pump()
        except Exception:
            pass

        if self.gamepad is None:
            self.init_gamepad()

        if self.gamepad is not None:
            try:
                self.gamepad_estado = leer_mando(self.gamepad)
                stick_mando = self.gamepad_estado["stick_izquierdo"]
                stick = self.aplicar_prioridad_teclado(stick_mando)

                self.gamepad_status.configure(
                    text=(
                        f"Origen: {self.control_origen} | Mando: {self.gamepad_name}\n"
                        f"X:{stick['x']:.2f}  Y:{stick['y']:.2f}  t:{self.gamepad_tick}"
                    ),
                    fg=ACCENT if self.control_origen == GAMEPAD_PRIORITY_LABEL else "#ffd35a"
                )

            except Exception as e:
                self.gamepad = None
                self.gamepad_status.configure(
                    text=f"Mando: desconectado ({type(e).__name__})",
                    fg="#ff7070"
                )
                stick = self.aplicar_prioridad_teclado(None)
        else:
            stick = self.aplicar_prioridad_teclado(None)
            self.gamepad_status.configure(
                text=(
                    f"Origen: {self.control_origen} | Mando: no detectado\n"
                    f"X:{stick['x']:.2f}  Y:{stick['y']:.2f}  t:{self.gamepad_tick}"
                ),
                fg="#ffd35a" if self.control_origen == KEYBOARD_PRIORITY_LABEL else TEXT
            )

        self.actualizar_control_payload(stick)
        self.draw_stick_left_bars(stick)
        self.maybe_send_control()

        self.root.after(GAMEPAD_INTERVAL_MS, self.update_gamepad_loop)

    def draw_stick_left_bars(self, stick):

        c = self.gamepad_canvas
        c.delete("all")

        valores = {
            "AVANZAR": 0.0,
            "RETROCEDER": 0.0,
            "IZQUIERDA": 0.0,
            "DERECHA": 0.0,
        }

        if stick:
            valores["AVANZAR"] = stick.get("avanzar", 0.0)
            valores["RETROCEDER"] = stick.get("retroceder", 0.0)
            valores["IZQUIERDA"] = stick.get("izquierda", 0.0)
            valores["DERECHA"] = stick.get("derecha", 0.0)

        c.create_text(
            8,
            8,
            anchor="w",
            text=f"Prioridad: {self.control_origen}",
            fill="#ffd35a" if self.control_origen == KEYBOARD_PRIORITY_LABEL else ACCENT,
            font=("Consolas", 8, "bold")
        )

        x_label = 8
        x_bar = 92
        y = 28
        bar_w = 130
        bar_h = 16

        for nombre, valor in valores.items():
            valor = max(0.0, min(1.0, float(valor)))

            c.create_text(
                x_label,
                y + bar_h // 2,
                anchor="w",
                text=nombre,
                fill=TEXT,
                font=("Consolas", 8, "bold")
            )

            c.create_rectangle(
                x_bar,
                y,
                x_bar + bar_w,
                y + bar_h,
                outline=TEXT,
                fill="#273020"
            )

            fill_w = int(bar_w * valor)

            if fill_w > 0:
                c.create_rectangle(
                    x_bar + 2,
                    y + 2,
                    x_bar + fill_w - 2,
                    y + bar_h - 2,
                    outline="",
                    fill=ACCENT
                )

            c.create_text(
                x_bar + bar_w + 8,
                y + bar_h // 2,
                anchor="w",
                text=f"{valor:.2f} / {valor_0_1_a_byte(valor):03d}",
                fill=TEXT,
                font=("Consolas", 8)
            )

            y += 28

        try:
            c.update_idletasks()
        except Exception:
            pass

    def current_transport(self):

        return TRANSPORT_OPTIONS.get(
            self.video_transport.get(),
            TRANSPORT_OPTIONS[DEFAULT_VIDEO_TRANSPORT]
        )

    def using_udp_transport(self):

        return self.current_transport() == "UDP"

    def get_rover_host(self):

        host = self.ip_entry.get().strip()
        host = host.replace("http://", "").replace("https://", "")
        host = host.split("/", 1)[0]

        if not host:
            host = "192.168.4.1"

        # Si el usuario escribió IP:PUERTO en el campo IP, quitamos el puerto
        # para poder usar la IP sola en las conexiones UDP auxiliares.
        if ":" in host and not host.startswith("["):
            host = host.rsplit(":", 1)[0]

        return host

    def get_http_base_url(self):

        raw_host = self.ip_entry.get().strip()
        port = self.http_port_entry.get().strip()

        host = raw_host.replace("http://", "").replace("https://", "")
        host = host.split("/", 1)[0]

        if not host:
            host = "192.168.4.1"

        # Si el usuario escribe IP:PUERTO directamente, lo respetamos.
        if ":" in host and not host.startswith("["):
            return f"http://{host}"

        if port:
            return f"http://{host}:{port}"

        return f"http://{host}"

    def get_local_ip_for_rover(self):

        # Determina la IP local real que usa el PC para llegar al rover.
        # No envía datos; UDP connect solo selecciona la interfaz de salida.
        rover_host = self.get_rover_host()
        sock = None

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.connect((rover_host, 9))
            return sock.getsockname()[0]
        except Exception:
            return None
        finally:
            try:
                if sock:
                    sock.close()
            except Exception:
                pass

    # ==================================================
    # SELECTOR DE STREAM
    # ==================================================

    def change_video_transport(self, selected_transport):

        if selected_transport not in TRANSPORT_OPTIONS:
            selected_transport = DEFAULT_VIDEO_TRANSPORT
            self.video_transport.set(selected_transport)

        was_running = self.running

        if was_running:
            self.stop_video_stream(wait_for_thread=True)

        self.last_frame_time = 0
        self.reset_fps_stats()

        self.image_label.configure(
            image="",
            text=f"Transporte seleccionado: {selected_transport}"
        )

        if was_running:
            self.root.after(600, self.start_all)

    def change_stream_mode(self, selected_mode):

        new_endpoint = STREAM_OPTIONS.get(
            selected_mode,
            STREAM_OPTIONS[DEFAULT_STREAM_MODE]
        )

        if new_endpoint == self.active_stream_endpoint:
            return

        was_running = self.running

        # Al cambiar de stream, cerramos primero el enlace de vídeo activo
        # y esperamos a que el hilo salga antes de abrir el nuevo endpoint.
        if was_running:
            self.stop_video_stream(wait_for_thread=True)

        self.active_stream_endpoint = new_endpoint
        self.last_frame_time = 0
        self.reset_fps_stats()

        self.image_label.configure(
            image="",
            text=f"Cambiando a {selected_mode}..."
        )

        # Si el visor estaba activo antes del cambio, reiniciamos con un
        # pequeño margen para que el ESP32 libere streamingActive.
        if was_running:
            self.root.after(600, self.start_all)
        else:
            self.image_label.configure(
                image="",
                text=f"Stream seleccionado: {selected_mode}"
            )

    # ==================================================
    # START / STOP
    # ==================================================

    def start_all(self):

        if self.running:
            return

        self.running = True
        self.reset_fps_stats()

        # Si se pulsó Parar antes, la sesión anterior pudo quedar cerrada.
        self.stream_session = requests.Session()

        self.stream_thread = threading.Thread(
            target=self.stream_worker,
            daemon=True
        )
        self.stream_thread.start()

        if self.using_udp_transport():
            now = time.time()
            self.last_udp_video_time = now
            self.last_udp_telemetry_time = now
            self.root.after(UDP_WATCHDOG_INTERVAL_MS, self.udp_watchdog_loop)

        self.update_telemetry_loop()

    def stop_video_stream(self, wait_for_thread=False):

        self.running = False

        try:
            self.stream_session.close()
        except Exception:
            pass

        try:
            if self.udp_socket:
                self.udp_socket.close()
        except Exception:
            pass

        try:
            if self.udp_telemetry_socket:
                self.udp_telemetry_socket.close()
        except Exception:
            pass

        if self.udp_socket is not None or self.using_udp_transport():
            try:
                requests.get(f"{self.get_http_base_url()}{UDP_STOP_ENDPOINT}", timeout=0.5)
            except Exception:
                pass

        if (
            wait_for_thread
            and self.stream_thread
            and self.stream_thread.is_alive()
            and threading.current_thread() is not self.stream_thread
        ):
            self.stream_thread.join(timeout=1.0)

        if (
            wait_for_thread
            and self.udp_ping_thread
            and self.udp_ping_thread.is_alive()
            and threading.current_thread() is not self.udp_ping_thread
        ):
            self.udp_ping_thread.join(timeout=0.5)

        if (
            wait_for_thread
            and self.udp_telemetry_thread
            and self.udp_telemetry_thread.is_alive()
            and threading.current_thread() is not self.udp_telemetry_thread
        ):
            self.udp_telemetry_thread.join(timeout=0.5)

        self.stream_thread = None
        self.udp_ping_thread = None
        self.udp_telemetry_thread = None
        self.udp_socket = None
        self.udp_telemetry_socket = None

    def stop_all(self):

        self.stop_video_stream(wait_for_thread=True)

        self.reset_fps_stats()

        self.image_label.configure(
            image="",
            text="Parado"
        )

    # ==================================================
    # FPS RX
    # ==================================================

    def reset_fps_stats(self):

        self.rx_fps = 0.0
        self.rx_frame_count = 0
        self.rx_fps_window_start = time.time()

        try:
            self.fps_status.configure(text="FPS RX: --")
        except Exception:
            pass

    def register_received_frame(self):

        self.rx_frame_count += 1
        now = time.time()
        elapsed = now - self.rx_fps_window_start

        if elapsed >= FPS_AVG_WINDOW_SEC:
            self.rx_fps = self.rx_frame_count / elapsed
            self.rx_frame_count = 0
            self.rx_fps_window_start = now

            # Tkinter solo debe actualizarse desde el hilo principal.
            self.root.after(0, self.update_fps_label)

    def update_fps_label(self):

        self.fps_status.configure(
            text=f"FPS RX: {self.rx_fps:.1f}"
        )

    def tactical_hud_text(self, zoom):

        return (
            f"VIDEO LINK   "
            f"FPS {self.rx_fps:.1f}   "
            f"GPS {self.last_lat:.5f},{self.last_lon:.5f}   "
            f"HDG {self.last_course:.1f}°   "
            f"ZOOM x{zoom:.1f}"
        )

    # ==================================================
    # STREAM
    # ==================================================

    def stream_worker(self):

        if self.using_udp_transport():
            self.udp_stream_worker()
        else:
            self.http_stream_worker()

    def http_stream_worker(self):

        while self.running:

            endpoint = self.active_stream_endpoint
            url = f"{self.get_http_base_url()}{endpoint}"

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

                        # Cuenta frames JPEG completos recibidos del rover,
                        # aunque luego el limitador local descarte alguno.
                        self.register_received_frame()

                        now = time.time()

                        # Limitador FPS local
                        if now - self.last_frame_time < (1 / self.max_fps):
                            continue

                        self.last_frame_time = now

                        try:
                            img = Image.open(BytesIO(jpg))
                            img = img.convert("RGB")
                            img = img.resize(
                                self.get_stream_display_size(),
                                Image.Resampling.LANCZOS
                            )

                            self.root.after(
                                0,
                                self.show_stream_image,
                                img
                            )

                        except:
                            pass

            except Exception as e:
                if self.running:
                    self.root.after(
                        0,
                        self.image_label.configure,
                        {
                            "image": "",
                            "text": f"Reconectando video...\n{type(e).__name__}"
                        }
                    )
                time.sleep(RECONNECT_DELAY)

            finally:
                try:
                    if response:
                        response.close()
                except:
                    pass

    def udp_ping_worker(self):

        while self.running and self.using_udp_transport():

            try:
                requests.get(
                    f"{self.get_http_base_url()}{UDP_PING_ENDPOINT}",
                    timeout=0.3
                )
            except Exception:
                pass

            # Sueño troceado para poder salir rápido al pulsar Parar.
            end_time = time.time() + UDP_PING_INTERVAL_SEC

            while time.time() < end_time:
                if not self.running or not self.using_udp_transport():
                    return
                time.sleep(0.05)

    def udp_stream_worker(self):

        selected_mode = self.stream_mode.get()
        mode = "high" if STREAM_OPTIONS.get(selected_mode) == "/stream" else "low"

        sock = None

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("0.0.0.0", UDP_VIDEO_PORT))
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)
            sock.settimeout(UDP_SOCKET_TIMEOUT_SEC)
            self.udp_socket = sock

            local_ip = self.get_local_ip_for_rover()

            start_url = (
                f"{self.get_http_base_url()}{UDP_START_ENDPOINT}"
                f"?port={UDP_VIDEO_PORT}&mode={mode}"
                f"&telemetry_port={UDP_TELEMETRY_PORT}"
            )

            if local_ip:
                start_url += f"&ip={local_ip}"

            with requests.get(start_url, timeout=2) as r:
                r.raise_for_status()
                try:
                    start_info = r.json()
                except Exception:
                    start_info = {"response": r.text}

            self.udp_ping_thread = threading.Thread(
                target=self.udp_ping_worker,
                daemon=True
            )
            self.udp_ping_thread.start()

            self.udp_telemetry_thread = threading.Thread(
                target=self.udp_telemetry_worker,
                daemon=True
            )
            self.udp_telemetry_thread.start()

            self.root.after(
                0,
                self.image_label.configure,
                {
                    "image": "",
                    "text": f"UDP activo video:{UDP_VIDEO_PORT} telemetria:{UDP_TELEMETRY_PORT} ({mode})\ncliente:{local_ip or 'auto'}"
                }
            )

            frames = {}

            while self.running and self.using_udp_transport():

                now = time.time()

                # Limpia frames incompletos antiguos.
                old_frame_ids = [
                    frame_id for frame_id, frame in frames.items()
                    if now - frame["time"] > UDP_FRAME_TIMEOUT_SEC
                ]

                for frame_id in old_frame_ids:
                    frames.pop(frame_id, None)

                try:
                    packet, _addr = sock.recvfrom(1500)
                except socket.timeout:
                    continue
                except OSError:
                    break

                if len(packet) <= UDP_HEADER_SIZE:
                    continue

                try:
                    magic, frame_id, packet_index, packet_count, payload_size = struct.unpack(
                        UDP_HEADER_FORMAT,
                        packet[:UDP_HEADER_SIZE]
                    )
                except struct.error:
                    continue

                if magic != UDP_MAGIC:
                    continue

                payload = packet[UDP_HEADER_SIZE:UDP_HEADER_SIZE + payload_size]

                if packet_count <= 0 or packet_index >= packet_count:
                    continue

                if len(payload) != payload_size:
                    continue

                frame = frames.get(frame_id)

                if frame is None or frame["count"] != packet_count:
                    frame = {
                        "count": packet_count,
                        "parts": {},
                        "time": now,
                    }
                    frames[frame_id] = frame

                frame["parts"][packet_index] = payload
                frame["time"] = now

                if len(frame["parts"]) != frame["count"]:
                    continue

                try:
                    jpg = b"".join(
                        frame["parts"][i]
                        for i in range(frame["count"])
                    )
                except KeyError:
                    frames.pop(frame_id, None)
                    continue

                frames.pop(frame_id, None)

                self.last_udp_video_time = time.time()

                # Cuenta frames JPEG completos recibidos del rover,
                # aunque luego el limitador local descarte alguno.
                self.register_received_frame()

                now = time.time()

                # Limitador FPS local.
                if now - self.last_frame_time < (1 / self.max_fps):
                    continue

                self.last_frame_time = now

                try:
                    img = Image.open(BytesIO(jpg))
                    img = img.convert("RGB")
                    img = img.resize(
                        self.get_stream_display_size(),
                        Image.Resampling.LANCZOS
                    )

                    self.root.after(
                        0,
                        self.show_stream_image,
                        img
                    )

                except Exception:
                    pass

        except Exception as e:
            if self.running:
                self.root.after(
                    0,
                    self.image_label.configure,
                    {
                        "image": "",
                        "text": f"Reconectando UDP...\n{type(e).__name__}: {e}"
                    }
                )
            time.sleep(RECONNECT_DELAY)

        finally:
            try:
                requests.get(f"{self.get_http_base_url()}{UDP_STOP_ENDPOINT}", timeout=1)
            except Exception:
                pass

            try:
                if sock:
                    sock.close()
            except Exception:
                pass

            try:
                if self.udp_telemetry_socket:
                    self.udp_telemetry_socket.close()
            except Exception:
                pass

            self.udp_socket = None
            self.udp_telemetry_socket = None

    def udp_watchdog_loop(self):

        if not self.running or not self.using_udp_transport():
            return

        if self.udp_restart_in_progress:
            self.root.after(UDP_WATCHDOG_INTERVAL_MS, self.udp_watchdog_loop)
            return

        now = time.time()
        video_age = now - self.last_udp_video_time
        telemetry_age = now - self.last_udp_telemetry_time

        # Reiniciamos solo cuando han caido las dos rutas UDP.
        # Si solo falla telemetria durante un instante, no tocamos el video.
        if (
            video_age > UDP_VIDEO_STALL_TIMEOUT_SEC
            and telemetry_age > UDP_TELEMETRY_STALL_TIMEOUT_SEC
        ):
            self.restart_udp_session(
                f"sin video {video_age:.1f}s / sin telemetria {telemetry_age:.1f}s"
            )
            return

        self.root.after(UDP_WATCHDOG_INTERVAL_MS, self.udp_watchdog_loop)

    def restart_udp_session(self, reason):

        if self.udp_restart_in_progress:
            return

        self.udp_restart_in_progress = True

        self.image_label.configure(
            image="",
            text=f"Reiniciando UDP...\n{reason}"
        )

        threading.Thread(
            target=self.restart_udp_session_worker,
            daemon=True
        ).start()

    def restart_udp_session_worker(self):

        try:
            self.stop_video_stream(wait_for_thread=True)
            time.sleep(UDP_RESTART_DELAY_SEC)
        finally:
            self.root.after(0, self.finish_udp_restart)

    def finish_udp_restart(self):

        self.udp_restart_in_progress = False

        if self.video_transport.get() not in TRANSPORT_OPTIONS:
            self.video_transport.set(DEFAULT_VIDEO_TRANSPORT)

        if not self.using_udp_transport():
            return

        if not self.running:
            self.start_all()

    # ==================================================
    # FRAME
    # ==================================================

    def show_stream_image(self, img):

        if not self.running:
            return

        if self.rotate_display_image.get():
            img = img.transpose(Image.Transpose.ROTATE_180)

        # Graba el frame antes de hacer ImageTk.PhotoImage, asi se captura
        # exactamente la misma imagen que se muestra al usuario (con o sin
        # rotacion segun el checkbox).
        if self.recording:
            self.record_frame(img)

        self.last_display_img = img.copy()
        self.render_stream_image(img)

        if self.tactical_label and self.tactical_window:

            try:
                sw = self.tactical_window.winfo_width()
                sh = self.tactical_window.winfo_height()

                if sw < 300:
                    sw = 1280
                if sh < 300:
                    sh = 720

                zoom = getattr(self, "zoom_factor", 1.0)

                if zoom > 1.0:
                    iw, ih = img.size
                    crop_w = max(1, int(iw / zoom))
                    crop_h = max(1, int(ih / zoom))
                    left = (iw - crop_w) // 2
                    top = (ih - crop_h) // 2
                    tactical_img = img.crop((left, top, left + crop_w, top + crop_h))
                else:
                    tactical_img = img

                big = tactical_img.resize(
                    (sw, sh),
                    Image.Resampling.LANCZOS
                )

                # Retícula dibujada sobre la imagen para evitar un Canvas opaco encima.
                draw = ImageDraw.Draw(big)
                cx = sw // 2
                cy = sh // 2
                gap = 18
                length = 70
                color = (0, 255, 0)
                draw.line((cx - length, cy, cx - gap, cy), fill=color, width=2)
                draw.line((cx + gap, cy, cx + length, cy), fill=color, width=2)
                draw.line((cx, cy - length, cx, cy - gap), fill=color, width=2)
                draw.line((cx, cy + gap, cx, cy + length), fill=color, width=2)
                draw.ellipse((cx - 8, cy - 8, cx + 8, cy + 8), outline=color, width=2)

                bigtk = ImageTk.PhotoImage(big)

                self.tactical_label.imgtk = bigtk
                self.tactical_label.configure(image=bigtk)

                if self.tactical_hud:
                    self.tactical_hud.configure(
                        text=self.tactical_hud_text(zoom)
                    )

            except tk.TclError:
                self.close_tactical_mode()

    def apply_telemetry_data(self, data):

        self.telemetry_display.configure(
            text=self.format_dict(data)
        )

        gps = data.get("gps", {})
        imu = data.get("imu", {})

        self.last_course = float(gps.get("course", self.last_course or 0))

        lat = gps.get("lat")
        lon = gps.get("lon")

        if lat is not None and lon is not None:

            self.last_lat = float(lat)
            self.last_lon = float(lon)

            self.update_map(
                self.last_lat,
                self.last_lon
            )

        self.draw_compass(
            self.last_course
        )

        pitch = float(imu.get("pitch", 0))
        roll = float(imu.get("roll", 0))

        self.horizon.set_attitude(pitch, roll)

    def udp_telemetry_worker(self):

        sock = None

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("0.0.0.0", UDP_TELEMETRY_PORT))
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 256 * 1024)
            sock.settimeout(UDP_TELEMETRY_SOCKET_TIMEOUT_SEC)
            self.udp_telemetry_socket = sock

            while self.running and self.using_udp_transport():

                try:
                    packet, _addr = sock.recvfrom(2048)
                except socket.timeout:
                    if time.time() - self.last_udp_telemetry_time > 3.0:
                        self.root.after(
                            0,
                            self.telemetry_display.configure,
                            {"text": "Telemetria UDP offline"}
                        )
                    continue
                except OSError:
                    break

                try:
                    data = json.loads(packet.decode("utf-8", errors="ignore"))
                except Exception:
                    continue

                self.last_udp_telemetry_time = time.time()
                self.root.after(0, self.apply_telemetry_data, data)

        finally:
            try:
                if sock:
                    sock.close()
            except Exception:
                pass

            self.udp_telemetry_socket = None

    # ==================================================
    # TELEMETRY
    # ==================================================

    def update_telemetry_loop(self):

        if not self.running:
            return

        # Si el transporte de video es UDP, la telemetria tambien llega por UDP.
        # No hacemos polling HTTP para no meter ruido en el servidor del ESP32.
        if self.using_udp_transport():
            self.root.after(
                TELEMETRY_INTERVAL_MS,
                self.update_telemetry_loop
            )
            return

        url = f"{self.get_http_base_url()}{TELEMETRY_ENDPOINT}"

        try:
            with requests.get(url, timeout=1) as r:
                r.raise_for_status()
                data = r.json()

            self.apply_telemetry_data(data)

        except Exception as e:
            self.telemetry_display.configure(
                text=f"Telemetria offline\n{type(e).__name__}"
            )

            if not self.map_online:
                self.draw_grid(self.last_lat, self.last_lon)

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

        w = max(1, c.winfo_width())
        h = max(1, c.winfo_height())

        if w <= 1:
            w = self.map_display_width
        if h <= 1:
            h = self.map_display_height

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

        self.tactical_window.protocol(
            "WM_DELETE_WINDOW",
            self.close_tactical_mode
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
            try:
                self.tactical_window.destroy()
            except tk.TclError:
                pass

        self.tactical_window = None
        self.tactical_label = None
        self.tactical_hud = None
        self.crosshair = None

    def tactical_zoom(self, event):

        if not self.tactical_window:
            return

        delta = getattr(event, "delta", 0)

        if delta > 0 or getattr(event, "num", None) == 4:
            self.zoom_factor += 0.15
        elif delta < 0 or getattr(event, "num", None) == 5:
            self.zoom_factor -= 0.15

        # límites
        self.zoom_factor = max(1.0, min(4.0, self.zoom_factor))

        if self.tactical_hud:
            self.tactical_hud.configure(
                text=(
                    f"VIDEO LINK   "
                    f"GPS {self.last_lat:.5f},{self.last_lon:.5f}   "
                    f"HDG {self.last_course:.1f}°   "
                    f"ZOOM x{self.zoom_factor:.1f}"
                )
            )

    def draw_crosshair(self):

        if not self.crosshair:
            return

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
    # RECORDING
    # ==================================================

    def toggle_recording(self):

        if not _RECORDING_AVAILABLE:
            self.record_status.configure(
                text="Falta opencv-python\npip install opencv-python",
                fg="#ff7070"
            )
            return

        if self.recording:
            self.stop_recording()
        else:
            self.start_recording()

    def start_recording(self):

        try:
            os.makedirs(RECORDINGS_DIR, exist_ok=True)
        except OSError as e:
            self.record_status.configure(
                text=f"No se pudo crear {RECORDINGS_DIR}\n{type(e).__name__}",
                fg="#ff7070"
            )
            return

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.recording_path = os.path.join(
            RECORDINGS_DIR,
            f"rover_{timestamp}{RECORDING_EXTENSION}"
        )

        # El VideoWriter se inicializa de forma perezosa al recibir el
        # primer frame, asi conocemos el tamano real (640x400 normalmente,
        # pero si en el futuro cambia, se ajusta solo).
        self.video_writer = None
        self.recording = True
        self.recording_start_time = time.time()
        self.recording_frame_count = 0
        self.recording_size = None

        self.record_button.configure(text="■ STOP", bg="#7a2424")
        self.update_recording_status()

    def stop_recording(self):

        self.recording = False

        writer = self.video_writer
        self.video_writer = None

        if writer is not None:
            try:
                writer.release()
            except Exception:
                pass

        self.record_button.configure(text="● REC", bg="#3a2424")

        if self.recording_path and self.recording_frame_count > 0:
            elapsed = time.time() - self.recording_start_time
            avg_fps = self.recording_frame_count / elapsed if elapsed > 0 else 0
            filename = os.path.basename(self.recording_path)
            self.record_status.configure(
                text=(
                    f"Guardado:\n{filename}\n"
                    f"{self.recording_frame_count} frames  "
                    f"({avg_fps:.1f} fps reales)"
                ),
                fg=ACCENT
            )
        else:
            self.record_status.configure(text="Grabacion: ---", fg=TEXT)

    def record_frame(self, img):

        if not self.recording:
            return

        try:
            # Inicializacion perezosa del writer con el tamano del primer frame
            if self.video_writer is None:
                self.recording_size = img.size  # (w, h)
                fourcc = cv2.VideoWriter_fourcc(*RECORDING_FOURCC)
                writer = cv2.VideoWriter(
                    self.recording_path,
                    fourcc,
                    RECORDING_FPS,
                    self.recording_size
                )

                if not writer.isOpened():
                    self.recording = False
                    self.record_button.configure(text="● REC", bg="#3a2424")
                    self.record_status.configure(
                        text="Error abriendo VideoWriter\n(¿codec MJPG?)",
                        fg="#ff7070"
                    )
                    try:
                        writer.release()
                    except Exception:
                        pass
                    return

                self.video_writer = writer

            # PIL Image (RGB) -> numpy -> BGR (que es lo que espera cv2)
            arr = np.array(img)
            arr = cv2.cvtColor(arr, cv2.COLOR_RGB2BGR)

            # Por si en algun momento la imagen llega con un tamano distinto
            # al del primer frame (no deberia, pero curarse en salud)
            if (arr.shape[1], arr.shape[0]) != self.recording_size:
                arr = cv2.resize(arr, self.recording_size)

            self.video_writer.write(arr)
            self.recording_frame_count += 1

        except Exception as e:
            # Si peta a mitad de grabacion, paramos limpio
            self.recording = False

            writer = self.video_writer
            self.video_writer = None

            if writer is not None:
                try:
                    writer.release()
                except Exception:
                    pass

            self.record_button.configure(text="● REC", bg="#3a2424")
            self.record_status.configure(
                text=f"Error grabando\n{type(e).__name__}",
                fg="#ff7070"
            )

    def update_recording_status(self):

        if not self.recording:
            return

        elapsed = time.time() - self.recording_start_time
        mins = int(elapsed // 60)
        secs = int(elapsed % 60)

        self.record_status.configure(
            text=(
                f"● REC {mins:02d}:{secs:02d}\n"
                f"{self.recording_frame_count} frames"
            ),
            fg="#ff5050"
        )

        self.root.after(500, self.update_recording_status)

    # ==================================================
    # WIFI SIGNAL
    # ==================================================

    def update_wifi_signal_loop(self):

        signal = self.get_wifi_signal_percent()

        if signal is None:
            self.wifi_status.configure(
                text="WiFi AP: --",
                fg=TEXT
            )
        else:
            self.wifi_status.configure(
                text=f"WiFi AP: {signal}%  {self.signal_bars(signal)}",
                fg=ACCENT
            )

        self.root.after(
            2000,
            self.update_wifi_signal_loop
        )

    def signal_bars(self, signal):

        if signal >= 80:
            return "▂▄▆█"
        elif signal >= 60:
            return "▂▄▆_"
        elif signal >= 40:
            return "▂▄__"
        elif signal >= 20:
            return "▂___"
        else:
            return "____"

    def get_wifi_signal_percent(self):

        system = platform.system().lower()

        try:
            if "windows" in system:
                return self.get_wifi_signal_windows()

            if "linux" in system:
                return self.get_wifi_signal_linux()

            if "darwin" in system:
                return self.get_wifi_signal_macos()

        except Exception:
            return None

        return None

    def get_wifi_signal_windows(self):

        output = subprocess.check_output(
            ["netsh", "wlan", "show", "interfaces"],
            encoding="utf-8",
            errors="ignore"
        )

        match = re.search(r"Signal\s*:\s*(\d+)%", output)

        if not match:
            return None

        return int(match.group(1))

    def get_wifi_signal_linux(self):

        output = subprocess.check_output(
            ["iwconfig"],
            encoding="utf-8",
            errors="ignore"
        )

        match = re.search(r"Link Quality=(\d+)/(\d+)", output)

        if match:
            current = int(match.group(1))
            maximum = int(match.group(2))

            if maximum > 0:
                return int((current / maximum) * 100)

        return None

    def get_wifi_signal_macos(self):

        airport = (
            "/System/Library/PrivateFrameworks/"
            "Apple80211.framework/Versions/Current/Resources/airport"
        )

        output = subprocess.check_output(
            [airport, "-I"],
            encoding="utf-8",
            errors="ignore"
        )

        match = re.search(r"agrCtlRSSI:\s*(-?\d+)", output)

        if not match:
            return None

        rssi = int(match.group(1))

        # Conversion aproximada RSSI -> %
        # -90 dBm muy debil, -30 dBm excelente
        signal = int(2 * (rssi + 100))
        signal = max(0, min(100, signal))

        return signal

    # ==================================================
    # LUCES
    # ==================================================

    def light_on(self):
        self.call_endpoint(LIGHT_ON_ENDPOINT, "Luz ON")

    def light_off(self):
        self.call_endpoint(LIGHT_OFF_ENDPOINT, "Luz OFF")

    def call_endpoint(self, ep, txt):

        url = f"{self.get_http_base_url()}{ep}"

        try:
            requests.get(url, timeout=1)
            self.light_status.configure(text=txt)
        except:
            self.light_status.configure(text="Error")

    # ==================================================
    # CLOSE
    # ==================================================

    def on_close(self):

        # Si estaba grabando, cerramos el fichero AVI correctamente
        if self.recording:
            try:
                self.stop_recording()
            except Exception:
                pass

        self.running = False

        try:
            self.stream_session.close()
        except Exception:
            pass

        try:
            if self.udp_socket:
                self.udp_socket.close()
        except Exception:
            pass

        try:
            if self.udp_telemetry_socket:
                self.udp_telemetry_socket.close()
        except Exception:
            pass

        try:
            requests.get(f"{self.get_http_base_url()}{UDP_STOP_ENDPOINT}", timeout=1)
        except Exception:
            pass

        try:
            if self.udp_ping_thread and self.udp_ping_thread.is_alive():
                self.udp_ping_thread.join(timeout=0.5)
        except Exception:
            pass

        if self.tactical_window:
            self.close_tactical_mode()

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
