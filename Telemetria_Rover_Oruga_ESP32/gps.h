#ifndef GPS_TELEMETRY_H
#define GPS_TELEMETRY_H

#include <Arduino.h>
#include <HardwareSerial.h>

// Variables públicas
extern float gps_lat;
extern float gps_lon;
extern float gps_speed;     // km/h
extern float gps_course;    // grados
extern int   gps_sats;
extern bool  gps_fix;

// UART GPS
extern HardwareSerial GPS;

// Funciones
void initGPS(int rxPin, int txPin, uint32_t baud = 38400);
void gpsTask(void *pvParameters);
void parseNMEA(String line);

// Utilidades
const char* courseToText(float deg);

#endif