#pragma once

#include <stdint.h>

enum WorkoutImuAxis : uint8_t {
    WORKOUT_IMU_AXIS_UNKNOWN = 0,
    WORKOUT_IMU_AXIS_X,
    WORKOUT_IMU_AXIS_Y,
};

struct WorkoutImuSample {
    float ax;
    float ay;
    float az;
    float gx;
    float gy;
    float gz;
};

class WorkoutImuEstimator {
public:
    void start(float initialSpeedKmh, uint32_t nowMs);
    void pause();
    void resume(uint32_t nowMs);
    void reset();
    void setSpeedKmh(float speedKmh, uint32_t nowMs);
    void update(const WorkoutImuSample& sample, uint32_t nowMs);
    float speedKmh() const;
    WorkoutImuAxis axis() const;
    bool axisLocked() const;
    bool stationary() const;

private:
    bool running_ = false;
    bool gravityReady_ = false;
    bool axisLocked_ = false;
    bool stationary_ = false;
    uint32_t startedMs_ = 0;
    uint32_t lastUpdateMs_ = 0;
    uint32_t stationarySinceMs_ = 0;
    float speedMps_ = 0.0f;
    float gravityX_ = 0.0f;
    float gravityY_ = 0.0f;
    float gravityZ_ = 0.0f;
    float xEnergy_ = 0.0f;
    float yEnergy_ = 0.0f;
    float xSigned_ = 0.0f;
    float ySigned_ = 0.0f;
    float axisSign_ = 1.0f;
    WorkoutImuAxis axis_ = WORKOUT_IMU_AXIS_UNKNOWN;
};
