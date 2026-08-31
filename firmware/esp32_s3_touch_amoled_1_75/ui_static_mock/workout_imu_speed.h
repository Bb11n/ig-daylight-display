#pragma once

#include <stdint.h>

#include "SensorQMI8658.hpp"
#include "workout_imu_estimator.h"

bool workoutImuSpeedBegin(
    SensorCommCustom::CustomCallback i2cCallback,
    SensorCommCustomHal::CustomHalCallback halCallback);
void workoutImuSpeedStart(float initialSpeedKmh, uint32_t nowMs);
void workoutImuSpeedPause();
void workoutImuSpeedResume(uint32_t nowMs);
void workoutImuSpeedSetBaseline(float speedKmh, uint32_t nowMs);
void workoutImuSpeedUpdate(uint32_t nowMs);
float workoutImuSpeedGetKmh();
WorkoutImuAxis workoutImuSpeedGetAxis();
bool workoutImuSpeedReady();
