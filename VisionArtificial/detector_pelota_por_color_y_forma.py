import cv2
import numpy as np

# ====== STREAM DEL ESP32 ======
url = "http://192.168.1.97/stream"   # ← IP de tu cámara

cap = cv2.VideoCapture(url)

if not cap.isOpened():
    print("No se pudo abrir el stream")
    exit()

while True:
    ret, frame = cap.read()
    if not ret:
        break

    # Redimensionar para rendimiento
    frame = cv2.resize(frame, (640, 480))
    h, w = frame.shape[:2]

    # Centro de la imagen
    center_frame = (w // 2, h // 2)

    # ====== CONVERSIÓN A HSV ======
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

    # ====== RANGO DE COLOR ROJO ======
    lower_red1 = np.array([0, 120, 70])
    upper_red1 = np.array([10, 255, 255])
    lower_red2 = np.array([170, 120, 70])
    upper_red2 = np.array([180, 255, 255])

    mask = cv2.inRange(hsv, lower_red1, upper_red1) + \
           cv2.inRange(hsv, lower_red2, upper_red2)

    # ====== LIMPIEZA DE RUIDO ======
    kernel = np.ones((5, 5), np.uint8)
    # Filtro para ruido impulsivo. erosion + dilatacion = apertura morfologica. Esto hace que se eliminen elementos que no pueden contener al SE (kernel)
    mask = cv2.erode(mask, kernel, 2)
    mask = cv2.dilate(mask, kernel, 2)
    # Ahora que no hay ruido impulsivo ya se puede hacer el filtro de media para ruido gaussiano.
    mask = cv2.GaussianBlur(mask, (9, 9), 0)

    # ====== BUSCAR CONTORNOS ======
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    # Dibujar centro de referencia
    cv2.circle(frame, center_frame, 5, (0, 255, 255), -1)

    if contours:
        c = max(contours, key=cv2.contourArea)
        area = cv2.contourArea(c)

        if area > 500:

            # ====== VERIFICACIÓN DE FORMA CIRCULAR ======
            perimeter = cv2.arcLength(c, True)

            if perimeter > 0:
                circularity = 4 * np.pi * area / (perimeter * perimeter)

                # 1 = círculo perfecto
                if circularity > 0.7:  # <- AQUÍ INDICAMOS CUANTA CIRCULARIDAD SE CONSIDERA UNA PELOTA

                    ((x, y), radius) = cv2.minEnclosingCircle(c)

                    if radius > 10:
                        center_ball = (int(x), int(y))

                        # ====== POSICIÓN EN PORCENTAJE ======
                        percent_x = int(np.clip(((x - center_frame[0]) / (w / 2)) * 100, -100, 100))
                        percent_y = int(np.clip(((y - center_frame[1]) / (h / 2)) * 100, -100, 100))

                        # ====== DIBUJOS ======
                        cv2.circle(frame, center_ball, int(radius), (0, 255, 0), 2)
                        cv2.circle(frame, center_ball, 5, (255, 0, 0), -1)

                        text = f"Cir:{circularity:.2f} X:{percent_x}%  Y:{percent_y}%"
                        cv2.putText(frame, text,
                                    (center_ball[0] - 60, center_ball[1] - 20),
                                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

                        # Línea de error al centro
                        cv2.line(frame, center_frame, center_ball, (255, 255, 0), 2)

                        print(text)

    cv2.imshow("Frame", frame)
    cv2.imshow("Mascara Roja", mask)

    if cv2.waitKey(1) & 0xFF == 27:  # ESC para salir
        break

cap.release()
cv2.destroyAllWindows()