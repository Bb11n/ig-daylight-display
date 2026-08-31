#include "workout_imu_speed.h"

namespace {
constexpr uint8_t kQmi8658Address = QMI8658_L_SLAVE_ADDRESS;
constexpr uint32_t kSampleIntervalMs = 20;
constexpr float kStandardGravity = 9.80665f;

SensorQMI8658 qmi8658;
WorkoutImuEstimator estimator;
bool sensorReady = false;
uint32_t lastSampleMs = 0;
}

bool workoutImuSpeedBegin(
    SensorCommCustom::CustomCallback i2cCallback,
    SensorCommCustomHal::CustomHalCallback halCallback)
{
    sensorReady = qmi8658.begin(i2cCallback, halCallback, kQmi8658Address);
    if (!sensorReady) return false;
    if (qmi8658.configAccelerometer(
            SensorQMI8658::ACC_RANGE_4G,
            SensorQMI8658::ACC_ODR_1000Hz,
            SensorQMI8658::LPF_MODE_0) != 0) return sensorReady = false;
    if (qmi8658.configGyroscope(
            SensorQMI8658::GYR_RANGE_64DPS,
            SensorQMI8658::GYR_ODR_896_8Hz,
            SensorQMI8658::LPF_MODE_3) != 0) return sensorReady = false;
    qmi8658.enableAccelerometer();
    qmi8658.enableGyroscope();
    return true;
}

void workoutImuSpeedStart(float initialSpeedKmh, uint32_t nowMs)
{
    estimator.start(initialSpeedKmh, nowMs);
    lastSampleMs = nowMs;
}

void workoutImuSpeedPause() { estimator.pause(); }
void workoutImuSpeedResume(uint32_t nowMs) { estimator.resume(nowMs); lastSampleMs = nowMs; }
void workoutImuSpeedSetBaseline(float speedKmh, uint32_t nowMs) { estimator.setSpeedKmh(speedKmh, nowMs); }

void workoutImuSpeedUpdate(uint32_t nowMs)
{
    if (!sensorReady || (nowMs - lastSampleMs) < kSampleIntervalMs) return;
    lastSampleMs = nowMs;
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    if (!qmi8658.getAccelerometer(ax, ay, az) || !qmi8658.getGyroscope(gx, gy, gz)) return;
    estimator.update({ax * kStandardGravity, ay * kStandardGravity, az * kStandardGravity, gx, gy, gz}, nowMs);
}

float workoutImuSpeedGetKmh() { return estimator.speedKmh(); }
WorkoutImuAxis workoutImuSpeedGetAxis() { return estimator.axis(); }
bool workoutImuSpeedReady() { return sensorReady; }
