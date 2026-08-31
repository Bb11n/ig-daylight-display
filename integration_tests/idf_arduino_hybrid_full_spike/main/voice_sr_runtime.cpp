#include "voice_sr_runtime.h"

#include <algorithm>
#include <stdlib.h>

#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "display_runtime.h"
#include "voice_audio_adapter.h"
#include "voice_feedback_runtime.h"

namespace {

constexpr char kTag[] = "VOICE_SR";
constexpr int kSampleRate = 16000;
constexpr uint32_t kStatusIntervalMs = 5000;
constexpr float kDetectionThreshold = 0.50f;
constexpr uint32_t kVoiceTaskStackBytes = 8192;
constexpr UBaseType_t kVoiceTaskPriority = 5;
constexpr BaseType_t kVoiceTaskCore = 1;

const esp_afe_sr_iface_t* afeHandle = nullptr;
esp_afe_sr_data_t* afeData = nullptr;
const esp_mn_iface_t* multinet = nullptr;
model_iface_data_t* multinetData = nullptr;
esp_codec_dev_handle_t microphone = nullptr;
QueueHandle_t commandQueue = nullptr;

struct RuntimeMetrics {
    uint32_t frames;
    uint32_t droppedFrames;
    uint64_t audioReadUs;
    uint64_t afeUs;
    uint64_t multinetUs;
    uint64_t maxAudioReadUs;
    uint64_t maxAfeUs;
    uint64_t maxMultinetUs;
    uint64_t maxLoopUs;
};

RuntimeMetrics metrics = {};

struct CommandPhrase {
    VoiceCommand command;
    const char* phrase;
    int value;
};

constexpr CommandPhrase kCommandPhrases[] = {
    {VOICE_COMMAND_PROJECTOR_OFF, "tou ying guan bi", 0},
    {VOICE_COMMAND_PROJECTOR_OFF, "guan bi tou ying", 0},
    {VOICE_COMMAND_PROJECTOR_ON, "tou ying da kai", 1},
    {VOICE_COMMAND_PROJECTOR_ON, "da kai tou ying", 1},
    {VOICE_COMMAND_SHOW_BRIGHTNESS, "dang qian liang du", 0},
    {VOICE_COMMAND_SHOW_BATTERY, "dang qian dian liang", 0},
    {VOICE_COMMAND_SHOW_SPEED, "dang qian su du", 0},
    {VOICE_COMMAND_SHOW_TIME, "dang qian shi jian", 0},
    {VOICE_COMMAND_SHOW_CALORIES, "dang qian ka lu li", 0},
    {VOICE_COMMAND_SHOW_DISTANCE, "dang qian ju li", 0},
    {VOICE_COMMAND_SHOW_LAPS, "dang qian quan shu", 0},
    {VOICE_COMMAND_SHOW_PAGE_62, "ye mian liu shi er", 0},
    {VOICE_COMMAND_SHOW_PAGE_62, "xian shi ye mian liu shi er", 0},
    {VOICE_COMMAND_SHOW_PAGE_63, "ye mian liu shi san", 0},
    {VOICE_COMMAND_SHOW_PAGE_63, "xian shi ye mian liu shi san", 0},
    {VOICE_COMMAND_SET_BRIGHTNESS, "she zhi liang du liu shi", 60},
    {VOICE_COMMAND_BRIGHTNESS_UP, "ti gao liang du", 0},
    {VOICE_COMMAND_BRIGHTNESS_UP, "liang yi dian", 0},
    {VOICE_COMMAND_BRIGHTNESS_UP, "zeng jia liang du", 0},
    {VOICE_COMMAND_BRIGHTNESS_DOWN, "jiang di liang du", 0},
    {VOICE_COMMAND_BRIGHTNESS_DOWN, "an yi dian", 0},
    {VOICE_COMMAND_BRIGHTNESS_DOWN, "jian shao liang du", 0},
};

int commandValue(VoiceCommand command)
{
    for (const CommandPhrase& entry : kCommandPhrases) {
        if (entry.command == command) {
            return entry.value;
        }
    }
    return 0;
}

bool configureCommands()
{
    if (esp_mn_commands_alloc(multinet, multinetData) != ESP_OK) {
        ESP_LOGE(kTag, "VOICE_SR_ERR: reason=command_alloc_failed");
        return false;
    }
    for (const CommandPhrase& entry : kCommandPhrases) {
        const esp_err_t result = esp_mn_commands_add(static_cast<int>(entry.command), entry.phrase);
        if (result != ESP_OK) {
            ESP_LOGE(
                kTag,
                "VOICE_SR_ERR: reason=command_add_failed command_id=%d phrase=%s err=%s",
                static_cast<int>(entry.command),
                entry.phrase,
                esp_err_to_name(result));
            return false;
        }
    }
    esp_mn_error_t* errors = esp_mn_commands_update();
    if (errors != nullptr && errors->num > 0) {
        ESP_LOGE(kTag, "VOICE_SR_ERR: reason=command_compile_failed count=%d", errors->num);
        return false;
    }
    multinet->print_active_speech_commands(multinetData);
    return true;
}

void printStatusIfDue(uint32_t nowMs, uint32_t& lastStatusMs)
{
    if (nowMs - lastStatusMs < kStatusIntervalMs) {
        return;
    }
    lastStatusMs = nowMs;
    const uint32_t divisor = metrics.frames == 0 ? 1 : metrics.frames;
    ESP_LOGI(
        kTag,
        "VOICE_STATUS: frames=%lu dropped=%lu audio_us=%llu afe_us=%llu mn_us=%llu audio_max_us=%llu afe_max_us=%llu mn_max_us=%llu loop_max_us=%llu stack_hwm=%u core=%d",
        static_cast<unsigned long>(metrics.frames),
        static_cast<unsigned long>(metrics.droppedFrames),
        static_cast<unsigned long long>(metrics.audioReadUs / divisor),
        static_cast<unsigned long long>(metrics.afeUs / divisor),
        static_cast<unsigned long long>(metrics.multinetUs / divisor),
        static_cast<unsigned long long>(metrics.maxAudioReadUs),
        static_cast<unsigned long long>(metrics.maxAfeUs),
        static_cast<unsigned long long>(metrics.maxMultinetUs),
        static_cast<unsigned long long>(metrics.maxLoopUs),
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
        xPortGetCoreID());
    metrics = {};
}

void voiceRecognitionTask(void*)
{
    const int feedSamples = afeHandle->get_feed_chunksize(afeData);
    const int feedChannels = afeHandle->get_feed_channel_num(afeData);
    const size_t sampleCount = static_cast<size_t>(feedSamples) * feedChannels;
    int16_t* pcm = static_cast<int16_t*>(malloc(sampleCount * sizeof(*pcm)));
    if (pcm == nullptr) {
        ESP_LOGE(kTag, "VOICE_SR_ERR: reason=pcm_alloc_failed bytes=%u", static_cast<unsigned>(sampleCount * sizeof(*pcm)));
        vTaskDelete(nullptr);
        return;
    }

    uint32_t lastStatusMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    for (;;) {
        const int64_t readStartedUs = esp_timer_get_time();
        if (esp_codec_dev_read(microphone, pcm, sampleCount * sizeof(*pcm)) != ESP_CODEC_DEV_OK) {
            ++metrics.droppedFrames;
            vTaskDelay(pdMS_TO_TICKS(20));
            printStatusIfDue(static_cast<uint32_t>(esp_timer_get_time() / 1000), lastStatusMs);
            continue;
        }
        const int64_t readDoneUs = esp_timer_get_time();

        afeHandle->feed(afeData, pcm);
        afe_fetch_result_t* result = afeHandle->fetch(afeData);
        const int64_t afeDoneUs = esp_timer_get_time();
        if (result == nullptr || result->data == nullptr) {
            ++metrics.droppedFrames;
            printStatusIfDue(static_cast<uint32_t>(afeDoneUs / 1000), lastStatusMs);
            continue;
        }

        const esp_mn_state_t state = multinet->detect(multinetData, result->data);
        const int64_t multinetDoneUs = esp_timer_get_time();
        ++metrics.frames;
        metrics.audioReadUs += static_cast<uint64_t>(readDoneUs - readStartedUs);
        metrics.afeUs += static_cast<uint64_t>(afeDoneUs - readDoneUs);
        metrics.multinetUs += static_cast<uint64_t>(multinetDoneUs - afeDoneUs);
        metrics.maxAudioReadUs = std::max(
            metrics.maxAudioReadUs,
            static_cast<uint64_t>(readDoneUs - readStartedUs));
        metrics.maxAfeUs = std::max(
            metrics.maxAfeUs,
            static_cast<uint64_t>(afeDoneUs - readDoneUs));
        metrics.maxMultinetUs = std::max(
            metrics.maxMultinetUs,
            static_cast<uint64_t>(multinetDoneUs - afeDoneUs));

        if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t* results = multinet->get_results(multinetData);
            if (results != nullptr && results->num > 0 && results->prob[0] >= kDetectionThreshold) {
                const VoiceCommand command = static_cast<VoiceCommand>(results->command_id[0]);
                const VoiceCommandRequest request = {command, commandValue(command)};
                ESP_LOGI(
                    kTag,
                    "VOICE_RECOGNIZED command_id=%d name=%s confidence=%.2f timestamp=%llu",
                    static_cast<int>(command),
                    voiceCommandName(command),
                    results->prob[0],
                    static_cast<unsigned long long>(multinetDoneUs / 1000));
                if (!voiceFeedbackShouldSuppressCommand()) {
                    xQueueOverwrite(commandQueue, &request);
                }
            }
        }
        const int64_t loopDoneUs = esp_timer_get_time();
        metrics.maxLoopUs = std::max(
            metrics.maxLoopUs,
            static_cast<uint64_t>(loopDoneUs - readStartedUs));
        printStatusIfDue(static_cast<uint32_t>(loopDoneUs / 1000), lastStatusMs);
    }
}

}  // namespace

