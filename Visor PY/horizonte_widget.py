import tkinter as tk
import math
from PIL import Image, ImageDraw, ImageTk, ImageFont

BG = "#1c2218"
SKY = "#4a86ff"
GROUND = "#8a5528"
WHITE = "white"
YELLOW = "#ffd400"
GREEN = "#7aff5c"
TEXT = "#d8e2cf"



def _font(size, bold=False):
    """Fuente escalable para PIL; cae a la fuente básica si no existe."""
    names = (
        ["DejaVuSans-Bold.ttf", "Arial Bold.ttf", "arialbd.ttf"]
        if bold else
        ["DejaVuSans.ttf", "Arial.ttf", "arial.ttf"]
    )
    for name in names:
        try:
            return ImageFont.truetype(name, int(size))
        except Exception:
            pass
    return ImageFont.load_default()

def norm180(deg):
    """Normaliza un ángulo a [-180, 180)."""
    return ((float(deg) + 180.0) % 360.0) - 180.0


def real_attitude_to_indicator(pitch, roll):
    """
    Convierte actitud real a lo que muestra un horizonte artificial clásico.

    El pitch real puede dar vueltas completas: 0..360, 720, -360, etc.
    Cuando el pitch pasa de +90 o -90 grados, el avión queda invertido y
    el instrumento lo representa plegando el pitch visible y sumando 180°
    de roll, igual que en un ADI/horizonte artificial real.
    """
    p = norm180(pitch)
    r = float(roll)

    if p > 90.0:
        p = 180.0 - p
        r += 180.0
    elif p < -90.0:
        p = -180.0 - p
        r += 180.0

    return p, norm180(r)


def norm360(deg):
    """Normaliza un ángulo a [0, 360)."""
    return float(deg) % 360.0


