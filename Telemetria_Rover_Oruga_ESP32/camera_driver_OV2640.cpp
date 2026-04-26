#include "camera_driver_OV2640.h"
#include "Arduino.h"
#include "esp_camera.h"

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

bool initCamera()
{
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

    config.xclk_freq_hz   = 24000000;
    config.ledc_timer     = LEDC_TIMER_0;
    config.ledc_channel   = LEDC_CHANNEL_0;
    config.pixel_format   = PIXFORMAT_JPEG;

    // ---------------------------------
    // Config memoria
    // ---------------------------------

    if (psramFound()) {

        config.frame_size   = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count     = 2;
        config.grab_mode    = CAMERA_GRAB_LATEST;

    } else {

        config.frame_size   = FRAMESIZE_CIF;
        config.jpeg_quality = 15;
        config.fb_count     = 1;
        config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
    }

    // ---------------------------------
    // Init cámara
    // ---------------------------------

    esp_err_t err = esp_camera_init(&config);

    if (err != ESP_OK) {
        Serial.printf("Error de camara: 0x%x\n", err);
        return false;
    }

    // ---------------------------------
    // ROTAR 180º
    // ---------------------------------

    sensor_t* s = esp_camera_sensor_get();

    if (s) {
        s->set_hmirror(s, 1);   // espejo horizontal
        s->set_vflip(s, 1);     // vertical
    }

    Serial.println("Camara iniciada OK (rotada 180)");

    return true;
}

// ==================================================
// Frame buffer
// ==================================================

camera_fb_t* getCameraFrame()
{
    return esp_camera_fb_get();
}

void releaseCameraFrame(camera_fb_t* fb)
{
    if (fb) esp_camera_fb_return(fb);
}

// ==================================================
// LOW STREAM
// ==================================================

void setCameraLowStreamMode()
{
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;

    s->set_framesize(s, FRAMESIZE_HQVGA);   // FRAMESIZE_QVGA: 320x240 FRAMESIZE_HQVGA 240x176
    s->set_quality(s, 22);

    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);

    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_awb_gain(s, 1);

    s->set_gainceiling(s, GAINCEILING_2X);

    // mantener rotación
    s->set_hmirror(s, 1);
    s->set_vflip(s, 1);

    Serial.println("STREAM LOW optimizado + rotado");
}

// ==================================================
// HIGH STREAM
// ==================================================

void setCameraNormalMode()
{
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;

    s->set_framesize(s, FRAMESIZE_HVGA);   // FRAMESIZE_HVGA: 480x320 FRAMESIZE_VGA: 640x480
    s->set_quality(s, 20);

    s->set_saturation(s, 0);
    s->set_gainceiling(s, GAINCEILING_4X);

    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_awb_gain(s, 1);

    // mantener rotación
    s->set_hmirror(s, 1);
    s->set_vflip(s, 1);

    Serial.println("STREAM HIGH optimizado + rotado");
}