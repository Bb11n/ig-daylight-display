#include "workout_calories.h"

#include <math.h>

namespace {

constexpr float kMillisecondsPerHour = 3600000.0f;
constexpr float kMaximumCalories = 65535.0f;

float riderWeightKg = DEFAULT_WEIGHT_KG;
float estimatedCalories = 0.0f;
WorkoutCaloriesStatus workoutStatus = WorkoutCaloriesStatus::Idle;

float normalizeWeight(float weightKg)
{
    return isfinite(weightKg) && weightKg > 0.0f ? weightKg : DEFAULT_WEIGHT_KG;
}

} // namespace

void workoutCaloriesBegin(float weightKg)
{
    riderWeightKg = normalizeWeight(weightKg);
    estimatedCalories = 0.0f;
    workoutStatus = WorkoutCaloriesStatus::Idle;
}

void workoutCaloriesStart()
{
    if (workoutStatus == WorkoutCaloriesStatus::Idle) {
        estimatedCalories = 0.0f;
    }
    workoutStatus = WorkoutCaloriesStatus::Running;
}

void workoutCaloriesPause()
{
    if (workoutStatus == WorkoutCaloriesStatus::Running) {
        workoutStatus = WorkoutCaloriesStatus::Paused;
    }
}

void workoutCaloriesUpdate(float speedKmh, uint32_t elapsedMs)
{
    if (workoutStatus != WorkoutCaloriesStatus::Running || elapsedMs == 0) {
        return;
    }

    const float elapsedHours = static_cast<float>(elapsedMs) / kMillisecondsPerHour;
    estimatedCalories += workoutCaloriesMetForSpeed(speedKmh) * riderWeightKg * elapsedHours;
    if (estimatedCalories > kMaximumCalories) {
        estimatedCalories = kMaximumCalories;
    }
}

float workoutCaloriesMetForSpeed(float speedKmh)
{
    if (!isfinite(speedKmh) || speedKmh < 10.0f) {
        return 3.0f;
    }
    if (speedKmh < 16.0f) {
        return 4.0f;
    }
    if (speedKmh < 20.0f) {
        return 6.0f;
    }
    if (speedKmh < 25.0f) {
        return 8.0f;
    }
    return 10.0f;
}

float workoutCaloriesGetExact()
{
    return estimatedCalories;
}

uint16_t workoutCaloriesGetEstimated()
{
    return static_cast<uint16_t>(roundf(estimatedCalories));
}

WorkoutCaloriesStatus workoutCaloriesGetStatus()
{
    return workoutStatus;
}
