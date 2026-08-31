#include "voice_feedback_runtime.h"

#include <atomic>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "voice_audio_adapter.h"
#include "voice_feedback_wav.h"

namespace {

constexpr char kTag[] = "VOICE_FEEDBACK";
constexpr char kMountPoint[] = "/sdcard";
constexpr size_t kStreamBufferBytes = 1024;
constexpr int kVolumePercent = 100;
constexpr uint32_t kCooldownMs = 500;
constexpr uint32_t kTaskStackBytes = 4096;
constexpr UBaseType_t kTaskPriority = 4;

struct FeedbackRequest {
    VoiceFeedbackEvent event;
    int value;
};

QueueHandle_t feedbackQueue = nullptr;
esp_codec_dev_handle_t speaker = nullptr;
sdmmc_card_t* sdCard = nullptr;
std::atomic<bool> feedbackPlaying{false};
std::atomic<bool> feedbackPending{false};
std::atomic<int64_t> suppressUntilUs{0};
std::atomic<uint32_t> suppressedCommands{0};
bool feedbackReady = false;

void logDirectory(const char* path, const char* label)
{
    errno = 0;
    DIR* directory = opendir(path);
    if (directory == nullptr) {
        const int error = errno;
        ESP_LOGE(kTag, "VOICE_FEEDBACK_SD: list=%s path=%s result=error errno=%d text=%s",
            label, path, error, strerror(error));
        return;
    }

    ESP_LOGI(kTag, "VOICE_FEEDBACK_SD: list=%s path=%s result=begin", label, path);
    while (dirent* entry = readdir(directory)) {
        char entryPath[256] = {};
        const int written = snprintf(entryPath, sizeof(entryPath), "%s/%s", path, entry->d_name);
        struct stat info = {};
        errno = 0;
        const int statResult = written > 0 && static_cast<size_t>(written) < sizeof(entryPath)
            ? stat(entryPath, &info)
            : -1;
        const int statError = statResult == 0 ? 0 : errno;
        ESP_LOGI(
            kTag,
            "VOICE_FEEDBACK_SD_ENTRY: list=%s name=\"%s\" type=%s size=%lld stat_errno=%d stat_text=%s",
            label,
            entry->d_name,
            statResult == 0 && S_ISDIR(info.st_mode) ? "dir" : "file",
            statResult == 0 ? static_cast<long long>(info.st_size) : -1LL,
            statError,
            statError == 0 ? "OK" : strerror(statError));
    }
    closedir(directory);
    ESP_LOGI(kTag, "VOICE_FEEDBACK_SD: list=%s path=%s result=end", label, path);
}

void logPathProbe(const char* path)
{
    struct stat info = {};
    errno = 0;
    const int statResult = stat(path, &info);
    const int statError = statResult == 0 ? 0 : errno;
    ESP_LOGI(
        kTag,
        "VOICE_FEEDBACK_SD_PROBE: path=\"%s\" op=stat result=%d size=%lld errno=%d text=%s",
        path,
        statResult,
        statResult == 0 ? static_cast<long long>(info.st_size) : -1LL,
        statError,
        statError == 0 ? "OK" : strerror(statError));

    errno = 0;
    FILE* file = fopen(path, "rb");
    const int openError = file == nullptr ? errno : 0;
    ESP_LOGI(
        kTag,
        "VOICE_FEEDBACK_SD_PROBE: path=\"%s\" op=fopen mode=rb result=%s errno=%d text=%s",
        path,
        file == nullptr ? "error" : "ok",
        openError,
        openError == 0 ? "OK" : strerror(openError));
    if (file != nullptr) {
        fclose(file);
    }
}

void logSdDiagnostics()
{
    const uint64_t capacityBytes = sdCard == nullptr
        ? 0
        : static_cast<uint64_t>(sdCard->csd.capacity) *
            static_cast<uint64_t>(sdCard->csd.sector_size);
    ESP_LOGI(
        kTag,
        "VOICE_FEEDBACK_SD: mount_point=%s filesystem=FAT transport=SDMMC_1BIT capacity_bytes=%llu capacity_mib=%llu",
        kMountPoint,
        static_cast<unsigned long long>(capacityBytes),
        static_cast<unsigned long long>(capacityBytes / (1024ULL * 1024ULL)));
    logDirectory(kMountPoint, "root");
    logDirectory("/sdcard/voice", "voice");
    logPathProbe("/sdcard/voice/projector_on.wav");
    logPathProbe("/sdcard/voice/projector_off.wav");
    logPathProbe("/sdcard/voice/brightness.wav");
}

bool mountSdCard()
{
    esp_vfs_fat_sdmmc_mount_config_t mountConfig = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slotConfig = SDMMC_SLOT_CONFIG_DEFAULT();
    slotConfig.clk = GPIO_NUM_2;
    slotConfig.cmd = GPIO_NUM_1;
    slotConfig.d0 = GPIO_NUM_3;
    slotConfig.d1 = GPIO_NUM_NC;
    slotConfig.d2 = GPIO_NUM_NC;
    slotConfig.d3 = GPIO_NUM_NC;
    slotConfig.d4 = GPIO_NUM_NC;
    slotConfig.d5 = GPIO_NUM_NC;
    slotConfig.d6 = GPIO_NUM_NC;
    slotConfig.d7 = GPIO_NUM_NC;
    slotConfig.cd = SDMMC_SLOT_NO_CD;
    slotConfig.wp = SDMMC_SLOT_NO_WP;
    slotConfig.width = 1;

    const esp_err_t result = esp_vfs_fat_sdmmc_mount(
        kMountPoint, &host, &slotConfig, &mountConfig, &sdCard);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "VOICE_FEEDBACK_ERR: reason=sd_mount_failed err=%s", esp_err_to_name(result));
        return false;
    }
    ESP_LOGI(kTag, "VOICE_FEEDBACK: sd_mounted path=%s mode=sdmmc_1bit", kMountPoint);
    logSdDiagnostics();
    return true;
}

