#include "imu.h"

#include <Wire.h>
#include <MPU9250.h>
#include <Adafruit_BMP280.h>

MPU9250 mpu;
Adafruit_BMP280 bmp;

static IMUData data;

bool initIMU(int sda, int scl)
{
    Wire.begin(sda, scl);

    data.okMPU = false;
    data.okBMP = false;

    if (mpu.setup(0x68)) {
        data.okMPU = true;
    }

    if (bmp.begin(0x76)) {
        data.okBMP = true;
    }
    else if (bmp.begin(0x77)) {
        data.okBMP = true;
    }

    return data.okMPU || data.okBMP;
}

void updateIMU()
{
    if (data.okMPU) {

        mpu.update();

        data.ax = mpu.getAccX();
        data.ay = mpu.getAccY();
        data.az = mpu.getAccZ();

        data.gx = mpu.getGyroX();
        data.gy = mpu.getGyroY();
        data.gz = mpu.getGyroZ();

        data.mx = mpu.getMagX();
        data.my = mpu.getMagY();
        data.mz = mpu.getMagZ();

        data.roll  = mpu.getRoll();
        data.pitch = mpu.getPitch();
        data.yaw   = mpu.getYaw();
    }

    if (data.okBMP) {
        data.temp = bmp.readTemperature();
        data.pressure = bmp.readPressure() / 100.0f;
        data.altitude = bmp.readAltitude(1013.25f);
    }
}

IMUData getIMUData()
{
    return data;
}