import tkinter as tk
import math

BG = "#1c2218"
SKY = "#4a86ff"
GROUND = "#8a5528"
WHITE = "white"
YELLOW = "#ffd400"
GREEN = "#7aff5c"


class HorizonteWidget(tk.Canvas):

    def __init__(self, parent, width=180, height=180):
        super().__init__(
            parent,
            width=width,
            height=height,
            bg=BG,
            highlightthickness=0
        )

        self.w = width
        self.h = height

        self.pitch = 0
        self.roll = 0

        self.draw()

    def set_attitude(self, pitch, roll):
        self.pitch = float(pitch)
        self.roll = float(roll)
        self.draw()

    def draw(self):
        self.delete("all")

        w = self.w
        h = self.h

        cx = w / 2
        cy = h / 2
        r = 72

        # fondo
        self.create_oval(
            cx-r, cy-r, cx+r, cy+r,
            fill="#101010",
            outline=GREEN,
            width=2
        )

        # pitch
        offset = self.pitch * 1.2

        # línea horizonte larga rotada
        ang = math.radians(self.roll)

        x1 = -220
        y1 = offset

        x2 = 220
        y2 = offset

        xr1 = x1 * math.cos(ang) - y1 * math.sin(ang)
        yr1 = x1 * math.sin(ang) + y1 * math.cos(ang)

        xr2 = x2 * math.cos(ang) - y2 * math.sin(ang)
        yr2 = x2 * math.sin(ang) + y2 * math.cos(ang)

        # cielo
        self.create_polygon(
            cx+xr1, cy+yr1,
            cx+xr2, cy+yr2,
            cx+300, cy-300,
            cx-300, cy-300,
            fill=SKY,
            outline=""
        )

        # tierra
        self.create_polygon(
            cx+xr1, cy+yr1,
            cx+xr2, cy+yr2,
            cx+300, cy+300,
            cx-300, cy+300,
            fill=GROUND,
            outline=""
        )

        # recorte falso: aro encima
        self.create_oval(
            cx-r, cy-r, cx+r, cy+r,
            outline=GREEN,
            width=3
        )

        # línea horizonte
        self.create_line(
            cx+xr1, cy+yr1,
            cx+xr2, cy+yr2,
            fill=WHITE,
            width=2
        )

        # avión fijo
        self.create_line(cx-28, cy, cx-8, cy, fill=YELLOW, width=3)
        self.create_line(cx+8, cy, cx+28, cy, fill=YELLOW, width=3)

        self.create_oval(
            cx-3, cy-3, cx+3, cy+3,
            outline=YELLOW,
            width=2
        )

        # textos abajo
        self.create_text(
            cx, h-10,
            text=f"P {self.pitch:+.0f}°   R {self.roll:+.0f}°",
            fill="white",
            font=("Consolas", 9, "bold")
        )