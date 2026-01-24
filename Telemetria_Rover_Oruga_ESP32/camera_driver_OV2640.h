#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_camera.h"

// Inicializa el sensor con configuración optimizada
bool initCamera();

// Obtiene el frame directamente de la memoria de la cámara
camera_fb_t* getCameraFrame();

// Devuelve el frame a la cámara para liberar memoria
void releaseCameraFrame(camera_fb_t* fb);