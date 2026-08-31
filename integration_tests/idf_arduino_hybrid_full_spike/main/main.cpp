#include <Arduino.h>
#include "esp32-hal-alloc-ble-mem.h"
#include "esp32-hal-bt.h"

#include "display_runtime.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "voice_sr_runtime.h"
#include "voice_feedback_runtime.h"

namespace {

constexpr char kTag[] = "PHASE2_SPIKE";

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "PHASE2_SPIKE: app_main begin");

    ESP_LOGI(
        kTag,
        "ARDUINO_BLE_RETENTION: registered=%d before_init_arduino=1 controller_status=%d ble_mem_released=%d",
        bleInUse() ? 1 : 0,
        static_cast<int>(esp_bt_controller_get_status()),
        btMemReleased(BT_MODE_BLE) ? 1 : 0);

    initArduino();
    ESP_LOGI(kTag, "PHASE2_SPIKE: initArduino success");

    Serial.begin(115200);
    delay(100);
    ESP_LOGI(kTag, "arduino_serial_init=1");
    ESP_LOGI(kTag, "console_transport=USB_SERIAL_JTAG");
    ESP_LOGI(
        kTag,
        "stage=%u tick_hz=%u reset_reason=%d psram_total=%u psram_free=%u",
        static_cast<unsigned>(IG_SPIKE_STAGE),
        static_cast<unsigned>(configTICK_RATE_HZ),
        static_cast<int>(esp_reset_reason()),
        static_cast<unsigned>(ESP.getPsramSize()),
        static_cast<unsigned>(ESP.getFreePsram()));

    if (!display_runtime_init()) {
        ESP_LOGE(kTag, "PHASE2_SPIKE_ERR: display_runtime_init failed");
        return;
    }

    ESP_LOGI(kTag, "PHASE2_SPIKE: display runtime ready");
    const bool bleReady = display_runtime_start_ble();
    ESP_LOGI(kTag, "VOICE_BLE_EXPERIMENT_A: ble_ready=%d", bleReady ? 1 : 0);
    if (!bleReady) {
        ESP_LOGE(kTag, "VOICE_BLE_EXPERIMENT_A_ERR: BLE failed before voice initialization");
        for (;;) {
            display_runtime_loop();
        }
    }

    const bool voiceReady = voice_sr_runtime_init();
    ESP_LOGI(kTag, "VOICE_GATE1: init_result=%d", voiceReady ? 1 : 0);
    if (voiceReady) {
        const bool feedbackReady = voiceFeedbackInit();
        ESP_LOGI(kTag, "VOICE_FEEDBACK_GATE: init_result=%d", feedbackReady ? 1 : 0);
#if IG_VOICE_FEEDBACK_GATE_A_TEST
        if (feedbackReady) {
            ESP_LOGI(kTag, "VOICE_FEEDBACK_GATE_A: explicit_test=1 event=projector_on");
            voiceFeedbackEnqueue(VoiceFeedbackEvent::ProjectorOn);
        }
#endif
        ESP_LOGI(kTag, "READY_FOR_VOICE_TEST");
        ESP_LOGI(kTag, "READY_FOR_VOICE_COMMAND_TEST");
    }
    for (;;) {
        display_runtime_loop();
    }
}
