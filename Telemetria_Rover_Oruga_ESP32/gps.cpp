#include "gps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

HardwareSerial GPS(2);

// ============================
// Estado privado del módulo
// ============================
static GPSData gpsData;
static SemaphoreHandle_t gpsMutex = nullptr;

static String line = "";

// ============================
// Utilidades internas
// ============================
static float convLat(const String& v, const String& h) {
    if (v.length() < 4) return 0.0f;

    float x = v.substring(0, 2).toFloat() +
              v.substring(2).toFloat() / 60.0f;

    if (h == "S") x = -x;
    return x;
}

static float convLon(const String& v, const String& h) {
    if (v.length() < 5) return 0.0f;

    float x = v.substring(0, 3).toFloat() +
              v.substring(3).toFloat() / 60.0f;

    if (h == "W") x = -x;
    return x;
}

static int splitNMEA(const String& s, String* t, int maxFields) {
    int n = 0;
    int last = 0;

    for (int i = 0; i < s.length() && n < maxFields; i++) {
        if (s[i] == ',') {
            t[n++] = s.substring(last, i);
            last = i + 1;
        }
    }

    if (n < maxFields) {
        t[n++] = s.substring(last);
    }

    return n;
}

static int twoDigitsToInt(const String& s, int start) {
    if (s.length() < start + 2) return 0;
    return s.substring(start, start + 2).toInt();
}

// ============================
// API pública
// ============================
const char* courseToText(float deg) {
    static const char* dir[] = {
        "N", "NE", "E", "SE",
        "S", "SO", "O", "NO"
    };

    return dir[(int)((deg + 22.5f) / 45.0f) % 8];
}

GPSData getGPSData() {
    GPSData copy;

    xSemaphoreTake(gpsMutex, portMAX_DELAY);
    copy = gpsData;
    xSemaphoreGive(gpsMutex);

    return copy;
}

// ============================
// Parser NMEA
// ============================
static void parseNMEA(const String& s) {

    // GLL: latitud, longitud y estado de fix
    if (s.startsWith("$GNGLL") || s.startsWith("$GPGLL")) {

        int p[8], n = 0;

        for (int i = 0; i < s.length() && n < 8; i++) {
            if (s[i] == ',') p[n++] = i;
        }

        if (n >= 6) {
            xSemaphoreTake(gpsMutex, portMAX_DELAY);

            gpsData.lat = convLat(
                s.substring(p[0] + 1, p[1]),
                s.substring(p[1] + 1, p[2])
            );

            gpsData.lon = convLon(
                s.substring(p[2] + 1, p[3]),
                s.substring(p[3] + 1, p[4])
            );

            gpsData.fix =
                (s.substring(p[5] + 1, p[6]) == "A");

            xSemaphoreGive(gpsMutex);
        }
    }

    // RMC: hora UTC, fecha UTC, velocidad, rumbo y estado de fix
    if (s.startsWith("$GNRMC") || s.startsWith("$GPRMC")) {

        String t[16];
        int n = splitNMEA(s, t, 16);

        if (n >= 10) {
            String utcTime = t[1];  // hhmmss.sss
            String status  = t[2];  // A = valido, V = no valido
            String utcDate = t[9];  // ddmmyy

            xSemaphoreTake(gpsMutex, portMAX_DELAY);

            gpsData.speed = t[7].toFloat() * 1.852f;  // nudos a km/h
            gpsData.course = t[8].toFloat();
            gpsData.fix = (status == "A");

            gpsData.utcValid = false;

            if (utcTime.length() >= 6) {
                gpsData.utcHour = twoDigitsToInt(utcTime, 0);
                gpsData.utcMinute = twoDigitsToInt(utcTime, 2);
                gpsData.utcSecond = twoDigitsToInt(utcTime, 4);
                gpsData.utcValid = true;
            }

            if (utcDate.length() >= 6) {
                gpsData.utcDay = twoDigitsToInt(utcDate, 0);
                gpsData.utcMonth = twoDigitsToInt(utcDate, 2);
                gpsData.utcYear = 2000 + twoDigitsToInt(utcDate, 4);
            }

            xSemaphoreGive(gpsMutex);
        }
    }

    // GSV: satelites visibles
    if (s.startsWith("$GPGSV") || s.startsWith("$GNGSV")) {

        String t[6];
        int n = splitNMEA(s, t, 6);

        if (n >= 4) {
            xSemaphoreTake(gpsMutex, portMAX_DELAY);
            gpsData.sats = t[3].toInt();
            xSemaphoreGive(gpsMutex);
        }
    }
}

// ============================
// Tarea GPS
// ============================
static void gpsTask(void* p) {

    while (true) {

        while (GPS.available()) {

            char c = GPS.read();

            if (c == '\n') {
                parseNMEA(line);
                line = "";
            }
            else if (c != '\r') {
                line += c;
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ============================
// Inicialización
// ============================
void initGPS(int rxPin, int txPin, uint32_t baud) {

    gpsMutex = xSemaphoreCreateMutex();

    GPS.begin(
        baud,
        SERIAL_8N1,
        rxPin,
        txPin
    );

    xTaskCreatePinnedToCore(
        gpsTask,
        "gpsTask",
        4096,
        nullptr,
        1,
        nullptr,
        1
    );
}
