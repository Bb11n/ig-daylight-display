#include "voice_command_adapter.h"

#include "display_state.h"
#include "voice_command.h"
#include "voice_feedback_runtime.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define ADAPTER_LOGI(...) ESP_LOGI("VOICE_ADAPTER", __VA_ARGS__)
#define ADAPTER_LOGW(...) ESP_LOGW("VOICE_ADAPTER", __VA_ARGS__)
#else
#define ADAPTER_LOGI(...) do {} while (0)
#define ADAPTER_LOGW(...) do {} while (0)
#endif

namespace {

uint32_t stateRevision = 0;

int clampBrightness(int value)
{
    if (value < 0) {
        return 0;
    }
    return value > 100 ? 100 : value;
}

bool isDeferredDisplayCommand(VoiceCommand command)
{
    switch (command) {
        case VOICE_COMMAND_SHOW_BRIGHTNESS:
        case VOICE_COMMAND_SHOW_BATTERY:
        case VOICE_COMMAND_SHOW_SPEED:
        case VOICE_COMMAND_SHOW_TIME:
        case VOICE_COMMAND_SHOW_CALORIES:
        case VOICE_COMMAND_SHOW_DISTANCE:
        case VOICE_COMMAND_SHOW_LAPS:
        case VOICE_COMMAND_SHOW_PAGE_62:
        case VOICE_COMMAND_SHOW_PAGE_63:
            return true;
        default:
            return false;
    }
}

}  // namespace

bool voiceCommandAdapterDispatch(int commandId, int value)
{
    const VoiceCommand command = static_cast<VoiceCommand>(commandId);
    const DisplayState before = displayStateGetSnapshot();

    if (isDeferredDisplayCommand(command)) {
        ADAPTER_LOGW(
            "VOICE_COMMAND_DEFERRED: command_id=%d command_name=%s reason=no_display_state_contract",
            commandId,
            voiceCommandName(command));
        ADAPTER_LOGI(
            "VOICE_COMMAND_ADAPTER: command_id=%d command_name=%s value=%d result=deferred state_revision=%lu",
            commandId,
            voiceCommandName(command),
            value,
            static_cast<unsigned long>(stateRevision));
        return false;
    }

    switch (command) {
        case VOICE_COMMAND_PROJECTOR_ON:
            displayStateSetProjection(true);
            break;
        case VOICE_COMMAND_PROJECTOR_OFF:
            displayStateSetProjection(false);
            break;
        case VOICE_COMMAND_SET_BRIGHTNESS:
            value = clampBrightness(value);
            displayStateSetBrightnessPercent(static_cast<uint8_t>(value));
            break;
        case VOICE_COMMAND_BRIGHTNESS_UP:
            value = clampBrightness(static_cast<int>(before.brightnessPercent) + 10);
            displayStateSetBrightnessPercent(static_cast<uint8_t>(value));
            break;
        case VOICE_COMMAND_BRIGHTNESS_DOWN:
            value = clampBrightness(static_cast<int>(before.brightnessPercent) - 10);
            displayStateSetBrightnessPercent(static_cast<uint8_t>(value));
            break;
        default:
            ADAPTER_LOGW(
                "VOICE_COMMAND_ADAPTER: command_id=%d command_name=unknown value=%d result=unknown state_revision=%lu",
                commandId,
                value,
                static_cast<unsigned long>(stateRevision));
            return false;
    }

    const DisplayState after = displayStateGetSnapshot();
    const bool changed = before.projectionOn != after.projectionOn ||
        before.brightnessPercent != after.brightnessPercent;
    if (changed) {
        ++stateRevision;
        ADAPTER_LOGI(
            "DISPLAY_STATE_UPDATED: revision=%lu proj=%d->%d brightness=%u->%u source=voice",
            static_cast<unsigned long>(stateRevision),
            before.projectionOn ? 1 : 0,
            after.projectionOn ? 1 : 0,
            static_cast<unsigned>(before.brightnessPercent),
            static_cast<unsigned>(after.brightnessPercent));
    }
    ADAPTER_LOGI(
        "VOICE_COMMAND_ADAPTER: command_id=%d command_name=%s value=%d result=%s state_revision=%lu",
        commandId,
        voiceCommandName(command),
        value,
        changed ? "applied" : "no_change",
        static_cast<unsigned long>(stateRevision));

    switch (command) {
        case VOICE_COMMAND_PROJECTOR_ON:
            voiceFeedbackEnqueue(VoiceFeedbackEvent::ProjectorOn);
            break;
        case VOICE_COMMAND_PROJECTOR_OFF:
            voiceFeedbackEnqueue(VoiceFeedbackEvent::ProjectorOff);
            break;
        case VOICE_COMMAND_SET_BRIGHTNESS:
            if (value == 60) {
                voiceFeedbackEnqueue(VoiceFeedbackEvent::BrightnessSet, value);
            }
            break;
        default:
            break;
    }
    return true;
}

uint32_t voiceCommandAdapterRevision()
{
    return stateRevision;
}
