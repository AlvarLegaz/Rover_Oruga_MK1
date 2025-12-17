import tkinter as tk
from tkinter import Entry, Button, Label, Frame
from PIL import Image, ImageTk
import requests
from io import BytesIO

FIXED_IMAGE_WIDTH = 640
FIXED_IMAGE_HEIGHT = 480

class Visor:
    def __init__(self, root):
        self.root = root
        self.root.title("Visor Rover-Oruga")

        # Frame superior para la IP y botón
        top_frame = Frame(root)
        top_frame.pack(pady=5)

        self.ip_label = Label(top_frame, text="IP Modulo Telemetría:")
        self.ip_label.pack(side="left")
        self.ip_entry = Entry(top_frame, width=20)
        self.ip_entry.pack(side="left", padx=5)
        self.ip_entry.insert(0, "192.168.4.1")  # ejemplo

        self.start_button = Button(top_frame, text="Iniciar", command=self.start_stream)
        self.start_button.pack(side="left", padx=5)

        # Frame principal para imagen y telemetría
        main_frame = Frame(root)
        main_frame.pack(pady=10)

        # Label para mostrar la imagen
        self.image_label = Label(main_frame)
        self.image_label.pack(side="left")

        # Label para telemetría al lateral
        self.telemetry_display = Label(main_frame, text="", justify="left",
                                       font=("Courier", 10), anchor="nw")
        self.telemetry_display.pack(side="left", padx=10)

        self.update_job = None
        self.running = False

    def start_stream(self):
        self.running = True
        self.update_frame()

    def update_frame(self):
        if not self.running:
            return

        ip = self.ip_entry.get()
        cam_url = f"http://{ip}/capture"
        telemetry_url = f"http://{ip}/telemetry"

        # Mostrar imagen
        try:
            response = requests.get(cam_url, timeout=0.5)
            response.raise_for_status()
            img_data = response.content
            img = Image.open(BytesIO(img_data))

            # Redimensionar a tamaño fijo (puede distorsionar si no mantiene proporción)
            img = img.resize((FIXED_IMAGE_WIDTH, FIXED_IMAGE_HEIGHT))

            imgtk = ImageTk.PhotoImage(image=img)
            self.image_label.imgtk = imgtk
            self.image_label.configure(image=imgtk)
        except requests.RequestException:
            self.image_label.configure(text="No se pudo conectar", image="")

        # Mostrar telemetría al lateral
        try:
            response = requests.get(telemetry_url, timeout=0.5)
            response.raise_for_status()
            data = response.json()
            text = ""
            for key, value in data.items():
                if isinstance(value, dict):
                    text += f"{key}:\n"
                    for subkey, subval in value.items():
                        text += f"  {subkey}: {subval}\n"
                else:
                    text += f"{key}: {value}\n"
            self.telemetry_display.configure(text=text)
        except requests.RequestException:
            self.telemetry_display.configure(text="No se pudo obtener telemetría")

        # Actualizar cada 200 ms
        self.update_job = self.root.after(200, self.update_frame)

    def stop_stream(self):
        self.running = False
        if self.update_job:
            self.root.after_cancel(self.update_job)

    def on_close(self):
        self.stop_stream()
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    viewer = Visor(root)
    root.protocol("WM_DELETE_WINDOW", viewer.on_close)
    root.mainloop()
