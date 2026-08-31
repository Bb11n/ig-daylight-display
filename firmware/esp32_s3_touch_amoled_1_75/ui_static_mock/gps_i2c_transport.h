#pragma once

#include <stddef.h>
#include <stdint.h>

enum class GpsI2cTransportStatus : uint8_t {
    OK = 0,
    INVALID_ARGUMENT,
    ZERO_LENGTH,
    LENGTH_EXCEEDS_MAXIMUM,
    OUTPUT_TOO_SMALL,
    BUS_ERROR,
    ZERO_READ,
    SHORT_READ,
};

struct GpsI2cChunkResult {
    bool success;
    size_t bytesRead;
};

using GpsI2cChunkRead = GpsI2cChunkResult (*)(
    void* context,
    uint8_t* destination,
    size_t requestedLength
);

uint32_t gpsI2cParseLittleEndianLength(const uint8_t* lengthBytes);

GpsI2cTransportStatus gpsI2cReadExactChunked(
    GpsI2cChunkRead readChunk,
    void* context,
    uint8_t* output,
    size_t outputSize,
    size_t dataLength,
    size_t chunkCapacity,
    size_t maximumLength
);
