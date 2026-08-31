#include "voice_feedback_wav.h"

#include <string.h>

namespace {

constexpr uint32_t kRequiredSampleRate = 16000;
constexpr uint16_t kRequiredChannels = 1;
constexpr uint16_t kRequiredBitsPerSample = 16;

uint16_t readLe16(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readLe32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8) |
        (static_cast<uint32_t>(data[2]) << 16) |
        (static_cast<uint32_t>(data[3]) << 24);
}

bool readExact(FILE* file, void* data, size_t size)
{
    return fread(data, 1, size, file) == size;
}

}  // namespace

VoiceFeedbackWavResult voiceFeedbackParseWav(FILE* file, VoiceFeedbackWavInfo* info)
{
    if (file == nullptr || info == nullptr) {
        return VoiceFeedbackWavResult::InvalidArgument;
    }
    *info = {};

    uint8_t header[12] = {};
    if (!readExact(file, header, sizeof(header))) {
        return VoiceFeedbackWavResult::ShortRead;
    }
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        return VoiceFeedbackWavResult::InvalidContainer;
    }

    bool foundFormat = false;
    uint16_t audioFormat = 0;
    for (;;) {
        uint8_t chunkHeader[8] = {};
        if (!readExact(file, chunkHeader, sizeof(chunkHeader))) {
            return VoiceFeedbackWavResult::ShortRead;
        }
        const uint32_t chunkSize = readLe32(chunkHeader + 4);
        const long chunkStart = ftell(file);
        if (chunkStart < 0) {
            return VoiceFeedbackWavResult::SeekFailed;
        }

        if (memcmp(chunkHeader, "fmt ", 4) == 0) {
            uint8_t format[16] = {};
            if (chunkSize < sizeof(format) || !readExact(file, format, sizeof(format))) {
                return VoiceFeedbackWavResult::ShortRead;
            }
            audioFormat = readLe16(format);
            info->channels = readLe16(format + 2);
            info->sampleRate = readLe32(format + 4);
            info->bitsPerSample = readLe16(format + 14);
            foundFormat = true;
        } else if (memcmp(chunkHeader, "data", 4) == 0) {
            if (!foundFormat) {
                return VoiceFeedbackWavResult::UnsupportedFormat;
            }
            info->dataSize = chunkSize;
            info->dataOffset = chunkStart;
            break;
        }

        const long nextChunk = chunkStart + static_cast<long>(chunkSize + (chunkSize & 1U));
        if (fseek(file, nextChunk, SEEK_SET) != 0) {
            return VoiceFeedbackWavResult::SeekFailed;
        }
    }

    if (audioFormat != 1 || info->sampleRate != kRequiredSampleRate ||
        info->channels != kRequiredChannels ||
        info->bitsPerSample != kRequiredBitsPerSample) {
        return VoiceFeedbackWavResult::UnsupportedFormat;
    }
    return VoiceFeedbackWavResult::Ok;
}

const char* voiceFeedbackWavResultName(VoiceFeedbackWavResult result)
{
    switch (result) {
        case VoiceFeedbackWavResult::Ok: return "ok";
        case VoiceFeedbackWavResult::InvalidArgument: return "invalid_argument";
        case VoiceFeedbackWavResult::ShortRead: return "short_read";
        case VoiceFeedbackWavResult::InvalidContainer: return "invalid_container";
        case VoiceFeedbackWavResult::UnsupportedFormat: return "unsupported_format";
        case VoiceFeedbackWavResult::SeekFailed: return "seek_failed";
    }
    return "unknown";
}
