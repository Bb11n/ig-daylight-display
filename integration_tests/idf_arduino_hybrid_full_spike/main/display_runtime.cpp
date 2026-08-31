#include "display_runtime.h"

#include "display_ble_server.h"
#include "display_protocol_v2.h"
#include "display_state.h"
#include "esp_log.h"
#include "i2c_telemetry.h"
#include "voice_command_adapter.h"
#include "voice_sr_runtime.h"

#define setup display_runtime_sketch_setup
#define loop display_runtime_sketch_loop
#include "../../../firmware/esp32_s3_touch_amoled_1_75/ui_static_mock/ui_static_mock.ino"
#undef loop
#undef setup

namespace {

constexpr char kRuntimeTag[] = "DISPLAY_RUNTIME";
constexpr uint32_t kDiagnosticPhaseDurationMs = 120000;

uint32_t diagnosticPhaseStartedMs = 0;
I2cTelemetryPhase diagnosticPhase = I2C_PHASE_A_I2C_ONLY;
bool phaseBStarted = false;
bool waitingForPhone = false;
bool phaseCCompleteLogged = false;

}  // namespace

bool display_runtime_init()
{
    ESP_LOGI(kRuntimeTag, "DISPLAY_RUNTIME: init begin stage=%u", static_cast<unsigned>(IG_SPIKE_STAGE));
    i2cTelemetryBegin();
    display_runtime_sketch_setup();

    const bool lvglBufferReady = lvBuf1 != nullptr;
#if ENABLE_DISPLAY_BLE_SERVER
    const bool bleReady = displayBleServerReady;
#else
    const bool bleReady = false;
#endif
    ESP_LOGI(
        kRuntimeTag,
        "DISPLAY_RUNTIME: init done lvgl_buffer=%d ble_enabled=%d ble_ready=%d",
        lvglBufferReady ? 1 : 0,
        ENABLE_DISPLAY_BLE_SERVER ? 1 : 0,
        bleReady ? 1 : 0);
    diagnosticPhaseStartedMs = millis();
    return lvglBufferReady;
}

bool display_runtime_start_ble()
{
    if (displayBleServerReady) {
        return true;
    }

    diagnosticPhase = I2C_PHASE_B_BLE_ADVERTISING;
    i2cTelemetrySetPhase(I2C_PHASE_B_BLE_ADVERTISING);
    display_runtime_log_memory("MEM_BEFORE_BLE");
    displayBleServerReady = displayBleBegin();
    display_runtime_log_memory("MEM_AFTER_BLE");
    i2cTelemetrySetBleState(displayBleServerReady, false);
    ESP_LOGI(kRuntimeTag, "I2C_PHASE: phase_b_ble_begin_result=%d", displayBleServerReady ? 1 : 0);
    phaseBStarted = true;
    diagnosticPhaseStartedMs = millis();
    return displayBleServerReady;
}

void display_runtime_loop()
{
    display_runtime_sketch_loop();
    if (!systemPowerAllowsBusinessWork()) {
        return;
    }

    VoiceCommandRequest voiceRequest = {VOICE_COMMAND_NONE, 0};
    if (voice_sr_runtime_get_command(&voiceRequest) &&
        voiceCommandAdapterDispatch(static_cast<int>(voiceRequest.command), voiceRequest.value)) {
        const DisplayState state = displayStateGetSnapshot();
        ESP_LOGI(
            kRuntimeTag,
            "PROTOCOL_V2_STATE_READY: revision=%lu packet_len=%u projector=%d brightness=%u next_notify_ms_max=500",
            static_cast<unsigned long>(voiceCommandAdapterRevision()),
            static_cast<unsigned>(PROTOCOL_V2_PACKET_LENGTH),
            state.projectionOn ? 1 : 0,
            static_cast<unsigned>(state.brightnessPercent));
    }

    const uint32_t now = millis();
    if (displayBleServerReady) {
        displayBleLoop();
        const bool connected = displayBleIsConnected();
        i2cTelemetrySetBleState(true, connected);

        if (!waitingForPhone &&
            diagnosticPhase == I2C_PHASE_B_BLE_ADVERTISING &&
            (now - diagnosticPhaseStartedMs) >= kDiagnosticPhaseDurationMs) {
            i2cTelemetryPrintPhaseSummary(I2C_PHASE_B_BLE_ADVERTISING);
            waitingForPhone = true;
            ESP_LOGI(kRuntimeTag, "READY_FOR_PHONE_CONNECTION: device=IG_ROUND");
        }

        if (waitingForPhone && connected && diagnosticPhase != I2C_PHASE_C_BLE_CONNECTED) {
            diagnosticPhase = I2C_PHASE_C_BLE_CONNECTED;
            i2cTelemetrySetPhase(I2C_PHASE_C_BLE_CONNECTED);
            diagnosticPhaseStartedMs = now;
        }

        if (diagnosticPhase == I2C_PHASE_C_BLE_CONNECTED && !phaseCCompleteLogged &&
            (now - diagnosticPhaseStartedMs) >= kDiagnosticPhaseDurationMs) {
            phaseCCompleteLogged = true;
            i2cTelemetryPrintPhaseSummary(I2C_PHASE_C_BLE_CONNECTED);
            ESP_LOGI(kRuntimeTag, "I2C_PHASE: PHASE_C_COMPLETE duration_ms=120000");
        }
    }

    i2cTelemetryPrintSummaryIfDue(now);
}

esp_err_t display_runtime_shared_i2c_write(
    uint8_t address,
    const uint8_t* data,
    size_t length,
    uint32_t timeout_ms
)
{
    return sharedI2cWrite(address, data, length, timeout_ms);
}

esp_err_t display_runtime_shared_i2c_read(
    uint8_t address,
    uint8_t* data,
    size_t length,
    uint32_t timeout_ms
)
{
    return sharedI2cRead(address, data, length, timeout_ms);
}