bool playWav(const FeedbackRequest& request)
{
    const char* path = voiceFeedbackFileForEvent(request.event, request.value);
    if (path == nullptr) {
        ESP_LOGW(kTag, "VOICE_FEEDBACK_ERR: reason=no_file_mapping event=%s value=%d",
            voiceFeedbackEventName(request.event), request.value);
        return false;
    }

    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        ESP_LOGE(kTag, "VOICE_FEEDBACK_ERR: reason=file_missing file=%s", path);
        return false;
    }
    VoiceFeedbackWavInfo wav = {};
    const VoiceFeedbackWavResult parseResult = voiceFeedbackParseWav(file, &wav);
    if (parseResult != VoiceFeedbackWavResult::Ok) {
        ESP_LOGE(kTag, "VOICE_FEEDBACK_ERR: reason=%s file=%s",
            voiceFeedbackWavResultName(parseResult), path);
        fclose(file);
        return false;
    }

    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = static_cast<uint8_t>(wav.bitsPerSample),
        .channel = static_cast<uint8_t>(wav.channels),
        .channel_mask = 0,
        .sample_rate = wav.sampleRate,
        .mclk_multiple = 0,
    };
    if (esp_codec_dev_open(speaker, &format) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(kTag, "VOICE_FEEDBACK_ERR: reason=speaker_open_failed file=%s", path);
        fclose(file);
        return false;
    }
    if (esp_codec_dev_set_out_vol(speaker, kVolumePercent) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(kTag, "VOICE_FEEDBACK_ERR: reason=volume_failed file=%s", path);
        esp_codec_dev_close(speaker);
        fclose(file);
        return false;
    }

    uint8_t buffer[kStreamBufferBytes] = {};
    uint32_t remaining = wav.dataSize;
    bool success = true;
    while (remaining > 0) {
        const size_t requested = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        const size_t bytesRead = fread(buffer, 1, requested, file);
        if (bytesRead != requested) {
            ESP_LOGE(kTag, "VOICE_FEEDBACK_ERR: reason=short_read file=%s remaining=%lu read=%u",
                path, static_cast<unsigned long>(remaining), static_cast<unsigned>(bytesRead));
            success = false;
            break;
        }
        if (esp_codec_dev_write(speaker, buffer, static_cast<int>(bytesRead)) != ESP_CODEC_DEV_OK) {
            ESP_LOGE(kTag, "VOICE_FEEDBACK_ERR: reason=speaker_write_failed file=%s", path);
            success = false;
            break;
        }
        remaining -= static_cast<uint32_t>(bytesRead);
    }
    esp_codec_dev_close(speaker);
    fclose(file);
    return success;
}

