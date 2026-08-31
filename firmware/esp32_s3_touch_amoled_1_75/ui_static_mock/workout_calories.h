#pragma once

#include <stdint.h>

constexpr float DEFAULT_WEIGHT_KG = 70.0f;

enum class WorkoutCaloriesStatus : uint8_t {
    Idle,
    Running,
    Paused,
};

void workoutCaloriesBegin(float weightKg = DEFAULT_WEIGHT_KG);
void workoutCaloriesStart();
void workoutCaloriesPause();
void workoutCaloriesUpdate(float speedKmh, uint32_t elapsedMs);

float workoutCaloriesMetForSpeed(float speedKmh);
float workoutCaloriesGetExact();
uint16_t workoutCaloriesGetEstimated();
WorkoutCaloriesStatus workoutCaloriesGetStatus();
