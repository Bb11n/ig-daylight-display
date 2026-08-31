#include <assert.h>
#include <math.h>

#include "workout_calories.h"

namespace {

bool nearlyEqual(float left, float right, float tolerance = 0.02f)
{
    return fabsf(left - right) <= tolerance;
}

} // namespace

int main()
{
    assert(workoutCaloriesMetForSpeed(9.9f) == 3.0f);
    assert(workoutCaloriesMetForSpeed(10.0f) == 4.0f);
    assert(workoutCaloriesMetForSpeed(16.0f) == 6.0f);
    assert(workoutCaloriesMetForSpeed(20.0f) == 8.0f);
    assert(workoutCaloriesMetForSpeed(25.0f) == 10.0f);

    workoutCaloriesBegin();
    workoutCaloriesStart();
    workoutCaloriesUpdate(19.9f, 10U * 60U * 1000U);
    assert(nearlyEqual(workoutCaloriesGetExact(), 70.0f));

    workoutCaloriesBegin();
    workoutCaloriesStart();
    workoutCaloriesUpdate(20.0f, 10U * 60U * 1000U);
    assert(nearlyEqual(workoutCaloriesGetExact(), 93.3333f));

    workoutCaloriesPause();
    const float pausedCalories = workoutCaloriesGetExact();
    workoutCaloriesUpdate(25.0f, 10U * 60U * 1000U);
    assert(nearlyEqual(workoutCaloriesGetExact(), pausedCalories));

    workoutCaloriesStart();
    workoutCaloriesUpdate(20.0f, 5U * 60U * 1000U);
    assert(nearlyEqual(workoutCaloriesGetExact(), 140.0f));

    workoutCaloriesBegin();
    workoutCaloriesStart();
    workoutCaloriesUpdate(5.0f, 60U * 60U * 1000U);
    workoutCaloriesUpdate(12.0f, 60U * 60U * 1000U);
    assert(nearlyEqual(workoutCaloriesGetExact(), 490.0f));
    assert(workoutCaloriesGetEstimated() == 490);
    return 0;
}
