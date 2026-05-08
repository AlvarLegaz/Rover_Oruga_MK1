// Telemetry.cpp

#include "Telemetry.h"
#include "freertos/task.h"
#include "imu.h"

#define TELEMETRY_INTERVAL_MS 1000 

static void formatUptime(unsigned long uptimeMs, char* out, size_t outSize) {

    unsigned long totalSeconds = uptimeMs / 1000UL;
    unsigned long seconds = totalSeconds % 60UL;
    unsigned long minutes = (totalSeconds / 60UL) % 60UL;
    unsigned long hours = (totalSeconds / 3600UL) % 24UL;
    unsigned long days = totalSeconds / 86400UL;

    snprintf(
        out,
        outSize,
        "%lud %02lu:%02lu:%02lu",
        days,
        hours,
        minutes,
        seconds
    );
}

static void telemetryTask(void* param) {

    Telemetry* self = static_cast<Telemetry*>(param);

    while (true) {

        self->updateInputs();

        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS));
    }
}

Telemetry::Telemetry()
    : temp(0.0f),
      bat(0.0f),
      mutex(nullptr) {
}

void Telemetry::begin() {

    initGPS(13, 2, 38400);
    initIMU(14,15);

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
    updateIMU();
    //IMUData imu = getIMUData();

    xSemaphoreTake(mutex, portMAX_DELAY);

    temp = newTemp;
    bat  = newBat;
    gps  = newGps;

    xSemaphoreGive(mutex);
}

void Telemetry::toJSON(char* buffer, size_t size)
{
    IMUData imu = getIMUData();

    unsigned long uptimeMs = millis();
    unsigned long uptimeS = uptimeMs / 1000UL;
    char uptimeText[24];
    formatUptime(uptimeMs, uptimeText, sizeof(uptimeText));

    snprintf(buffer, size,
        "{"
        "\"temp\":%.1f,"
        "\"bat\":%.2f,"

        "\"uptime\":\"%s\","

        "\"gps\":{"
        "\"fix\":%s,"
        "\"lat\":%.6f,"
        "\"lon\":%.6f,"
        "\"speed\":%.1f,"
        "\"course\":%.1f,"
        "\"dir\":\"%s\","
        "\"sats\":%d"
        "},"

        "\"imu\":{"
        "\"roll\":%.2f,"
        "\"pitch\":%.2f,"
        "\"yaw\":%.2f,"
        "\"alt\":%.2f,"
        "\"pres\":%.2f"
        "}"

        "}",

        imu.temp,
        bat,
        uptimeText,

        gps.fix ? "true" : "false",
        gps.lat,
        gps.lon,
        gps.speed,
        gps.course,
        courseToText(gps.course),
        gps.sats,

        imu.roll,
        imu.pitch,
        imu.yaw,
        imu.altitude,
        imu.pressure
    );
}

void Telemetry::toJSONSystem(char* buffer, size_t size)
{
    IMUData imu = getIMUData();

    unsigned long uptimeMs = millis();
    unsigned long uptimeS = uptimeMs / 1000UL;
    char uptimeText[24];
    formatUptime(uptimeMs, uptimeText, sizeof(uptimeText));

    snprintf(buffer, size,
        "{"
        "\"temp\":%.1f,"
        "\"bat\":%.2f,"
        "\"uptime\":\"%s\""
        "}",
        imu.temp,
        bat,
        uptimeText
    );
}

void Telemetry::toJSONGPS(char* buffer, size_t size)
{
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
            "\"fix\":true,"
            "\"lat\":%.6f,"
            "\"lon\":%.6f,"
            "\"speed\":%.1f,"
            "\"course\":%.1f,"
            "\"dir\":\"%s\","
            "\"sats\":%d"
            "}",
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
            "\"fix\":false,"
            "\"lat\":null,"
            "\"lon\":null,"
            "\"speed\":0,"
            "\"course\":0,"
            "\"dir\":\"---\","
            "\"sats\":%d"
            "}",
            outGps.sats
        );
    }
}

void Telemetry::toJSONIMU(char* buffer, size_t size)
{
    IMUData imu = getIMUData();

    snprintf(buffer, size,
        "{"
        "\"ok\":true,"
        "\"roll\":%.2f,"
        "\"pitch\":%.2f,"
        "\"yaw\":%.2f,"
        "\"alt\":%.2f,"
        "\"pres\":%.2f,"
        "\"temp\":%.2f"
        "}",
        imu.roll,
        imu.pitch,
        imu.yaw,
        imu.altitude,
        imu.pressure,
        imu.temp
    );
}