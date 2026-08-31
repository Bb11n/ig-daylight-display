#pragma once

#include <stdint.h>

enum I2cTelemetryPhase : uint8_t {
    I2C_PHASE_A_I2C_ONLY = 0,
    I2C_PHASE_B_BLE_ADVERTISING,
    I2C_PHASE_C_BLE_CONNECTED,
    I2C_PHASE_COUNT,
};

void i2cTelemetryBegin();
void i2cTelemetrySetPhase(I2cTelemetryPhase phase);
void i2cTelemetrySetBleState(bool initialized, bool connected);
void i2cTelemetryPrintSummaryIfDue(uint32_t nowMs);
void i2cTelemetryPrintPhaseSummary(I2cTelemetryPhase phase);
const char* i2cTelemetryPhaseName(I2cTelemetryPhase phase);
