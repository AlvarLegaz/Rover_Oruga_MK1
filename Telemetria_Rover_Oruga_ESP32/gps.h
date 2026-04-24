#ifndef GPS_TELEMETRY_H
#define GPS_TELEMETRY_H

#include <Arduino.h>
#include <HardwareSerial.h>

// ============================
// Estructura pública de datos
// ============================
struct GPSData {
    float lat = 0.0f;
    float lon = 0.0f;
    float speed = 0.0f;     // km/h
    float course = 0.0f;    // grados
    int sats = 0;
    bool fix = false;
};

// ============================
// API pública del módulo GPS
// ============================

// Inicializa UART y lanza tarea GPS
void initGPS(int rxPin, int txPin, uint32_t baud = 38400);
// Devuelve snapshot seguro del estado GPS
GPSData getGPSData();
// Conversión rumbo grados -> texto
const char* courseToText(float deg);

#endif