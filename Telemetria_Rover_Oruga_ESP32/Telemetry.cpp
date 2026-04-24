// Telemetry.cpp

#include "Telemetry.h"
#include "freertos/task.h"

static void telemetryTask(void* param) {

    Telemetry* self = static_cast<Telemetry*>(param);

    while (true) {

        self->updateInputs();

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

Telemetry::Telemetry()
    : temp(0.0f),
      bat(0.0f),
      mutex(nullptr) {
}

void Telemetry::begin() {

    initGPS(13, 12, 38400);

    mutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(
        telemetryTask,
        "telemetryTask",
        4096,
        this,
        1,
        nullptr,
        1
    );
}

void Telemetry::updateInputs() {

    float newTemp = 24.0f + (millis() % 1000) / 100.0f;
    float newBat  = 12.6f - (millis() % 500) / 1000.0f;
    GPSData newGps = getGPSData();

    xSemaphoreTake(mutex, portMAX_DELAY);

    temp = newTemp;
    bat  = newBat;
    gps  = newGps;

    xSemaphoreGive(mutex);
}

void Telemetry::toJSON(char* buffer, size_t size) {

    float outTemp;
    float outBat;
    GPSData outGps;

    xSemaphoreTake(mutex, portMAX_DELAY);

    outTemp = temp;
    outBat  = bat;
    outGps  = gps;

    xSemaphoreGive(mutex);

    if (outGps.fix) {

        snprintf(buffer, size,
            "{"
            "\"temp\":%.1f,"
            "\"bat\":%.2f,"
            "\"gps\":{"
            "\"fix\":true,"
            "\"lat\":%.6f,"
            "\"lon\":%.6f,"
            "\"speed\":%.1f,"
            "\"course\":%.1f,"
            "\"dir\":\"%s\","
            "\"sats\":%d"
            "}"
            "}",
            outTemp,
            outBat,
            outGps.lat,
            outGps.lon,
            outGps.speed,
            outGps.course,
            courseToText(outGps.course),
            outGps.sats
        );

    } else {

        snprintf(buffer, size,
            "{"
            "\"temp\":%.1f,"
            "\"bat\":%.2f,"
            "\"gps\":{"
            "\"fix\":false,"
            "\"lat\":null,"
            "\"lon\":null,"
            "\"speed\":0,"
            "\"course\":0,"
            "\"dir\":\"---\","
            "\"sats\":%d"
            "}"
            "}",
            outTemp,
            outBat,
            outGps.sats
        );
    }
}