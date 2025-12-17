#include "camera_driver_OV2640.h"
#include "esp_camera.h"
#include "Arduino.h"
#include <stdlib.h>
#include <string.h>

// Definición de pines de la cámara (ESP32-CAM AI Thinker)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

bool initCamera() {
  
  camera_config_t config;

  config.pin_pwdn       = PWDN_GPIO_NUM;
  config.pin_reset      = RESET_GPIO_NUM;
  config.pin_xclk       = XCLK_GPIO_NUM;
  config.pin_sccb_sda   = SIOD_GPIO_NUM;
  config.pin_sccb_scl   = SIOC_GPIO_NUM;

  config.pin_d0         = Y2_GPIO_NUM;
  config.pin_d1         = Y3_GPIO_NUM;
  config.pin_d2         = Y4_GPIO_NUM;
  config.pin_d3         = Y5_GPIO_NUM;
  config.pin_d4         = Y6_GPIO_NUM;
  config.pin_d5         = Y7_GPIO_NUM;
  config.pin_d6         = Y8_GPIO_NUM;
  config.pin_d7         = Y9_GPIO_NUM;

  config.pin_vsync      = VSYNC_GPIO_NUM;
  config.pin_href       = HREF_GPIO_NUM;
  config.pin_pclk       = PCLK_GPIO_NUM;

  config.xclk_freq_hz   = 20000000;
  config.ledc_timer     = LEDC_TIMER_0;
  config.ledc_channel   = LEDC_CHANNEL_0;

  config.pixel_format   = PIXFORMAT_JPEG;
  config.frame_size     = FRAMESIZE_UXGA;
  config.jpeg_quality   = 12;
  config.fb_count       = 1;
  config.fb_location    = CAMERA_FB_IN_PSRAM;
  config.grab_mode      = CAMERA_GRAB_WHEN_EMPTY;

  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_QVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_QVGA;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%X\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

  return true;
}

esp_err_t getImage(uint8_t** out_buf, size_t* out_len, uint16_t* out_width, uint16_t* out_height) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Error: fallo en la captura de imagen");
    return ESP_FAIL;
  }

  *out_buf = (uint8_t*)malloc(fb->len);
  if (*out_buf == nullptr) {
    Serial.println("Error: malloc falló al reservar memoria");
    esp_camera_fb_return(fb);
    return ESP_FAIL;
  }

  memcpy(*out_buf, fb->buf, fb->len);
  *out_len = fb->len;
  *out_width = fb->width;
  *out_height = fb->height;

  //Serial.printf("Imagen capturada: %u x %u, %u bytes\n", fb->width, fb->height, fb->len);

  // (Opcional) Hex dump para depuración
  /*
  for (size_t i = 0; i < fb->len; ++i) {
    Serial.printf("%02X ", fb->buf[i]);
    if ((i + 1) % 16 == 0) Serial.println();
  }
  Serial.println();
*/
  esp_camera_fb_return(fb);
  return ESP_OK;
}
