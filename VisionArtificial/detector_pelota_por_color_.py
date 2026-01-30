import cv2
import numpy as np

# ====== STREAM DEL ESP32 ======
url = "http://192.168.1.97/stream"  # ← CAMBIA ESTA IP

cap = cv2.VideoCapture(url)

if not cap.isOpened():
    print("No se pudo abrir el stream")
    exit()

while True:
    ret, frame = cap.read()
    if not ret:
        break

    frame = cv2.resize(frame, (640, 480))
    h, w = frame.shape[:2]

    center_frame = (w // 2, h // 2)

    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)

    lower_red1 = np.array([0, 120, 70])
    upper_red1 = np.array([10, 255, 255])
    lower_red2 = np.array([170, 120, 70])
    upper_red2 = np.array([180, 255, 255])

    mask = cv2.inRange(hsv, lower_red1, upper_red1) + \
           cv2.inRange(hsv, lower_red2, upper_red2)

    kernel = np.ones((5, 5), np.uint8)
    mask = cv2.erode(mask, kernel, 2)
    mask = cv2.dilate(mask, kernel, 2)
    mask = cv2.GaussianBlur(mask, (9, 9), 0)

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    # Dibujar centro de la imagen
    cv2.circle(frame, center_frame, 5, (0, 255, 255), -1)

    if contours:
        c = max(contours, key=cv2.contourArea)
        area = cv2.contourArea(c)

        if area > 500:
            ((x, y), radius) = cv2.minEnclosingCircle(c)

            if radius > 10:
                center_ball = (int(x), int(y))

                # ====== CALCULAR PORCENTAJES ======
                percent_x = ((x - center_frame[0]) / (w / 2)) * 100
                percent_y = ((y - center_frame[1]) / (h / 2)) * 100

                percent_x = int(np.clip(percent_x, -100, 100))
                percent_y = int(np.clip(percent_y, -100, 100))

                # ====== DIBUJOS ======
                cv2.circle(frame, center_ball, int(radius), (0, 255, 0), 2)
                cv2.circle(frame, center_ball, 5, (255, 0, 0), -1)

                text = f"X:{percent_x}%  Y:{percent_y}%"
                cv2.putText(frame, text,
                            (center_ball[0] - 60, center_ball[1] - 20),
                            cv2.FONT_HERSHEY_SIMPLEX,
                            0.6, (0, 255, 0), 2)

                # Línea al centro (visual de error)
                cv2.line(frame, center_frame, center_ball, (255, 255, 0), 2)

                print(text)

    cv2.imshow("Frame", frame)
    cv2.imshow("Mask Roja", mask)

    if cv2.waitKey(1) & 0xFF == 27:
        break

cap.release()
cv2.destroyAllWindows()