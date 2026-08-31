#include "i2c_telemetry.h"

#include <Arduino.h>
#include "esp32-hal-i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

enum I2cClient : uint8_t {
    I2C_CLIENT_TOUCH = 0,
    I2C_CLIENT_PMU,
    I2C_CLIENT_RTC,
    I2C_CLIENT_GPS,
    I2C_CLIENT_UNKNOWN,
    I2C_CLIENT_COUNT,
};

struct I2cCounters {
    uint32_t transactionCount;
    uint32_t successCount;
    uint32_t invalidStateCount;
    uint32_t timeoutCount;
    uint32_t ackErrorCount;
    uint32_t shortReadCount;
    uint32_t zeroReadCount;
    uint32_t maxTransactionUs;
    esp_err_t lastError;
    uint32_t lastErrorTimestamp;
    TaskHandle_t lastTask;
    uint16_t lastAddress;
};

struct ErrorContext {
    bool shouldLog;
    bool firstInvalid;
    uint32_t clientErrorCount;
    uint32_t sequence;
};

constexpr uint16_t kTouchAddress = 0x5A;
constexpr uint16_t kPmuAddress = 0x34;
constexpr uint16_t kRtcAddress = 0x51;
constexpr uint16_t kGpsAddress = 0x50;
constexpr uint16_t kGpsReadAddress = 0x54;
constexpr uint32_t kSummaryIntervalMs = 10000;
constexpr char kTelemetryTag[] = "I2C_TELEMETRY";

portMUX_TYPE telemetryMux = portMUX_INITIALIZER_UNLOCKED;
I2cCounters counters[I2C_PHASE_COUNT][I2C_CLIENT_COUNT] = {};
volatile I2cTelemetryPhase currentPhase = I2C_PHASE_A_I2C_ONLY;
volatile bool bleInitialized = false;
volatile bool bleConnected = false;
bool firstInvalidLogged = false;
uint32_t transactionSequence = 0;
uint32_t lastSummaryMs = 0;

I2cClient clientForAddress(uint16_t address)
{
    switch (address) {
        case kTouchAddress: return I2C_CLIENT_TOUCH;
        case kPmuAddress: return I2C_CLIENT_PMU;
        case kRtcAddress: return I2C_CLIENT_RTC;
        case kGpsAddress:
        case kGpsReadAddress: return I2C_CLIENT_GPS;
        default: return I2C_CLIENT_UNKNOWN;
    }
}

const char* clientName(I2cClient client)
{
    switch (client) {
        case I2C_CLIENT_TOUCH: return "TOUCH";
        case I2C_CLIENT_PMU: return "PMU";
        case I2C_CLIENT_RTC: return "RTC";
        case I2C_CLIENT_GPS: return "GPS";
        default: return "UNKNOWN";
    }
}

ErrorContext recordTransaction(
    I2cClient client,
    uint16_t address,
    esp_err_t result,
    size_t requestedRead,
    size_t actualRead,
    uint32_t elapsedUs)
{
    ErrorContext context = {};
    const uint32_t nowMs = millis();
    const TaskHandle_t task = xTaskGetCurrentTaskHandle();

    portENTER_CRITICAL(&telemetryMux);
    const I2cTelemetryPhase phase = currentPhase;
    I2cCounters& item = counters[phase][client];
    item.transactionCount++;
    context.sequence = ++transactionSequence;
    if (elapsedUs > item.maxTransactionUs) {
        item.maxTransactionUs = elapsedUs;
    }

    if (requestedRead > 0 && actualRead == 0) {
        item.zeroReadCount++;
    } else if (requestedRead > 0 && actualRead < requestedRead) {
        item.shortReadCount++;
    }

    if (result == ESP_OK) {
        item.successCount++;
    } else {
        item.lastError = result;
        item.lastErrorTimestamp = nowMs;
        item.lastTask = task;
        item.lastAddress = address;
        if (result == ESP_ERR_INVALID_STATE) {
            item.invalidStateCount++;
            // IDF 5.5.4 maps a synchronous NACK to ESP_ERR_INVALID_STATE.
            item.ackErrorCount++;
            if (!firstInvalidLogged) {
                firstInvalidLogged = true;
                context.firstInvalid = true;
            }
        } else if (result == ESP_ERR_TIMEOUT) {
            item.timeoutCount++;
        }
        context.clientErrorCount = item.invalidStateCount + item.timeoutCount;
        context.shouldLog = context.clientErrorCount == 1 || (context.clientErrorCount % 100) == 0;
    }
    portEXIT_CRITICAL(&telemetryMux);
    return context;
}

