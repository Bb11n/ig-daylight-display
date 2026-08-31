#pragma once

#include <stdint.h>

enum class VoiceFeedbackEvent : uint8_t {
    ProjectorOn,
    ProjectorOff,
    BrightnessSet,
    CommandUnsupported,
    PlaybackError,
};

bool voiceFeedbackInit();
bool voiceFeedbackEnqueue(VoiceFeedbackEvent event, int value = 0);
bool voiceFeedbackIsPlaying();
bool voiceFeedbackShouldSuppressCommand();
const char* voiceFeedbackEventName(VoiceFeedbackEvent event);
const char* voiceFeedbackFileForEvent(VoiceFeedbackEvent event, int value);
