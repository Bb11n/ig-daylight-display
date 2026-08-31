#include "gps_i2c_transport.h"

#include <algorithm>

uint32_t gpsI2cParseLittleEndianLength(const uint8_t* lengthBytes)
{
    if (lengthBytes == nullptr) {
        return 0;
    }

    return static_cast<uint32_t>(lengthBytes[0]) |
        (static_cast<uint32_t>(lengthBytes[1]) << 8) |
        (static_cast<uint32_t>(lengthBytes[2]) << 16) |
        (static_cast<uint32_t>(lengthBytes[3]) << 24);
}

GpsI2cTransportStatus gpsI2cReadExactChunked(
    GpsI2cChunkRead readChunk,
    void* context,
    uint8_t* output,
    size_t outputSize,
    size_t dataLength,
    size_t chunkCapacity,
    size_t maximumLength
)
{
    if (readChunk == nullptr || output == nullptr || chunkCapacity == 0 || maximumLength == 0) {
        return GpsI2cTransportStatus::INVALID_ARGUMENT;
    }
    if (dataLength == 0) {
        return GpsI2cTransportStatus::ZERO_LENGTH;
    }
    if (dataLength > maximumLength) {
        return GpsI2cTransportStatus::LENGTH_EXCEEDS_MAXIMUM;
    }
    if (dataLength > outputSize) {
        return GpsI2cTransportStatus::OUTPUT_TOO_SMALL;
    }

    size_t offset = 0;
    while (offset < dataLength) {
        const size_t requested = std::min(chunkCapacity, dataLength - offset);
        const GpsI2cChunkResult result = readChunk(context, output + offset, requested);
        if (!result.success) {
            return GpsI2cTransportStatus::BUS_ERROR;
        }
        if (result.bytesRead == 0) {
            return GpsI2cTransportStatus::ZERO_READ;
        }
        if (result.bytesRead != requested) {
            return GpsI2cTransportStatus::SHORT_READ;
        }
        offset += result.bytesRead;
    }

    return GpsI2cTransportStatus::OK;
}
