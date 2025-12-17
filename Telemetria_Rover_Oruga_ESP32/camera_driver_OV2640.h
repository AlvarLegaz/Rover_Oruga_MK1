#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * @file camera_driver_OV2640.h
 * @brief Interfaz de inicialización y captura de imágenes para la cámara OV2640 en ESP32-CAM.
 *
 * Este módulo proporciona funciones para configurar e inicializar el sensor OV2640
 * y capturar imágenes en formato JPEG desde el framebuffer de la ESP32.
 *
 * La implementación completa está en `camera_driver_OV2640.cpp`.
 */

/**
 * @brief Inicializa el sensor OV2640 con una configuración adaptativa en función de la PSRAM.
 *
 * Configura los pines, formato de imagen, resolución y calidad JPEG. Si se detecta PSRAM,
 * se mejora la calidad y el número de framebuffers.
 *
 * @return `true` si la inicialización fue exitosa, `false` en caso de error.
 */
bool initCamera();

/**
 * @brief Captura una imagen desde la cámara y devuelve los datos JPEG en memoria dinámica.
 *
 * Esta función reserva memoria con `malloc()` para copiar el contenido del framebuffer.
 * El buffer devuelto debe ser liberado manualmente por el usuario con `free()`.
 *
 * @param[out] out_buf     Puntero al buffer JPEG (reservado dinámicamente)
 * @param[out] out_len     Longitud del buffer JPEG en bytes
 * @param[out] out_width   Ancho de la imagen capturada
 * @param[out] out_height  Alto de la imagen capturada
 * @return `ESP_OK` si la captura fue exitosa, `ESP_FAIL` si hubo error o falta de memoria.
 */
esp_err_t getImage(uint8_t** out_buf, size_t* out_len, uint16_t* out_width, uint16_t* out_height);
