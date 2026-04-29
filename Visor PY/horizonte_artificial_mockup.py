# horizonte_artificial_revisado.py
# -----------------------------------------
# pip install PySide6
# python horizonte_artificial_revisado.py
# -----------------------------------------

import sys
import math
from PySide6.QtCore import Qt, QTimer, QPointF
from PySide6.QtGui import (
    QPainter, QColor, QPen, QPainterPath,
    QPolygonF, QFont
)
from PySide6.QtWidgets import QApplication, QWidget, QMainWindow


# =====================================================
# UTILIDADES DE ANGULOS
# =====================================================

def norm_180(angle):
    """Normaliza un angulo a [-180, 180)."""
    return (angle + 180.0) % 360.0 - 180.0


def norm_360(angle):
    """Normaliza un angulo a [0, 360)."""
    return angle % 360.0


def lerp_angle(a, b, t):
    """Interpolacion corta entre angulos, evitando saltos 359 -> 0."""
    diff = norm_180(b - a)
    return norm_180(a + diff * t)


def attitude_to_display(pitch, roll):
    """
    Convierte una actitud fisica pitch/roll a lo que debe pintar el horizonte.

    Cuando pitch supera +90 o -90 grados, el horizonte debe quedar invertido
    y el roll efectivo se desplaza 180 grados. Esto evita el comportamiento
    raro al pasar por vertical o al simular maniobras invertidas.
    """
    pitch = norm_180(pitch)
    roll = norm_180(roll)

    if pitch > 90.0:
        pitch = 180.0 - pitch
        roll += 180.0
    elif pitch < -90.0:
        pitch = -180.0 - pitch
        roll += 180.0

    return pitch, norm_180(roll)


# =====================================================
# WIDGET HORIZONTE
# =====================================================

class ArtificialHorizon(QWidget):
    def __init__(self):
        super().__init__()

        self.pitch = 0.0
        self.roll = 0.0

        self.pitch_target = 0.0
        self.roll_target = 0.0

        self.pitch_display = 0.0
        self.roll_display = 0.0

        self.smoothing = 0.14
        self.pixels_per_10deg = 38.0

        self.setMinimumSize(520, 520)

    def set_attitude(self, pitch, roll):
        self.pitch = norm_180(float(pitch))
        self.roll = norm_180(float(roll))
        self.pitch_target, self.roll_target = attitude_to_display(
            self.pitch,
            self.roll,
        )
        self.update()

    def paintEvent(self, event):
        # Suavizado desacoplado de la entrada.
        self.pitch_display += (self.pitch_target - self.pitch_display) * self.smoothing
        self.roll_display = lerp_angle(self.roll_display, self.roll_target, self.smoothing)

        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)

        try:
            w = self.width()
            h = self.height()
            cx = w / 2.0
            cy = h / 2.0
            r = min(w, h) * 0.46

            p.fillRect(self.rect(), QColor(12, 12, 12))

            # -------------------------------------------------
            # CLIP CIRCULAR
            # -------------------------------------------------
            clip = QPainterPath()
            clip.addEllipse(QPointF(cx, cy), r, r)

            p.save()
            p.setClipPath(clip)
            p.translate(cx, cy)

            # En un horizonte artificial, el fondo rota contrario al avion.
            p.rotate(-self.roll_display)

            offset = (self.pitch_display / 10.0) * self.pixels_per_10deg

            sky = QColor(70, 150, 255)
            ground = QColor(150, 90, 45)

            # Cielo y tierra. Con pitch positivo, el horizonte baja: mas cielo visible.
            p.fillRect(-3000, -3000, 6000, 3000 + offset, sky)
            p.fillRect(-3000, offset, 6000, 3000, ground)

            # Linea de horizonte.
            p.setPen(QPen(Qt.white, 4))
            p.drawLine(-3000, offset, 3000, offset)

            # Marcas de pitch.
            p.setFont(QFont("Consolas", 10))
            for deg in range(-90, 91, 10):
                y = offset - (deg / 10.0) * self.pixels_per_10deg

                if abs(y) > r + 30:
                    continue

                lw = 130 if deg % 20 == 0 else 75
                p.setPen(QPen(Qt.white, 2))
                p.drawLine(-lw / 2, y, lw / 2, y)

                if deg != 0:
                    label = str(abs(deg))
                    p.drawText(-lw / 2 - 35, y + 5, label)
                    p.drawText(lw / 2 + 10, y + 5, label)

            p.restore()

            # -------------------------------------------------
            # MARCO Y ESCALA DE ROLL
            # -------------------------------------------------
            p.setPen(QPen(Qt.white, 3))
            p.setBrush(Qt.NoBrush)
            p.drawEllipse(QPointF(cx, cy), r, r)

            p.save()
            p.translate(cx, cy)

            for ang in range(-60, 61, 15):
                p.save()
                p.rotate(ang)
                length = 22 if ang in (-60, -30, 0, 30, 60) else 14
                p.setPen(QPen(Qt.white, 2))
                p.drawLine(0, -r, 0, -r + length)
                p.restore()

            p.restore()

            # Avion fijo.
            p.setPen(QPen(QColor(255, 210, 0), 5))
            p.drawLine(cx - 105, cy, cx - 28, cy)
            p.drawLine(cx + 28, cy, cx + 105, cy)
            p.drawLine(cx, cy - 12, cx, cy + 12)
            p.drawEllipse(QPointF(cx, cy), 5, 5)

            # Triangulo superior fijo.
            tri = QPolygonF([
                QPointF(cx, cy - r - 2),
                QPointF(cx - 11, cy - r + 20),
                QPointF(cx + 11, cy - r + 20),
            ])
            p.setPen(QPen(Qt.white, 2))
            p.setBrush(Qt.white)
            p.drawPolygon(tri)

            # Texto de depuracion/lectura.
            p.setPen(Qt.green)
            p.setFont(QFont("Consolas", 13))
            p.drawText(20, 30, f"PITCH {self.pitch:+07.2f}  DISP {self.pitch_display:+07.2f}")
            p.drawText(20, 52, f"ROLL  {self.roll:+07.2f}  DISP {self.roll_display:+07.2f}")
            p.drawText(20, h - 22, "Flechas: mover | R: reset | Esc: salir")

        finally:
            p.end()


# =====================================================
# MAIN WINDOW
# =====================================================

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("Horizonte Artificial Revisado")
        self.resize(700, 700)

        self.horizon = ArtificialHorizon()
        self.setCentralWidget(self.horizon)
        self.setFocusPolicy(Qt.StrongFocus)

        self.pitch = 0.0
        self.roll = 0.0

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh)
        self.timer.start(16)

    def refresh(self):
        self.horizon.set_attitude(self.pitch, self.roll)

    def keyPressEvent(self, e):
        k = e.key()

        if k == Qt.Key_Up:
            self.pitch += 2.0
        elif k == Qt.Key_Down:
            self.pitch -= 2.0
        elif k == Qt.Key_Left:
            self.roll += 3.0
        elif k == Qt.Key_Right:
            self.roll -= 3.0
        elif k == Qt.Key_R:
            self.pitch = 0.0
            self.roll = 0.0
        elif k == Qt.Key_Escape:
            self.close()
            return
        else:
            super().keyPressEvent(e)
            return

        self.pitch = norm_180(self.pitch)
        self.roll = norm_180(self.roll)
        self.refresh()


# =====================================================
# RUN
# =====================================================

if __name__ == "__main__":
    app = QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())
