// imu.cpp
// ESP32-CAM + MPU/BMP sin magnetómetro funcional
// Usa:
//   Pitch  -> estable
//   Roll   -> estable
//   Yaw    -> relativo por gyro (sin brújula)
// Ideal para rover oruga

#include "imu.h"

#include <Arduino.h>
#include <Wire.h>
#include <MPU9250_asukiaaa.h>
#include <Adafruit_BMP280.h>

MPU9250_asukiaaa mpu;
Adafruit_BMP280 bmp;

static IMUData data;

// actitud
static float roll  = 0.0f;
static float pitch = 0.0f;
static float yaw   = 0.0f;

// offsets gyro
static float gxOff = 0.0f;
static float gyOff = 0.0f;
static float gzOff = 0.0f;

// timers
static uint32_t lastFast  = 0;
static uint32_t lastBMP   = 0;
static uint32_t lastDebug = 0;

// --------------------------------------------------
// Calibración gyro (dejar quieto al arrancar)
// --------------------------------------------------

static void calibrateGyro()
{
    const int N = 300;
    float sx = 0, sy = 0, sz = 0;

    for (int i = 0; i < N; i++)
    {
        mpu.gyroUpdate();

        sx += mpu.gyroX();
        sy += mpu.gyroY();
        sz += mpu.gyroZ();

        delay(5);
    }

    gxOff = sx / N;
    gyOff = sy / N;
    gzOff = sz / N;
}

// --------------------------------------------------
// INIT
// --------------------------------------------------

bool initIMU(int sda, int scl)
{
    Wire.begin(sda, scl);
    Wire.setClock(400000);
    delay(100);

    data.okMPU = false;
    data.okBMP = false;

    mpu.setWire(&Wire);

    // detectar MPU
    Wire.beginTransmission(0x68);
    if (Wire.endTransmission() == 0)
    {
        mpu.beginAccel();
        mpu.beginGyro();

        data.okMPU = true;

        calibrateGyro();
    }

    // detectar BMP280
    if (bmp.begin(0x76)) data.okBMP = true;
    else if (bmp.begin(0x77)) data.okBMP = true;

    lastFast  = millis();
    lastBMP   = millis();
    lastDebug = millis();

    return data.okMPU || data.okBMP;
}

// --------------------------------------------------
// UPDATE
// llamar cada loop()
// --------------------------------------------------

void updateIMU()
{
    uint32_t now = millis();

    // 50 ms = 20 Hz
    if (now - lastFast < 10) return;

    float dt = (now - lastFast) * 0.001f;
    lastFast = now;

    if (dt <= 0 || dt > 1.0f) dt = 0.05f;

    // --------------------------------------------------
    // MPU
    // --------------------------------------------------

    if (data.okMPU)
    {
        mpu.accelUpdate();
        mpu.gyroUpdate();

        // RAW
        data.ax = mpu.accelX();
        data.ay = mpu.accelY();
        data.az = mpu.accelZ();

        data.gx = mpu.gyroX() - gxOff;
        data.gy = mpu.gyroY() - gyOff;
        data.gz = mpu.gyroZ() - gzOff;

        // Pitch / Roll desde acelerómetro
        float accRoll =
            atan2(data.ay, -data.az) * 57.2958f;

        float accPitch =
            atan2(-data.ax,
            sqrt(data.ay * data.ay +
                 data.az * data.az)) * 57.2958f;

        // Complementary filter
        roll =
            0.1f * (roll + data.gx * dt) +
            0.99f * accRoll;

        pitch =
            0.1f * (pitch + data.gy * dt) +
            0.99f * accPitch;

        // Yaw SOLO gyro (relativo)
        yaw += data.gz * dt;

        // normalizar
        if (yaw >= 360.0f) yaw -= 360.0f;
        if (yaw < 0.0f)    yaw += 360.0f;

        data.roll  = roll;
        data.pitch = -pitch;
        data.yaw   = yaw;
    }

    // --------------------------------------------------
    // BMP280 cada 1 segundo
    // --------------------------------------------------

    if (data.okBMP && now - lastBMP > 1000)
    {
        data.temp     = bmp.readTemperature();
        data.pressure = bmp.readPressure() / 100.0f;
        data.altitude = bmp.readAltitude(1013.25f);

        lastBMP = now;
    }

    // --------------------------------------------------
    // DEBUG cada 1000 ms
    // --------------------------------------------------
    /*
    if (now - lastDebug > 1000)
    {
        Serial.printf(
            "AX:%.2f AY:%.2f AZ:%.2f | "
            "GX:%.2f GY:%.2f GZ:%.2f | "
            "R:%.1f P:%.1f Y:%.1f | "
            "Alt:%.1f T:%.1f\n",

            data.ax, data.ay, data.az,
            data.gx, data.gy, data.gz,
            data.roll, data.pitch, data.yaw,
            data.altitude, data.temp
        );

        lastDebug = now;
    }
    */
}

// --------------------------------------------------
// GETTER
// --------------------------------------------------

IMUData getIMUData()
{
    return data;
}