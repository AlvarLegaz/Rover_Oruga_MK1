// Telemetry.h

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <Arduino.h>
#include "gps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class Telemetry {
public:
    Telemetry();
    void begin();
    void updateInputs();
    void toJSON(char* buffer, size_t size);

private:

    float temp;
    float bat;
    GPSData gps;

    SemaphoreHandle_t mutex;
};

#endif