#include <assert.h>
#include <math.h>

#include "workout_imu_estimator.h"
#include "workout_speed_resolver.h"

static WorkoutImuSample sample(float ax, float ay, float az, float gz = 0.0f)
{
    return {ax, ay, az, 0.0f, 0.0f, gz};
}

int main()
{
    WorkoutImuEstimator estimator;
    estimator.start(12.0f, 0);
    for (uint32_t now = 20; now <= 2200; now += 20) {
        estimator.update(sample(now > 400 ? 1.0f : 0.0f, 0.0f, 9.81f), now);
    }
    assert(estimator.axis() == WORKOUT_IMU_AXIS_X);
    const float accelerated = estimator.speedKmh();
    assert(accelerated > 12.0f && accelerated <= 80.0f);
    for (uint32_t now = 2220; now <= 4500; now += 20) {
        estimator.update(sample(0.0f, 1.5f, 9.81f), now);
    }
    assert(estimator.axis() == WORKOUT_IMU_AXIS_X);
    estimator.setSpeedKmh(79.9f, 4500);
    estimator.update(sample(20.0f, 0.0f, 9.81f), 9500);
    assert(estimator.speedKmh() <= 80.0f);

    WorkoutSpeedResolver resolver;
    resolver.start(0);
    auto result = resolver.update(0, true, 0.0f, 0.0f);
    assert(result.source == WORKOUT_SPEED_GPS);
    result = resolver.update(9999, false, 0.0f, 0.0f);
    assert(result.source == WORKOUT_SPEED_GPS);
    result = resolver.update(10000, false, 0.0f, 0.0f);
    assert(result.source == WORKOUT_SPEED_IMU_EST && result.switchedToImu);
    assert(fabsf(result.imuInitialSpeedKmh) < 0.01f);
    result = resolver.update(11000, true, 24.0f, 8.0f);
    assert(result.switchedToGps);
    assert(result.effectiveSpeedKmh < 24.0f);
    result = resolver.update(11500, false, 0.0f, 8.0f);
    assert(result.source == WORKOUT_SPEED_GPS);
    assert(fabsf(result.effectiveSpeedKmh - 24.0f) < 0.01f);

    resolver.start(0);
    resolver.update(0, true, 18.0f, 0.0f);
    resolver.pause(5000);
    resolver.resume(20000);
    result = resolver.update(24999, false, 0.0f, 0.0f);
    assert(result.source == WORKOUT_SPEED_GPS);
    result = resolver.update(25000, false, 0.0f, 0.0f);
    assert(result.source == WORKOUT_SPEED_IMU_EST);
    assert(fabsf(result.imuInitialSpeedKmh - 18.0f) < 0.01f);

    resolver.start(0);
    result = resolver.update(15000, false, 0.0f, 0.0f, false);
    assert(result.source == WORKOUT_SPEED_GPS && !result.switchedToImu);
    return 0;
}
