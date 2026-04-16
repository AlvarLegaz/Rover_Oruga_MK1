#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <Arduino.h>

class Telemetry {
public:
float temp;
float bat;

// GPS
float lat;
float lon;
float course;
uint8_t fix;
uint8_t sat;

// Constructor
Telemetry();

// Update general
void update(float t, float b);

// Update GPS
void updateGPS(float la, float lo, float c, uint8_t f, uint8_t s);

// JSON
void toJSON(char* buffer, size_t size);


};

#endif

