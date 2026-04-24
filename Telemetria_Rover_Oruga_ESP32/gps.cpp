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
    float x = v.substring(0, 2).toFloat() +
              v.substring(2).toFloat() / 60.0f;

    if (h == "S") x = -x;
    return x;
}

static float convLon(const String& v, const String& h) {
    float x = v.substring(0, 3).toFloat() +
              v.substring(3).toFloat() / 60.0f;

    if (h == "W") x = -x;
    return x;
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

    if (s.startsWith("$GNGLL")) {

        int p[8], n = 0;

        for (int i = 0; i < s.length() && n < 8; i++) {
            if (s[i] == ',') p[n++] = i;
        }

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

    if (s.startsWith("$GNRMC")) {

        String t[16];
        int n = 0;
        int last = 0;

        for (int i = 0; i < s.length(); i++) {
            if (s[i] == ',') {
                t[n++] = s.substring(last, i);
                last = i + 1;
            }
        }

        xSemaphoreTake(gpsMutex, portMAX_DELAY);

        gpsData.speed = t[7].toFloat() * 1.852f;
        gpsData.course = t[8].toFloat();

        xSemaphoreGive(gpsMutex);
    }

    if (s.startsWith("$GPGSV") || s.startsWith("$GNGSV")) {

        String t[6];
        int n = 0;
        int last = 0;

        for (int i = 0; i < s.length(); i++) {
            if (s[i] == ',') {
                t[n++] = s.substring(last, i);
                last = i + 1;
                if (n >= 4) break;
            }
        }

        xSemaphoreTake(gpsMutex, portMAX_DELAY);
        gpsData.sats = t[3].toInt();
        xSemaphoreGive(gpsMutex);
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