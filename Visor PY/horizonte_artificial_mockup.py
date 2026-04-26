# horizonte_profesional.py
# -----------------------------------------
# pip install PySide6
# python horizonte_profesional.py
# -----------------------------------------

import sys
import math
from PySide6.QtCore import Qt, QTimer, QPointF
from PySide6.QtGui import (
    QPainter, QColor, QPen, QPainterPath,
    QPolygonF, QFont
)
from PySide6.QtWidgets import (
    QApplication, QWidget, QMainWindow
)


# =====================================================
# FILTRO SUAVIZADO
# =====================================================

def lerp_angle(a, b, t):
    diff = (b - a + 180) % 360 - 180
    return a + diff * t


# =====================================================
# WIDGET HORIZONTE
# =====================================================

class ArtificialHorizon(QWidget):
    def __init__(self):
        super().__init__()

        self.pitch = 0.0
        self.roll = 0.0

        self.pitch_display = 0.0
        self.roll_display = 0.0

        self.setMinimumSize(520, 520)

    def set_attitude(self, pitch, roll):
        self.pitch = pitch
        self.roll = roll
        self.update()

    def paintEvent(self, event):
        self.pitch_display += (self.pitch - self.pitch_display) * 0.12
        self.roll_display = lerp_angle(self.roll_display, self.roll, 0.12)

        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)

        w = self.width()
        h = self.height()

        cx = w / 2
        cy = h / 2
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

        # roll
        p.rotate(-(self.roll_display % 360))

        pitch_rad = math.radians(self.pitch_display)

        # desplazamiento circular continuo
        pixels_per_10deg = 38
        offset = (self.pitch_display / 10.0) * pixels_per_10deg

        # invertido correcto entre 90 y 270
        inverted = math.cos(pitch_rad) < 0

        if not inverted:
            sky = QColor(70, 150, 255)
            ground = QColor(150, 90, 45)
        else:
            sky = QColor(150, 90, 45)
            ground = QColor(70, 150, 255)

        # cielo
        p.fillRect(-2500, -2500 + offset, 5000, 2500, sky)

        # tierra
        p.fillRect(-2500, offset, 5000, 2500, ground)

        # línea horizonte
        p.setPen(QPen(Qt.white, 4))
        p.drawLine(-3000, offset, 3000, offset)

        # marcas pitch
        for deg in range(-90, 91, 10):
            pixels_per_10deg = 38
            y = offset - (deg / 10) * pixels_per_10deg

            if abs(y) > 320:
                continue

            lw = 120 if deg % 20 == 0 else 70

            p.setPen(QPen(Qt.white, 2))
            p.drawLine(-lw/2, y, lw/2, y)

            if deg != 0:
                p.drawText(-lw/2 - 28, y + 5, str(abs(deg)))
                p.drawText(lw/2 + 8, y + 5, str(abs(deg)))

        p.restore()

        # -------------------------------------------------
        # MARCO
        # -------------------------------------------------
        p.setPen(QPen(Qt.white, 3))
        p.drawEllipse(QPointF(cx, cy), r, r)

        # marcas roll
        p.save()
        p.translate(cx, cy)

        for ang in range(-60, 61, 15):
            p.save()
            p.rotate(ang)
            p.drawLine(0, -r, 0, -r + 16)
            p.restore()

        p.restore()

        # avión fijo
        p.setPen(QPen(QColor(255, 210, 0), 5))
        p.drawLine(cx - 100, cy, cx - 25, cy)
        p.drawLine(cx + 25, cy, cx + 100, cy)
        p.drawEllipse(QPointF(cx, cy), 5, 5)

        # triángulo superior
        tri = QPolygonF([
            QPointF(cx, cy - r - 2),
            QPointF(cx - 10, cy - r + 18),
            QPointF(cx + 10, cy - r + 18),
        ])

        p.setBrush(Qt.white)
        p.drawPolygon(tri)

        # texto
        p.setPen(Qt.green)
        p.setFont(QFont("Consolas", 13))

        p.drawText(20, 30, f"PITCH {self.pitch_display:+07.2f}")
        p.drawText(20, 52, f"ROLL  {self.roll_display:+07.2f}")


# =====================================================
# MAIN WINDOW
# =====================================================

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        self.setWindowTitle("Horizonte Artificial Profesional")
        self.resize(700, 700)

        self.horizon = ArtificialHorizon()
        self.setCentralWidget(self.horizon)

        self.setFocusPolicy(Qt.StrongFocus)

        self.pitch = 0.0
        self.roll = 0.0

        self.timer = QTimer()
        self.timer.timeout.connect(self.refresh)
        self.timer.start(16)

    def refresh(self):
        self.horizon.set_attitude(self.pitch, self.roll)

    def keyPressEvent(self, e):
        k = e.key()

        if k == Qt.Key_Up:
            self.pitch += 2

        elif k == Qt.Key_Down:
            self.pitch -= 2

        elif k == Qt.Key_Left:
            self.roll += 3

        elif k == Qt.Key_Right:
            self.roll -= 3

        elif k == Qt.Key_R:
            self.pitch = 0
            self.roll = 0

        # pitch físico realista
        if self.pitch > 180:
            self.pitch -= 360
        elif self.pitch < -180:
            self.pitch += 360

        # roll continuo
        self.roll = self.roll % 360


# =====================================================
# RUN
# =====================================================

if __name__ == "__main__":
    app = QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())