void logTransactionError(
    const ErrorContext& context,
    I2cClient client,
    const char* operation,
    uint8_t bus,
    uint16_t address,
    size_t length,
    esp_err_t result,
    uint32_t elapsedUs)
{
    if (!context.shouldLog && !context.firstInvalid) {
        return;
    }

    const I2cTelemetryPhase phase = currentPhase;
    const char* taskName = pcTaskGetName(xTaskGetCurrentTaskHandle());
    const bool wireInitialized = i2cIsInit(bus);
    if (context.firstInvalid) {
        ESP_LOGE(kTelemetryTag,
            "I2C_FIRST_INVALID_STATE: phase=%s client=%s operation=%s address=0x%02X length=%u task=%s core=%d timestamp=%lu wire_initialized=%d ble_initialized=%d ble_connected=%d transaction_sequence=%lu\n",
            i2cTelemetryPhaseName(phase), clientName(client), operation, address,
            static_cast<unsigned>(length), taskName, xPortGetCoreID(),
            static_cast<unsigned long>(millis()), wireInitialized ? 1 : 0,
            bleInitialized ? 1 : 0, bleConnected ? 1 : 0,
            static_cast<unsigned long>(context.sequence));
    }
    if (context.shouldLog) {
        ESP_LOGE(kTelemetryTag,
            "I2C_ERROR: phase=%s client=%s operation=%s bus=%u address=0x%02X length=%u result=%d name=%s nack_compatible=%d elapsed_us=%lu task=%s core=%d client_error_count=%lu sequence=%lu\n",
            i2cTelemetryPhaseName(phase), clientName(client), operation,
            static_cast<unsigned>(bus), address, static_cast<unsigned>(length),
            static_cast<int>(result), esp_err_to_name(result),
            result == ESP_ERR_INVALID_STATE ? 1 : 0,
            static_cast<unsigned long>(elapsedUs), taskName, xPortGetCoreID(),
            static_cast<unsigned long>(context.clientErrorCount),
            static_cast<unsigned long>(context.sequence));
    }
}

void recordAndMaybeLog(
    const char* operation,
    uint8_t bus,
    uint16_t address,
    size_t length,
    size_t requestedRead,
    size_t actualRead,
    esp_err_t result,
    uint32_t elapsedUs)
{
    const I2cClient client = clientForAddress(address);
    const ErrorContext context = recordTransaction(
        client, address, result, requestedRead, actualRead, elapsedUs);
    logTransactionError(context, client, operation, bus, address, length, result, elapsedUs);
}

}  // namespace

extern "C" esp_err_t __real_i2cWrite(
    uint8_t, uint16_t, const uint8_t*, size_t, uint32_t);
extern "C" esp_err_t __real_i2cRead(
    uint8_t, uint16_t, uint8_t*, size_t, uint32_t, size_t*);
extern "C" esp_err_t __real_i2cWriteReadNonStop(
    uint8_t, uint16_t, const uint8_t*, size_t, uint8_t*, size_t, uint32_t, size_t*);

extern "C" esp_err_t __wrap_i2cWrite(
    uint8_t bus,
    uint16_t address,
    const uint8_t* buffer,
    size_t size,
    uint32_t timeoutMs)
{
    const int64_t startedUs = esp_timer_get_time();
    const esp_err_t result = __real_i2cWrite(bus, address, buffer, size, timeoutMs);
    const uint32_t elapsedUs = static_cast<uint32_t>(esp_timer_get_time() - startedUs);
    recordAndMaybeLog("write", bus, address, size, 0, 0, result, elapsedUs);
    return result;
}

extern "C" esp_err_t __wrap_i2cRead(
    uint8_t bus,
    uint16_t address,
    uint8_t* buffer,
    size_t size,
    uint32_t timeoutMs,
    size_t* readCount)
{
    const int64_t startedUs = esp_timer_get_time();
    const esp_err_t result = __real_i2cRead(bus, address, buffer, size, timeoutMs, readCount);
    const uint32_t elapsedUs = static_cast<uint32_t>(esp_timer_get_time() - startedUs);
    const size_t actualRead = readCount == nullptr ? 0 : *readCount;
    recordAndMaybeLog("read", bus, address, size, size, actualRead, result, elapsedUs);
    return result;
}