bool voice_sr_runtime_init()
{
    ESP_LOGI(kTag, "VOICE_SR: init begin source=LisenMoucle gate=recognition_only");
    display_runtime_log_memory("MEM_BEFORE_CODEC");
    microphone = voiceAudioAdapterInit();
    if (microphone == nullptr) {
        ESP_LOGE(kTag, "VOICE_SR_ERR: reason=microphone_init_failed");
        return false;
    }

    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = kSampleRate,
        .mclk_multiple = 0,
    };
    if (esp_codec_dev_open(microphone, &format) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(kTag, "VOICE_SR_ERR: reason=microphone_open_failed");
        return false;
    }
    if (esp_codec_dev_set_in_gain(microphone, 30.0f) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(kTag, "VOICE_SR_ERR: reason=microphone_gain_failed");
        return false;
    }
    display_runtime_log_memory("MEM_AFTER_CODEC");

    srmodel_list_t* models = esp_srmodel_init("model");
    if (models == nullptr) {
        ESP_LOGE(kTag, "VOICE_SR_ERR: reason=model_load_failed");
        return false;
    }

    display_runtime_log_memory("MEM_BEFORE_AFE");
    afe_config_t* afeConfig = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (afeConfig == nullptr) {
        ESP_LOGE(kTag, "VOICE_SR_ERR: reason=afe_config_failed");
        return false;
    }
    afeConfig->aec_init = false;
    afeConfig->wakenet_init = false;
    afeHandle = esp_afe_handle_from_config(afeConfig);
    afeData = afeHandle == nullptr ? nullptr : afeHandle->create_from_config(afeConfig);
    if (afeData == nullptr) {
        ESP_LOGE(kTag, "VOICE_SR_ERR: reason=afe_create_failed");
        return false;
    }
    display_runtime_log_memory("MEM_AFTER_AFE");

    multinet = esp_mn_handle_from_name(const_cast<char*>("mn6_cn"));
    multinetData = multinet == nullptr ? nullptr : multinet->create("mn6_cn", 6000);
    if (multinetData == nullptr) {
        ESP_LOGE(kTag, "VOICE_SR_ERR: reason=multinet6_unavailable");
        return false;
    }
    display_runtime_log_memory("MEM_AFTER_MULTINET");
    multinet->set_det_threshold(multinetData, kDetectionThreshold);
    if (!configureCommands()) {
        return false;
    }

    commandQueue = xQueueCreate(1, sizeof(VoiceCommandRequest));
    if (commandQueue == nullptr) {
        ESP_LOGE(kTag, "VOICE_SR_ERR: reason=queue_create_failed");
        return false;
    }
    ESP_LOGI(
        kTag,
        "VOICE_TASK_CONFIG: name=voice_recognition priority=%u core=%d stack=%lu twdt_subscribed=0 explicit_yield=0",
        static_cast<unsigned>(kVoiceTaskPriority),
        static_cast<int>(kVoiceTaskCore),
        static_cast<unsigned long>(kVoiceTaskStackBytes));
    if (xTaskCreatePinnedToCore(
            voiceRecognitionTask,
            "voice_recognition",
            kVoiceTaskStackBytes,
            nullptr,
            kVoiceTaskPriority,
            nullptr,
            kVoiceTaskCore) != pdPASS) {
        ESP_LOGE(kTag, "VOICE_SR_ERR: reason=task_create_failed");
        vQueueDelete(commandQueue);
        commandQueue = nullptr;
        return false;
    }
    display_runtime_log_memory("MEM_VOICE_RUNNING");

    ESP_LOGI(kTag, "VOICE_SR: ready model=mn6_cn threshold=0.50 wake_word=disabled");
    return true;
}

bool voice_sr_runtime_get_command(VoiceCommandRequest* request)
{
    if (request != nullptr && commandQueue != nullptr &&
        xQueueReceive(commandQueue, request, 0) == pdPASS) {
        return true;
    }
    return false;
}
