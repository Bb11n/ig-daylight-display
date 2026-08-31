#pragma once

#include <stdint.h>

enum SpeedSource {
    SPEED_SOURCE_NONE,
    SPEED_SOURCE_GPS,
    SPEED_SOURCE_IMU_EST,
    SPEED_SOURCE_FUSED,
    SPEED_SOURCE_MOCK
};

struct UiState {
    int hour;
    int minute;
    int batteryPercent;
    float speedKmh;
    float distanceKm;
    int elapsedMinutes;
    uint32_t elapsedSeconds;
    int satellites;
    int igBrightness;
    int igTemperature;
    int homeHour;
    int homeMinute;
    int homeBrightness;
    int brightnessLevel;
    float batteryVoltage;
    float systemVoltage;
    float pmuTemperature;
    bool gpsFixed;
    bool bleConnected;
    bool igConnected;
    bool recording;
    bool projectorOn;
    bool gpsEnabled;
    bool bleEnabled;
    bool pmuReady;
    bool charging;
    bool vbusIn;
    bool vbusGood;
    SpeedSource speedSource;
    const char* dateText;
    const char* modeText;
    const char* statusText;
    const char* igMode;
    const char* homeModeText;
    const char* indoorStatus;
    const char* homeIgStatus;
    const char* homeActionText;
    const char* settingsTitle;
    const char* settingBluetooth;
    const char* settingGps;
    const char* settingBle;
    const char* settingBrightness;
    const char* settingRecord;
    const char* settingAbout;
    const char* settingSystem;
};

inline UiState uiCreateMockState()
{
    UiState state = {};
    state.hour = 10;
    state.minute = 18;
    state.batteryPercent = 76;
    // Mock/fallback values for the static round UI before live sensors update them.
    state.speedKmh = 24.8f;
    state.distanceKm = 0.0f;
    state.elapsedMinutes = 18;
    state.elapsedSeconds = 18 * 60;
    state.satellites = 12;
    state.igBrightness = 68;
    state.igTemperature = 36;
    state.homeHour = 22;
    state.homeMinute = 15;
    state.homeBrightness = 35;
    state.brightnessLevel = 76;
    state.batteryVoltage = 0.0f;
    state.systemVoltage = 0.0f;
    state.pmuTemperature = 0.0f;
    state.gpsFixed = true;
    state.bleConnected = true;
    state.igConnected = true;
    state.recording = false;
    state.projectorOn = true;
    state.gpsEnabled = true;
    state.bleEnabled = true;
    state.pmuReady = false;
    state.charging = false;
    state.vbusIn = false;
    state.vbusGood = false;
    state.speedSource = SPEED_SOURCE_MOCK;
    state.dateText = "JUN 26";
    state.modeText = "ALL DAY";
    state.statusText = "IG READY";
    state.igMode = "NIGHT HUD";
    state.homeModeText = "HOME MODE";
    state.indoorStatus = "INDOOR";
    state.homeIgStatus = "IG STANDBY";
    state.homeActionText = "TAP TO START";
    state.settingsTitle = "SETTINGS";
    state.settingBluetooth = "BLUETOOTH";
    state.settingGps = "GPS";
    state.settingBle = "BLE";
    state.settingBrightness = "BRIGHTNESS";
    state.settingRecord = "RECORD";
    state.settingAbout = "ABOUT";
    state.settingSystem = "SYSTEM";
    return state;
}