void feedbackTask(void*)
{
    FeedbackRequest request = {};
    for (;;) {
        if (xQueueReceive(feedbackQueue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        feedbackPending.store(false);
        feedbackPlaying.store(true);
        const int64_t startedUs = esp_timer_get_time();
        const char* path = voiceFeedbackFileForEvent(request.event, request.value);
        ESP_LOGI(kTag, "VOICE_FEEDBACK: event=%s file=%s playing=1",
            voiceFeedbackEventName(request.event), path == nullptr ? "none" : path);
        const bool success = playWav(request);
        const int64_t finishedUs = esp_timer_get_time();
        feedbackPlaying.store(false);
        suppressUntilUs.store(finishedUs + static_cast<int64_t>(kCooldownMs) * 1000);
        ESP_LOGI(kTag, "VOICE_FEEDBACK_DONE: event=%s duration_ms=%llu result=%s suppressed=%lu",
            voiceFeedbackEventName(request.event),
            static_cast<unsigned long long>((finishedUs - startedUs) / 1000),
            success ? "ok" : "error",
            static_cast<unsigned long>(suppressedCommands.load()));
    }
}

}  // namespace

const char* voiceFeedbackEventName(VoiceFeedbackEvent event)
{
    switch (event) {
        case VoiceFeedbackEvent::ProjectorOn: return "projector_on";
        case VoiceFeedbackEvent::ProjectorOff: return "projector_off";
        case VoiceFeedbackEvent::BrightnessSet: return "brightness_set";
        case VoiceFeedbackEvent::CommandUnsupported: return "command_unsupported";
        case VoiceFeedbackEvent::PlaybackError: return "playback_error";
    }
    return "unknown";
}

const char* voiceFeedbackFileForEvent(VoiceFeedbackEvent event, int value)
{
    switch (event) {
        case VoiceFeedbackEvent::ProjectorOn: return "/sdcard/voice/projector_on.wav";
        case VoiceFeedbackEvent::ProjectorOff: return "/sdcard/voice/projector_off.wav";
        case VoiceFeedbackEvent::BrightnessSet:
            return value == 60 ? "/sdcard/voice/brightness.wav" : nullptr;
        case VoiceFeedbackEvent::CommandUnsupported:
        case VoiceFeedbackEvent::PlaybackError:
            return nullptr;
    }
    return nullptr;
}

bool voiceFeedbackInit()
{
    if (feedbackReady) {
        return true;
    }
    if (!mountSdCard()) {
        return false;
    }
    speaker = voiceAudioAdapterSpeakerInit();
    if (speaker == nullptr) {
        ESP_LOGE(kTag, "VOICE_FEEDBACK_ERR: reason=speaker_init_failed");
        return false;
    }
    feedbackQueue = xQueueCreate(1, sizeof(FeedbackRequest));
    if (feedbackQueue == nullptr) {
        ESP_LOGE(kTag, "VOICE_FEEDBACK_ERR: reason=queue_create_failed");
        return false;
    }
    if (xTaskCreate(feedbackTask, "voice_feedback", kTaskStackBytes, nullptr,
            kTaskPriority, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "VOICE_FEEDBACK_ERR: reason=task_create_failed");
        vQueueDelete(feedbackQueue);
        feedbackQueue = nullptr;
        return false;
    }
    feedbackReady = true;
    ESP_LOGI(kTag, "VOICE_FEEDBACK: ready queue_depth=1 overwrite=1 stack=4096 priority=4 affinity=none cooldown_ms=500");
    return true;
}

bool voiceFeedbackEnqueue(VoiceFeedbackEvent event, int value)
{
    if (!feedbackReady || feedbackQueue == nullptr || voiceFeedbackFileForEvent(event, value) == nullptr) {
        return false;
    }
    const FeedbackRequest request = {event, value};
    feedbackPending.store(true);
    return xQueueOverwrite(feedbackQueue, &request) == pdPASS;
}

bool voiceFeedbackIsPlaying()
{
    return feedbackPlaying.load();
}

bool voiceFeedbackShouldSuppressCommand()
{
    const bool suppress = feedbackPending.load() || feedbackPlaying.load() ||
        esp_timer_get_time() < suppressUntilUs.load();
    if (suppress) {
        const uint32_t count = suppressedCommands.fetch_add(1) + 1;
        ESP_LOGW(kTag, "VOICE_COMMAND_SUPPRESSED: reason=feedback_playing count=%lu",
            static_cast<unsigned long>(count));
    }
    return suppress;
}
