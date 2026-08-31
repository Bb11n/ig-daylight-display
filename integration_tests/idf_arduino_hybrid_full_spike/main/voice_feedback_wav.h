#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct VoiceFeedbackWavInfo {
    uint32_t sampleRate;
    uint16_t channels;
    uint16_t bitsPerSample;
    uint32_t dataSize;
    long dataOffset;
};

enum class VoiceFeedbackWavResult {
    Ok,
    InvalidArgument,
    ShortRead,
    InvalidContainer,
    UnsupportedFormat,
    SeekFailed,
};

VoiceFeedbackWavResult voiceFeedbackParseWav(FILE* file, VoiceFeedbackWavInfo* info);
const char* voiceFeedbackWavResultName(VoiceFeedbackWavResult result);
