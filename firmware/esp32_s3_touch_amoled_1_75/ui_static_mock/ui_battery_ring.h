#pragma once

#include <lvgl.h>

struct UiBatteryRing {
    lv_obj_t* arc;
    int lastPercent;
    bool lastValid;
    bool lastLowBattery;
    bool created;
};

constexpr bool uiBatteryRingBatteryValid(
    bool pmuReady,
    float batteryVoltage,
    int batteryPercent)
{
    return pmuReady &&
        batteryVoltage > 0.0f &&
        batteryPercent >= 0 &&
        batteryPercent <= 100;
}

constexpr int uiBatteryRingClampPercent(int batteryPercent)
{
    return batteryPercent < 0 ? 0 : (batteryPercent > 100 ? 100 : batteryPercent);
}

constexpr bool uiBatteryRingIsLowBattery(int batteryPercent)
{
    return uiBatteryRingClampPercent(batteryPercent) <= 20;
}

constexpr bool uiBatteryRingIndicatorVisible(bool batteryValid, int batteryPercent)
{
    return batteryValid && uiBatteryRingClampPercent(batteryPercent) > 0;
}

constexpr int uiBatteryRingSweepDegrees(int batteryPercent)
{
    return (uiBatteryRingClampPercent(batteryPercent) * 360) / 100;
}

void uiBatteryRingCreate(lv_obj_t* parent, UiBatteryRing* ring);
void uiBatteryRingUpdate(UiBatteryRing* ring, bool batteryValid, int batteryPercent);
void uiBatteryRingReset(UiBatteryRing* ring);
