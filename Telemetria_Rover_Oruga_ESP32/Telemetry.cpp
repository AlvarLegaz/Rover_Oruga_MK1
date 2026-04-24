#include "Telemetry.h"

// Constructor
Telemetry::Telemetry()
{
  temp = 0.0;
  bat = 0.0;
  lat = 0.0;
  lon = 0.0;
  course = 0.0;
  fix = 0;
  sat = 0;
}

// Update sensores básicos
void Telemetry::update(float t, float b)
{
  temp = t;
  bat = b;
}

// Update GPS
void Telemetry::updateGPS(float la, float lo, float c, uint8_t f, uint8_t s) 
{
  lat = la;
  lon = lo;
  course = c;
  fix = f;
  sat = s;
}

// Generar JSON optimizado
void Telemetry::toJSON(char* buffer, size_t size) 
{
  if (fix) {
      snprintf(buffer, size,
          "{\"temp\":%.1f,\"bat\":%.2f,\"gps\":{\"lat\":%.6f,\"lon\":%.6f,\"fix\":%d,\"sat\":%d},\"heading\":%.1f}",
          temp, bat, lat, lon, fix, sat, course
      );
  } else {
      // Sin fix → no mandamos lat/lon basura
      snprintf(buffer, size,
          "{\"temp\":%.1f,\"bat\":%.2f,\"gps\":{\"fix\":0},\"heading\":%.1f}",
          temp, bat, course
      );
  }
}
