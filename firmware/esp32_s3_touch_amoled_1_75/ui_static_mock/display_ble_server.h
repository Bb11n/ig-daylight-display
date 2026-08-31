#pragma once

#include <Arduino.h>

bool displayBleBegin();
void displayBleLoop();

bool displayBleIsConnected();
bool displayBleHasSubscriber();

void displayBleNotifyTest();

void displayBleNotifyStatus(
    bool projectionOn,
    bool highBrightness,
    uint8_t brightnessPercent,
    uint16_t displayMask,
    uint8_t displayBatteryPercent,
    uint32_t workoutDurationSeconds,
    float speedKmh,
    uint16_t caloriesKcal,
    float distanceMeters,
    uint16_t lapCount
);
