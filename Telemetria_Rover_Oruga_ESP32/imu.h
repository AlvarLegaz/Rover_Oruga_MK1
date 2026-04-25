#pragma once

#include <Arduino.h>

struct IMUData {
    bool okMPU;
    bool okBMP;

    float roll;
    float pitch;
    float yaw;

    float ax;
    float ay;
    float az;

    float gx;
    float gy;
    float gz;

    float mx;
    float my;
    float mz;

    float temp;
    float pressure;
    float altitude;
};

bool initIMU(int sda, int scl);
void updateIMU();
IMUData getIMUData();