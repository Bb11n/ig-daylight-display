#pragma once

#include <stdint.h>

enum WorkoutSpeedSource : uint8_t {
    WORKOUT_SPEED_GPS = 0,
    WORKOUT_SPEED_IMU_EST,
};

struct WorkoutSpeedResult {
    WorkoutSpeedSource source;
    float effectiveSpeedKmh;
    bool switchedToImu;
    bool switchedToGps;
    float imuInitialSpeedKmh;
};

class WorkoutSpeedResolver {
public:
    void start(uint32_t nowMs);
    void pause(uint32_t nowMs);
    void resume(uint32_t nowMs);
    void reset();
    WorkoutSpeedResult update(
        uint32_t nowMs,
        bool validGpsSample,
        float gpsSpeedKmh,
        float imuSpeedKmh,
        bool imuAvailable = true);
    WorkoutSpeedSource source() const;

private:
    uint32_t activeNow(uint32_t nowMs) const;
    bool running_ = false;
    WorkoutSpeedSource source_ = WORKOUT_SPEED_GPS;
    uint32_t pausedAtMs_ = 0;
    uint32_t totalPausedMs_ = 0;
    uint32_t lastGpsActiveMs_ = 0;
    uint32_t recoveryStartedActiveMs_ = 0;
    float lastGpsSpeedKmh_ = 0.0f;
    float recoveryFromKmh_ = 0.0f;
    bool recovering_ = false;
};
