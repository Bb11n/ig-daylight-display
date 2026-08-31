#include "workout_speed_resolver.h"

namespace {
constexpr uint32_t kGpsFallbackMs = 10000;
constexpr uint32_t kGpsRecoveryBlendMs = 500;
}

void WorkoutSpeedResolver::start(uint32_t nowMs)
{
    reset(); running_ = true; lastGpsActiveMs_ = activeNow(nowMs);
}

void WorkoutSpeedResolver::pause(uint32_t nowMs)
{
    if (running_) { running_ = false; pausedAtMs_ = nowMs; }
}

void WorkoutSpeedResolver::resume(uint32_t nowMs)
{
    if (!running_) { totalPausedMs_ += nowMs - pausedAtMs_; running_ = true; }
}

void WorkoutSpeedResolver::reset() { *this = WorkoutSpeedResolver(); }

uint32_t WorkoutSpeedResolver::activeNow(uint32_t nowMs) const
{
    const uint32_t paused = totalPausedMs_ + (!running_ && pausedAtMs_ != 0 ? nowMs - pausedAtMs_ : 0);
    return nowMs - paused;
}

WorkoutSpeedResult WorkoutSpeedResolver::update(
    uint32_t nowMs,
    bool validGpsSample,
    float gpsSpeedKmh,
    float imuSpeedKmh,
    bool imuAvailable)
{
    WorkoutSpeedResult result = {source_, source_ == WORKOUT_SPEED_GPS ? lastGpsSpeedKmh_ : imuSpeedKmh, false, false, lastGpsSpeedKmh_};
    if (!running_) return result;
    const uint32_t activeMs = activeNow(nowMs);

    if (validGpsSample) {
        lastGpsSpeedKmh_ = gpsSpeedKmh < 0.0f ? 0.0f : gpsSpeedKmh;
        lastGpsActiveMs_ = activeMs;
        if (source_ == WORKOUT_SPEED_IMU_EST) {
            source_ = WORKOUT_SPEED_GPS;
            recovering_ = true;
            recoveryStartedActiveMs_ = activeMs;
            recoveryFromKmh_ = imuSpeedKmh;
            result.switchedToGps = true;
        }
    } else if (imuAvailable && source_ == WORKOUT_SPEED_GPS && (activeMs - lastGpsActiveMs_) >= kGpsFallbackMs) {
        source_ = WORKOUT_SPEED_IMU_EST;
        recovering_ = false;
        result.switchedToImu = true;
        result.imuInitialSpeedKmh = lastGpsSpeedKmh_;
    }

    result.source = source_;
    if (source_ == WORKOUT_SPEED_IMU_EST) {
        result.effectiveSpeedKmh = result.switchedToImu ? lastGpsSpeedKmh_ : imuSpeedKmh;
    } else if (recovering_) {
        const uint32_t blendElapsed = activeMs - recoveryStartedActiveMs_;
        if (blendElapsed >= kGpsRecoveryBlendMs) {
            recovering_ = false;
            result.effectiveSpeedKmh = lastGpsSpeedKmh_;
        } else {
            const float blend = static_cast<float>(blendElapsed) / static_cast<float>(kGpsRecoveryBlendMs);
            result.effectiveSpeedKmh = recoveryFromKmh_ + (lastGpsSpeedKmh_ - recoveryFromKmh_) * blend;
        }
    } else {
        result.effectiveSpeedKmh = lastGpsSpeedKmh_;
    }
    return result;
}

WorkoutSpeedSource WorkoutSpeedResolver::source() const { return source_; }
