// Telemetry.cpp

#include "Telemetry.h"
#include "freertos/task.h"
#include "imu.h"

#define TELEMETRY_INTERVAL_MS 1000 

#define GPS_BAUDRATE 9600 // G28U7FTTL

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

static void formatGPSUtc(const GPSData& gps, char* out, size_t outSize) {

    if (!gps.utcValid) {
        snprintf(out, outSize, "");
        return;
    }

    snprintf(
        out,
        outSize,
        "%04d-%02d-%02dT%02d:%02d:%02dZ",
        gps.utcYear,
        gps.utcMonth,
        gps.utcDay,
        gps.utcHour,
        gps.utcMinute,
        gps.utcSecond
    );
}

static void formatGPSUtcJson(const GPSData& gps, char* out, size_t outSize) {

    if (!gps.utcValid) {
        snprintf(out, outSize, "null");
        return;
    }

    char utcText[24];
    formatGPSUtc(gps, utcText, sizeof(utcText));
    snprintf(out, outSize, "\"%s\"", utcText);
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

    initGPS(13, 2, GPS_BAUDRATE);
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
    char uptimeText[24];
    formatUptime(uptimeMs, uptimeText, sizeof(uptimeText));

    char gpsUtcJson[32];
    formatGPSUtcJson(gps, gpsUtcJson, sizeof(gpsUtcJson));

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
        "\"sats\":%d,"
        "\"utcValid\":%s,"
        "\"utc\":%s,"
        "\"utcHour\":%d,"
        "\"utcMinute\":%d,"
        "\"utcSecond\":%d,"
        "\"utcDay\":%d,"
        "\"utcMonth\":%d,"
        "\"utcYear\":%d"
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
        gps.utcValid ? "true" : "false",
        gpsUtcJson,
        gps.utcHour,
        gps.utcMinute,
        gps.utcSecond,
        gps.utcDay,
        gps.utcMonth,
        gps.utcYear,

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
    GPSData outGps;

    xSemaphoreTake(mutex, portMAX_DELAY);
    outGps = gps;
    xSemaphoreGive(mutex);

    char gpsUtcJson[32];
    formatGPSUtcJson(outGps, gpsUtcJson, sizeof(gpsUtcJson));

    if (outGps.fix) {

        snprintf(buffer, size,
            "{"
            "\"fix\":true,"
            "\"lat\":%.6f,"
            "\"lon\":%.6f,"
            "\"speed\":%.1f,"
            "\"course\":%.1f,"
            "\"dir\":\"%s\","
            "\"sats\":%d,"
            "\"utcValid\":%s,"
            "\"utc\":%s,"
            "\"utcHour\":%d,"
            "\"utcMinute\":%d,"
            "\"utcSecond\":%d,"
            "\"utcDay\":%d,"
            "\"utcMonth\":%d,"
            "\"utcYear\":%d"
            "}",
            outGps.lat,
            outGps.lon,
            outGps.speed,
            outGps.course,
            courseToText(outGps.course),
            outGps.sats,
            outGps.utcValid ? "true" : "false",
            gpsUtcJson,
            outGps.utcHour,
            outGps.utcMinute,
            outGps.utcSecond,
            outGps.utcDay,
            outGps.utcMonth,
            outGps.utcYear
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
            "\"sats\":%d,"
            "\"utcValid\":%s,"
            "\"utc\":%s,"
            "\"utcHour\":%d,"
            "\"utcMinute\":%d,"
            "\"utcSecond\":%d,"
            "\"utcDay\":%d,"
            "\"utcMonth\":%d,"
            "\"utcYear\":%d"
            "}",
            outGps.sats,
            outGps.utcValid ? "true" : "false",
            gpsUtcJson,
            outGps.utcHour,
            outGps.utcMinute,
            outGps.utcSecond,
            outGps.utcDay,
            outGps.utcMonth,
            outGps.utcYear
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
