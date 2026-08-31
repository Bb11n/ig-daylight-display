#include "workout_imu_estimator.h"

#include <math.h>

namespace {
constexpr float kMaxSpeedMps = 80.0f / 3.6f;
constexpr float kGravityAlpha = 0.02f;
constexpr float kAccelerationDeadband = 0.08f;
constexpr float kStationaryAcceleration = 0.15f;
constexpr float kStationaryGyroDps = 3.0f;
constexpr uint32_t kCalibrationMs = 2000;
constexpr uint32_t kStationaryConfirmMs = 1000;
constexpr uint32_t kMaxDeltaMs = 100;
constexpr float kDegreesToRadians = 0.01745329252f;

float clampf(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}
}

void WorkoutImuEstimator::start(float initialSpeedKmh, uint32_t nowMs)
{
    reset();
    running_ = true;
    startedMs_ = nowMs;
    lastUpdateMs_ = nowMs;
    speedMps_ = clampf(initialSpeedKmh / 3.6f, 0.0f, kMaxSpeedMps);
}

void WorkoutImuEstimator::pause() { running_ = false; }

void WorkoutImuEstimator::resume(uint32_t nowMs)
{
    running_ = true;
    lastUpdateMs_ = nowMs;
    stationarySinceMs_ = 0;
}

void WorkoutImuEstimator::reset() { *this = WorkoutImuEstimator(); }

void WorkoutImuEstimator::setSpeedKmh(float speedKmh, uint32_t nowMs)
{
    speedMps_ = clampf(speedKmh / 3.6f, 0.0f, kMaxSpeedMps);
    lastUpdateMs_ = nowMs;
}

void WorkoutImuEstimator::update(const WorkoutImuSample& sample, uint32_t nowMs)
{
    if (!running_) return;
    if (!gravityReady_) {
        gravityX_ = sample.ax; gravityY_ = sample.ay; gravityZ_ = sample.az;
        gravityReady_ = true; lastUpdateMs_ = nowMs; return;
    }

    uint32_t deltaMs = nowMs - lastUpdateMs_;
    lastUpdateMs_ = nowMs;
    if (deltaMs == 0) return;
    if (deltaMs > kMaxDeltaMs) deltaMs = kMaxDeltaMs;

    const float dt = static_cast<float>(deltaMs) / 1000.0f;
    const float wx = sample.gx * kDegreesToRadians;
    const float wy = sample.gy * kDegreesToRadians;
    const float wz = sample.gz * kDegreesToRadians;
    const float predictedGravityX = gravityX_ - (wy * gravityZ_ - wz * gravityY_) * dt;
    const float predictedGravityY = gravityY_ - (wz * gravityX_ - wx * gravityZ_) * dt;
    const float predictedGravityZ = gravityZ_ - (wx * gravityY_ - wy * gravityX_) * dt;
    gravityX_ = predictedGravityX + kGravityAlpha * (sample.ax - predictedGravityX);
    gravityY_ = predictedGravityY + kGravityAlpha * (sample.ay - predictedGravityY);
    gravityZ_ = predictedGravityZ + kGravityAlpha * (sample.az - predictedGravityZ);
    const float linearX = sample.ax - gravityX_;
    const float linearY = sample.ay - gravityY_;
    const float linearZ = sample.az - gravityZ_;

    if (!axisLocked_) {
        xEnergy_ += linearX * linearX;
        yEnergy_ += linearY * linearY;
        xSigned_ += linearX;
        ySigned_ += linearY;
        if ((nowMs - startedMs_) < kCalibrationMs) return;
        axis_ = xEnergy_ >= yEnergy_ ? WORKOUT_IMU_AXIS_X : WORKOUT_IMU_AXIS_Y;
        const float signedSum = axis_ == WORKOUT_IMU_AXIS_X ? xSigned_ : ySigned_;
        axisSign_ = signedSum < 0.0f ? -1.0f : 1.0f;
        axisLocked_ = true;
    }

    float forwardAcceleration = axisSign_ * (axis_ == WORKOUT_IMU_AXIS_X ? linearX : linearY);
    const float linearMagnitude = sqrtf(linearX * linearX + linearY * linearY + linearZ * linearZ);
    const float gyroMagnitude = sqrtf(sample.gx * sample.gx + sample.gy * sample.gy + sample.gz * sample.gz);
    const bool currentlyStationary = linearMagnitude < kStationaryAcceleration && gyroMagnitude < kStationaryGyroDps;
    if (currentlyStationary) {
        if (stationarySinceMs_ == 0) stationarySinceMs_ = nowMs;
        stationary_ = (nowMs - stationarySinceMs_) >= kStationaryConfirmMs;
    } else {
        stationarySinceMs_ = 0;
        stationary_ = false;
    }

    if (fabsf(forwardAcceleration) < kAccelerationDeadband) forwardAcceleration = 0.0f;
    speedMps_ += forwardAcceleration * dt;
    if (stationary_) speedMps_ *= 0.98f;
    speedMps_ = clampf(speedMps_, 0.0f, kMaxSpeedMps);
}

float WorkoutImuEstimator::speedKmh() const { return speedMps_ * 3.6f; }
WorkoutImuAxis WorkoutImuEstimator::axis() const { return axis_; }
bool WorkoutImuEstimator::axisLocked() const { return axisLocked_; }
bool WorkoutImuEstimator::stationary() const { return stationary_; }
