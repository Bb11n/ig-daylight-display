#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "voice_feedback_wav.h"

namespace {

void append16(std::vector<uint8_t>& bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void append32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 24));
}

std::vector<uint8_t> makeWav(uint32_t rate, uint16_t channels, uint16_t bits, uint32_t dataSize)
{
    std::vector<uint8_t> bytes;
    bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
    append32(bytes, 36 + dataSize);
    bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    append32(bytes, 16);
    append16(bytes, 1);
    append16(bytes, channels);
    append32(bytes, rate);
    append32(bytes, rate * channels * bits / 8);
    append16(bytes, channels * bits / 8);
    append16(bytes, bits);
    bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
    append32(bytes, dataSize);
    bytes.resize(bytes.size() + dataSize);
    return bytes;
}

VoiceFeedbackWavResult parse(const std::vector<uint8_t>& bytes, VoiceFeedbackWavInfo* info)
{
    FILE* file = tmpfile();
    assert(file != nullptr);
    assert(fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
    rewind(file);
    const VoiceFeedbackWavResult result = voiceFeedbackParseWav(file, info);
    fclose(file);
    return result;
}

}  // namespace

int main()
{
    VoiceFeedbackWavInfo info = {};
    assert(voiceFeedbackParseWav(nullptr, &info) == VoiceFeedbackWavResult::InvalidArgument);
    assert(voiceFeedbackWavResultName(VoiceFeedbackWavResult::Ok) != nullptr);

    const std::vector<uint8_t> valid = makeWav(16000, 1, 16, 32);
    assert(parse(valid, &info) == VoiceFeedbackWavResult::Ok);
    assert(info.sampleRate == 16000);
    assert(info.channels == 1);
    assert(info.bitsPerSample == 16);
    assert(info.dataSize == 32);

    assert(parse({}, &info) == VoiceFeedbackWavResult::ShortRead);
    std::vector<uint8_t> invalidContainer = valid;
    invalidContainer[0] = 'X';
    assert(parse(invalidContainer, &info) == VoiceFeedbackWavResult::InvalidContainer);
    assert(parse(makeWav(8000, 1, 16, 8), &info) == VoiceFeedbackWavResult::UnsupportedFormat);
    assert(parse(makeWav(16000, 2, 16, 8), &info) == VoiceFeedbackWavResult::UnsupportedFormat);
    assert(parse(makeWav(16000, 1, 8, 8), &info) == VoiceFeedbackWavResult::UnsupportedFormat);

    std::vector<uint8_t> shortFormat(valid.begin(), valid.begin() + 24);
    assert(parse(shortFormat, &info) == VoiceFeedbackWavResult::ShortRead);
    std::vector<uint8_t> noData(valid.begin(), valid.begin() + 36);
    assert(parse(noData, &info) == VoiceFeedbackWavResult::ShortRead);
    return 0;
}
