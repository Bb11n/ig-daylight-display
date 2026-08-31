#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint8_t DISPLAY_STATE_BATTERY_UNKNOWN = 0xFF;

constexpr uint16_t DISPLAY_STATE_MASK_BATTERY = 1U << 0;
constexpr uint16_t DISPLAY_STATE_MASK_DURATION = 1U << 1;
constexpr uint16_t DISPLAY_STATE_MASK_SPEED = 1U << 2;
constexpr uint16_t DISPLAY_STATE_MASK_CALORIES = 1U << 3;
constexpr uint16_t DISPLAY_STATE_MASK_DISTANCE = 1U << 4;
constexpr uint16_t DISPLAY_STATE_MASK_LAPS = 1U << 5;
constexpr uint16_t DISPLAY_STATE_MASK_ALL = 0x003F;

struct DisplayState {
    uint32_t revision;
    bool projectionOn;
    bool highBrightness;
    uint8_t brightnessPercent;
    uint16_t displayMask;
    // 0-100 is valid; 0xFF means unknown/unavailable from the display PMU.
    uint8_t displayBatteryPercent;
    // 0-100 is valid; 0xFF means unknown/not received from the laser.
    uint8_t laserBatteryPercent;
    uint32_t workoutDurationSeconds;
    float speedKmh;
    uint16_t caloriesKcal;
    float distanceMeters;
    uint16_t lapCount;
};

void displayStateBegin();

DisplayState displayStateGetSnapshot();

void displayStateSetProjection(bool on);

void displayStateSetBrightnessHigh(bool high);
void displayStateSetBrightnessPercent(uint8_t percent);

void displayStateSetDisplayMask(uint16_t mask);
void displayStateSetShowBattery(bool show);
void displayStateSetShowDuration(bool show);
void displayStateSetShowSpeed(bool show);
void displayStateSetShowCalories(bool show);
void displayStateSetShowDistance(bool show);
void displayStateSetShowLaps(bool show);

void displayStateSetDisplayBattery(uint8_t percent);
void displayStateSetLaserBattery(uint8_t percent);

// Legacy name retained for compatibility; it updates the display battery.
void displayStateSetIgBattery(uint8_t percent);

void displayStateUpdateWorkout(
    uint32_t durationSeconds,
    float speedKmh,
    uint16_t caloriesKcal,
    float distanceMeters
);

void displayStateSetLapCount(uint16_t lapCount);
void displayStateAddLap();
void displayStateResetLaps();

uint16_t displayStateGetDisplayMask();

// Legacy 23-byte encoder retained for compatibility; BLE no longer uses it.
bool displayStatePackBlePacket(uint8_t* outBuffer, size_t outSize, size_t* outLen);

bool displayStatePackProtocolV2Packet(
    uint8_t* outBuffer,
    size_t outSize,
    size_t* outLength
);

void displayStatePrintSnapshot();