extern "C" esp_err_t __wrap_i2cWriteReadNonStop(
    uint8_t bus,
    uint16_t address,
    const uint8_t* writeBuffer,
    size_t writeSize,
    uint8_t* readBuffer,
    size_t readSize,
    uint32_t timeoutMs,
    size_t* readCount)
{
    const int64_t startedUs = esp_timer_get_time();
    const esp_err_t result = __real_i2cWriteReadNonStop(
        bus, address, writeBuffer, writeSize, readBuffer, readSize, timeoutMs, readCount);
    const uint32_t elapsedUs = static_cast<uint32_t>(esp_timer_get_time() - startedUs);
    const size_t actualRead = readCount == nullptr ? 0 : *readCount;
    recordAndMaybeLog(
        "write_read", bus, address, writeSize + readSize, readSize, actualRead, result, elapsedUs);
    return result;
}

const char* i2cTelemetryPhaseName(I2cTelemetryPhase phase)
{
    switch (phase) {
        case I2C_PHASE_A_I2C_ONLY: return "PHASE_A_I2C_ONLY";
        case I2C_PHASE_B_BLE_ADVERTISING: return "PHASE_B_BLE_ADVERTISING";
        case I2C_PHASE_C_BLE_CONNECTED: return "PHASE_C_BLE_CONNECTED";
        default: return "PHASE_UNKNOWN";
    }
}

void i2cTelemetryBegin()
{
    currentPhase = I2C_PHASE_A_I2C_ONLY;
    bleInitialized = false;
    bleConnected = false;
    lastSummaryMs = millis();
    ESP_LOGI(kTelemetryTag, "I2C_PHASE: enter=PHASE_A_I2C_ONLY duration_ms=120000 ble=0");
}

void i2cTelemetrySetPhase(I2cTelemetryPhase phase)
{
    currentPhase = phase;
    ESP_LOGI(kTelemetryTag, "I2C_PHASE: enter=%s timestamp=%lu",
        i2cTelemetryPhaseName(phase), static_cast<unsigned long>(millis()));
}

void i2cTelemetrySetBleState(bool initialized, bool connected)
{
    bleInitialized = initialized;
    bleConnected = connected;
}

void i2cTelemetryPrintPhaseSummary(I2cTelemetryPhase phase)
{
    for (uint8_t index = 0; index < I2C_CLIENT_COUNT; ++index) {
        I2cCounters snapshot = {};
        portENTER_CRITICAL(&telemetryMux);
        snapshot = counters[phase][index];
        portEXIT_CRITICAL(&telemetryMux);
        ESP_LOGI(kTelemetryTag,
            "I2C_SUMMARY: phase=%s client=%s transactions=%lu success=%lu invalid_state=%lu timeout=%lu ack_error=%lu short_read=%lu zero_read=%lu max_us=%lu last_error=%d last_error_timestamp=%lu last_task=%s last_address=0x%02X\n",
            i2cTelemetryPhaseName(phase), clientName(static_cast<I2cClient>(index)),
            static_cast<unsigned long>(snapshot.transactionCount),
            static_cast<unsigned long>(snapshot.successCount),
            static_cast<unsigned long>(snapshot.invalidStateCount),
            static_cast<unsigned long>(snapshot.timeoutCount),
            static_cast<unsigned long>(snapshot.ackErrorCount),
            static_cast<unsigned long>(snapshot.shortReadCount),
            static_cast<unsigned long>(snapshot.zeroReadCount),
            static_cast<unsigned long>(snapshot.maxTransactionUs),
            static_cast<int>(snapshot.lastError),
            static_cast<unsigned long>(snapshot.lastErrorTimestamp),
            snapshot.lastTask == nullptr ? "none" : pcTaskGetName(snapshot.lastTask),
            snapshot.lastAddress);
    }
}

void i2cTelemetryPrintSummaryIfDue(uint32_t nowMs)
{
    if ((nowMs - lastSummaryMs) < kSummaryIntervalMs) {
        return;
    }
    lastSummaryMs = nowMs;
    i2cTelemetryPrintPhaseSummary(currentPhase);
}
