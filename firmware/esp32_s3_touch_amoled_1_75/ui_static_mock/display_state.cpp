#include "display_state.h"

#include <Arduino.h>
#include <math.h>

#include "debug_log_config.h"
#include "display_product_page.h"
#include "display_protocol_v2.h"

namespace {

constexpr size_t kBlePacketLength = 23;
constexpr uint8_t kBlePacketType = 0x20;
constexpr uint8_t kBlePacketDataLength = 0x15;
constexpr uint8_t kBlePacketVersion = 0x01;
DisplayState displayState = {};
uint8_t displayStateSequence = 2;
portMUX_TYPE displayStateMux = portMUX_INITIALIZER_UNLOCKED;

DisplayState defaultDisplayState()
{
    DisplayState state = {};
    state.revision = 0;
    state.projectionOn = false;
    state.highBrightness = true;
    state.brightnessPercent = 100;
    state.displayMask = DISPLAY_STATE_MASK_ALL;
    state.displayBatteryPercent = DISPLAY_STATE_BATTERY_UNKNOWN;
    state.laserBatteryPercent = DISPLAY_STATE_BATTERY_UNKNOWN;
    state.workoutDurationSeconds = 0;
    state.speedKmh = 0.0f;
    state.caloriesKcal = 0;
    state.distanceMeters = 0.0f;
    state.lapCount = 0;
    return state;
}

uint8_t clampPercent(uint8_t percent)
{
    return percent > 100 ? 100 : percent;
}

uint8_t normalizeLaserBatteryPercent(uint8_t percent)
{
    return percent == DISPLAY_STATE_BATTERY_UNKNOWN
        ? DISPLAY_STATE_BATTERY_UNKNOWN
        : clampPercent(percent);
}

uint8_t normalizeBatteryPercent(uint8_t percent)
{
    return percent == DISPLAY_STATE_BATTERY_UNKNOWN
        ? DISPLAY_STATE_BATTERY_UNKNOWN
        : clampPercent(percent);
}

void setMaskBit(uint16_t bit, bool show)
{
    portENTER_CRITICAL(&displayStateMux);
    if (show) {
        displayState.displayMask |= bit;
    } else {
        displayState.displayMask &= static_cast<uint16_t>(~bit);
    }
    portEXIT_CRITICAL(&displayStateMux);
}

void writeLe16(uint8_t* buffer, size_t offset, uint16_t value)
{
    buffer[offset] = static_cast<uint8_t>(value & 0xFF);
    buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void writeLe32(uint8_t* buffer, size_t offset, uint32_t value)
{
    buffer[offset] = static_cast<uint8_t>(value & 0xFF);
    buffer[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

uint16_t roundFloatToUint16(float value)
{
    if (isnan(value) || value <= 0.0f) {
        return 0;
    }
    if (value >= 65535.0f) {
        return 65535;
    }
    return static_cast<uint16_t>(roundf(value));
}

uint32_t roundFloatToUint32(float value)
{
    if (isnan(value) || value <= 0.0f) {
        return 0;
    }
    if (value >= 4294967295.0f) {
        return 0xFFFFFFFFUL;
    }
    return static_cast<uint32_t>(roundf(value));
}

} // namespace

void displayStateBegin()
{
    portENTER_CRITICAL(&displayStateMux);
    displayState = defaultDisplayState();
    displayStateSequence = 2;
    portEXIT_CRITICAL(&displayStateMux);
}

DisplayState displayStateGetSnapshot()
{
    portENTER_CRITICAL(&displayStateMux);
    const DisplayState snapshot = displayState;
    portEXIT_CRITICAL(&displayStateMux);
    return snapshot;
}

void displayStateSetProjection(bool on)
{
    portENTER_CRITICAL(&displayStateMux);
    displayState.projectionOn = on;
    portEXIT_CRITICAL(&displayStateMux);
}

void displayStateSetBrightnessHigh(bool high)
{
    portENTER_CRITICAL(&displayStateMux);
    displayState.highBrightness = high;
    displayState.brightnessPercent = high ? 100 : 40;
    portEXIT_CRITICAL(&displayStateMux);
}

void displayStateSetBrightnessPercent(uint8_t percent)
{
    portENTER_CRITICAL(&displayStateMux);
    displayState.brightnessPercent = clampPercent(percent);
    portEXIT_CRITICAL(&displayStateMux);
}

void displayStateSetDisplayMask(uint16_t mask)
{
    portENTER_CRITICAL(&displayStateMux);
    displayState.displayMask = mask;
    portEXIT_CRITICAL(&displayStateMux);
}

void displayStateSetShowBattery(bool show)
{
    setMaskBit(DISPLAY_STATE_MASK_BATTERY, show);
}

void displayStateSetShowDuration(bool show)
{
    setMaskBit(DISPLAY_STATE_MASK_DURATION, show);
}

void displayStateSetShowSpeed(bool show)
{
    setMaskBit(DISPLAY_STATE_MASK_SPEED, show);
}

void displayStateSetShowCalories(bool show)
{
    setMaskBit(DISPLAY_STATE_MASK_CALORIES, show);
}

void displayStateSetShowDistance(bool show)
{
    setMaskBit(DISPLAY_STATE_MASK_DISTANCE, show);
}

void displayStateSetShowLaps(bool show)
{
    setMaskBit(DISPLAY_STATE_MASK_LAPS, show);
}

void displayStateSetDisplayBattery(uint8_t percent)
{
    portENTER_CRITICAL(&displayStateMux);
    displayState.displayBatteryPercent = normalizeBatteryPercent(percent);
    portEXIT_CRITICAL(&displayStateMux);
}

void displayStateSetLaserBattery(uint8_t percent)
{
    portENTER_CRITICAL(&displayStateMux);
    displayState.laserBatteryPercent = normalizeLaserBatteryPercent(percent);
    portEXIT_CRITICAL(&displayStateMux);
}

void displayStateSetIgBattery(uint8_t percent)
{
    displayStateSetDisplayBattery(percent);
}

void displayStateUpdateWorkout(
    uint32_t durationSeconds,
    float speedKmh,
    uint16_t caloriesKcal,
    float distanceMeters
)
{
    portENTER_CRITICAL(&displayStateMux);
    displayState.workoutDurationSeconds = durationSeconds;
    displayState.speedKmh = speedKmh;
    displayState.caloriesKcal = caloriesKcal;
    displayState.distanceMeters = distanceMeters;
    portEXIT_CRITICAL(&displayStateMux);
}

void displayStateSetLapCount(uint16_t lapCount)
{
    portENTER_CRITICAL(&displayStateMux);
    displayState.lapCount = lapCount;
    portEXIT_CRITICAL(&displayStateMux);
}

void displayStateAddLap()
{
    portENTER_CRITICAL(&displayStateMux);
    displayState.lapCount++;
    portEXIT_CRITICAL(&displayStateMux);
}

void displayStateResetLaps()
{
    portENTER_CRITICAL(&displayStateMux);
    displayState.lapCount = 0;
    portEXIT_CRITICAL(&displayStateMux);
}

uint16_t displayStateGetDisplayMask()
{
    portENTER_CRITICAL(&displayStateMux);
    const uint16_t mask = displayState.displayMask;
    portEXIT_CRITICAL(&displayStateMux);
    return mask;
}

bool displayStatePackBlePacket(uint8_t* outBuffer, size_t outSize, size_t* outLen)
{
    if (outBuffer == nullptr || outLen == nullptr || outSize < kBlePacketLength) {
        return false;
    }

    portENTER_CRITICAL(&displayStateMux);
    const DisplayState snapshot = displayState;
    const uint8_t sequence = ++displayStateSequence;
    portEXIT_CRITICAL(&displayStateMux);

    const uint8_t controlFlags =
        (snapshot.projectionOn ? 0x01 : 0x00) |
        (snapshot.highBrightness ? 0x02 : 0x00);
    const uint16_t speedMpsX1000 = roundFloatToUint16((snapshot.speedKmh / 3.6f) * 1000.0f);
    const uint32_t distanceMX100 = roundFloatToUint32(snapshot.distanceMeters * 100.0f);

    outBuffer[0] = kBlePacketType;
    outBuffer[1] = kBlePacketDataLength;
    outBuffer[2] = kBlePacketVersion;
    outBuffer[3] = sequence;
    outBuffer[4] = controlFlags;
    outBuffer[5] = clampPercent(snapshot.brightnessPercent);
    writeLe16(outBuffer, 6, snapshot.displayMask);
    outBuffer[8] = clampPercent(snapshot.displayBatteryPercent);
    writeLe32(outBuffer, 9, snapshot.workoutDurationSeconds);
    writeLe16(outBuffer, 13, speedMpsX1000);
    writeLe16(outBuffer, 15, snapshot.caloriesKcal);
    writeLe32(outBuffer, 17, distanceMX100);
    writeLe16(outBuffer, 21, snapshot.lapCount);

    *outLen = kBlePacketLength;
    return true;
}

bool displayStatePackProtocolV2Packet(
    uint8_t* outBuffer,
    size_t outSize,
    size_t* outLength)
{
    return displayProtocolV2Pack(
        displayStateGetSnapshot(),
        displayProductPageGet(),
        outBuffer,
        outSize,
        outLength
    );
}

void displayStatePrintSnapshot()
{
#if ENABLE_DISPLAY_STATE_LOG
    const DisplayState snapshot = displayStateGetSnapshot();
    Serial.printf(
        "DISPLAY_STATE: rev=%lu proj=%d bright=%u high=%d mask=0x%04X display_batt=%u laser_batt=%u dur=%lu speed=%.2f cal=%u dist=%.2f laps=%u\n",
        static_cast<unsigned long>(snapshot.revision),
        snapshot.projectionOn ? 1 : 0,
        static_cast<unsigned>(snapshot.brightnessPercent),
        snapshot.highBrightness ? 1 : 0,
        static_cast<unsigned>(snapshot.displayMask),
        static_cast<unsigned>(snapshot.displayBatteryPercent),
        static_cast<unsigned>(snapshot.laserBatteryPercent),
        static_cast<unsigned long>(snapshot.workoutDurationSeconds),
        snapshot.speedKmh,
        static_cast<unsigned>(snapshot.caloriesKcal),
        snapshot.distanceMeters,
        static_cast<unsigned>(snapshot.lapCount)
    );
#endif
}