def heading_cardinal(deg):
    """Devuelve N, NE, E... según el rumbo."""
    dirs = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
    return dirs[int((norm360(deg) + 22.5) // 45) % 8]


class HorizonteWidget(tk.Canvas):
    """
    Horizonte artificial con recorte circular real, pitch 360° y rumbo.

    API compatible con tu visor:
        widget.set_attitude(pitch, roll)

    También acepta rumbo opcional:
        widget.set_attitude(pitch, roll, heading)
        widget.set_heading(heading)

    - pitch puede ser cualquier ángulo, por ejemplo 0..360 para simular loopings.
    - roll puede ser cualquier ángulo, se normaliza internamente.
    - heading es el rumbo en grados: 0=N, 90=E, 180=S, 270=W.
    """

    def __init__(self, parent, width=180, height=180):
        super().__init__(
            parent,
            width=width,
            height=height,
            bg=BG,
            highlightthickness=0,
        )

        self.w = int(width)
        self.h = int(height)
        self.pitch = 0.0      # pitch real
        self.roll = 0.0       # roll real
        self.heading = 0.0    # rumbo real: 0=N, 90=E, 180=S, 270=W
        self._photo = None

        self.draw()

    def set_attitude(self, pitch, roll, heading=None):
        self.pitch = float(pitch)
        self.roll = float(roll)
        if heading is not None:
            self.heading = norm360(heading)
        self.draw()

    def set_heading(self, heading):
        self.heading = norm360(heading)
        self.draw()

    def draw(self):
        self.delete("all")

        w = self.w
        h = self.h
        cx = w / 2.0

        # Banda superior compacta: deja sitio al texto de rumbo, pero no
        # encoge tanto el círculo como la versión anterior.
        top_band = max(34, h * 0.14)
        bottom_band = max(12, h * 0.05)

        # Círculo grande. En un widget de 250x250 queda aprox. 200 px
        # de diámetro, manteniendo el texto superior separado.
        max_r_by_width = w * 0.43
        max_r_by_height = (h - top_band - bottom_band) * 0.50
        r = min(max_r_by_width, max_r_by_height)
        cy = top_band + r

        # Si por tamaños raros se sale por abajo, se recoloca sin reducirlo
        # más de lo necesario.
        if cy + r > h - bottom_band:
            cy = h - bottom_band - r

        scale = r / 45.0  # px por grado; ±45° ocupa aprox. el radio

        # Fuentes escaladas al tamaño del widget.
        hdg_font = _font(max(14, w * 0.045), bold=True)
        card_font = _font(max(18, w * 0.060), bold=True)
        compass_font = _font(max(12, w * 0.038), bold=True)
        small_font = _font(max(10, w * 0.030), bold=True)

        pitch_i, roll_i = real_attitude_to_indicator(self.pitch, self.roll)

        # Imagen completa del instrumento.
        img = Image.new("RGBA", (w, h), BG)

        # Capa circular recortada.
        disk = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(disk)

        # Base negra dentro del círculo por si el horizonte sale fuera.
        d.ellipse((cx-r, cy-r, cx+r, cy+r), fill="#101010")

        # Vectores del mundo/horizonte en pantalla.
        ang = math.radians(roll_i)
        vx = math.cos(ang)
        vy = math.sin(ang)
        nx = -math.sin(ang)
        ny = math.cos(ang)

        offset = pitch_i * scale
        lx = cx + nx * offset
        ly = cy + ny * offset
        L = max(w, h) * 4

        p1 = (lx - vx * L, ly - vy * L)
        p2 = (lx + vx * L, ly + vy * L)

        # El lado -normal es cielo; el lado +normal es tierra.
        sky_poly = [
            p1,
            p2,
            (p2[0] - nx * L * 2, p2[1] - ny * L * 2),
            (p1[0] - nx * L * 2, p1[1] - ny * L * 2),
        ]
        ground_poly = [
            p1,
            p2,
            (p2[0] + nx * L * 2, p2[1] + ny * L * 2),
            (p1[0] + nx * L * 2, p1[1] + ny * L * 2),
        ]

        d.polygon(sky_poly, fill=SKY)
        d.polygon(ground_poly, fill=GROUND)

        # Línea de horizonte.
        d.line((p1[0], p1[1], p2[0], p2[1]), fill=WHITE, width=2)

        # Escalera de pitch. Se mueve y rota con el horizonte.
        for mark in range(-90, 91, 10):
            if mark == 0:
                continue

            y_local = (pitch_i - mark) * scale
            if abs(y_local) > r * 1.35:
                continue

            mx = cx + nx * y_local
            my = cy + ny * y_local
            half = 20 if mark % 30 == 0 else 12
            x1 = mx - vx * half
            y1 = my - vy * half
            x2 = mx + vx * half
            y2 = my + vy * half
            d.line((x1, y1, x2, y2), fill=WHITE, width=1)

        # Máscara circular real: todo queda dentro del instrumento.
        mask = Image.new("L", (w, h), 0)
        md = ImageDraw.Draw(mask)
        md.ellipse((cx-r, cy-r, cx+r, cy+r), fill=255)
        img.alpha_composite(Image.composite(disk, Image.new("RGBA", (w, h), (0, 0, 0, 0)), mask))

        out = ImageDraw.Draw(img)

        # Aro exterior.
        out.ellipse((cx-r, cy-r, cx+r, cy+r), outline=GREEN, width=3)

        # Marcas de alabeo fijas alrededor del aro, como referencia del instrumento.
        for deg, length in [(-60, 8), (-45, 6), (-30, 10), (-20, 6), (-10, 6), (0, 12),
                            (10, 6), (20, 6), (30, 10), (45, 6), (60, 8)]:
            a = math.radians(deg - 90)
            ro = r + 1
            ri = r - length
            x1 = cx + math.cos(a) * ri
            y1 = cy + math.sin(a) * ri
            x2 = cx + math.cos(a) * ro
            y2 = cy + math.sin(a) * ro
            out.line((x1, y1, x2, y2), fill=GREEN, width=2)

        # Rosa/compás de rumbo alrededor del aro.
        # El triángulo superior es la línea de fe: apunta al rumbo actual.
        heading = norm360(self.heading)

        # Marcas cada 10 grados y marcas principales cada 30.
        for bearing in range(0, 360, 10):
            rel = norm180(bearing - heading)
            if abs(rel) > 80:
                continue

            a = math.radians(rel - 90)
            is_major = bearing % 30 == 0
            length = 9 if is_major else 5
            ro = r + 14
            ri = ro - length
            x1 = cx + math.cos(a) * ri
            y1 = cy + math.sin(a) * ri
            x2 = cx + math.cos(a) * ro
            y2 = cy + math.sin(a) * ro
            out.line((x1, y1, x2, y2), fill=GREEN, width=2 if is_major else 1)

        # Rumbo grande, arriba.
        heading_y = max(14, h * 0.06)

        # Norte, Este, Sur y Oeste giran como una carta de compás real.
        # Para que el círculo pueda ser grande, NO dibujamos la letra que
        # queda justo arriba del todo, porque ya está indicada por el texto
        # grande del rumbo: 000° N, 090° E, etc.
        for bearing, label in [(0, "N"), (90, "E"), (180, "S"), (270, "W")]:
            rel = norm180(bearing - heading)
            if abs(rel) > 88:
                continue
            if abs(rel) < 12:
                continue
            a = math.radians(rel - 90)
            rt = r + 22
            tx = cx + math.cos(a) * rt
            ty = cy + math.sin(a) * rt
            if ty < heading_y + 22:
                continue
            out.text((tx, ty), label, fill=GREEN, anchor="mm", font=compass_font)

        # Triángulo superior fijo: línea de fe. Va pegado al aro, no al texto.
        circle_top = cy - r
        tri_tip_y = circle_top + 3
        tri_top = circle_top - 10
        tri = [(cx, tri_tip_y), (cx-8, tri_top), (cx+8, tri_top)]
        out.polygon(tri, outline=GREEN, fill=None)

        out.text(
            (cx - 8, heading_y),
            f"{heading:03.0f}°",
            fill=GREEN,
            anchor="rm",
            font=hdg_font,
        )
        out.text(
            (cx + 10, heading_y),
            heading_cardinal(heading),
            fill=GREEN,
            anchor="lm",
            font=card_font,
        )

        # Avión fijo.
        out.line((cx-30, cy, cx-8, cy), fill=YELLOW, width=3)
        out.line((cx+8, cy, cx+30, cy), fill=YELLOW, width=3)
        out.line((cx, cy-7, cx, cy+7), fill=YELLOW, width=2)
        out.ellipse((cx-3, cy-3, cx+3, cy+3), outline=YELLOW, width=2)

        # Texto inferior. P/R = actitud real; D = lo que ve el instrumento.
        out.text((cx, h-28), f"P {self.pitch:+.0f}°   R {self.roll:+.0f}°", fill=TEXT, anchor="mm", font=small_font)
        out.text((cx, h-13), f"IND P {pitch_i:+.0f}°  R {roll_i:+.0f}°", fill=WHITE, anchor="mm", font=small_font)

        self._photo = ImageTk.PhotoImage(img)
        self.create_image(0, 0, anchor="nw", image=self._photo)
