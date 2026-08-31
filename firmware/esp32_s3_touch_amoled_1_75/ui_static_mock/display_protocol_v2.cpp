#include "display_protocol_v2.h"

#include <math.h>

#include "display_product_page.h"
#include "display_state.h"

namespace {

class PacketWriter {
  public:
    PacketWriter(uint8_t* buffer, size_t capacity)
        : buffer_(buffer), capacity_(capacity), offset_(0), ok_(true)
    {
    }

    bool appendU8(uint8_t value)
    {
        if (!reserve(1)) {
            return false;
        }
        buffer_[offset_++] = value;
        return true;
    }

    bool appendU16Le(uint16_t value)
    {
        return appendU8(static_cast<uint8_t>(value & 0xFF)) &&
            appendU8(static_cast<uint8_t>((value >> 8) & 0xFF));
    }

    bool appendU32Le(uint32_t value)
    {
        return appendU16Le(static_cast<uint16_t>(value & 0xFFFF)) &&
            appendU16Le(static_cast<uint16_t>((value >> 16) & 0xFFFF));
    }

    bool appendFieldU8(uint8_t cmd, uint8_t value)
    {
        return appendU8(cmd) && appendU8(1) && appendU8(value);
    }

    bool appendFieldU16Le(uint8_t cmd, uint16_t value)
    {
        return appendU8(cmd) && appendU8(2) && appendU16Le(value);
    }

    bool appendFieldU32Le(uint8_t cmd, uint32_t value)
    {
        return appendU8(cmd) && appendU8(4) && appendU32Le(value);
    }

    size_t size() const
    {
        return offset_;
    }

    bool ok() const
    {
        return ok_;
    }

  private:
    bool reserve(size_t count)
    {
        if (!ok_ || offset_ > capacity_ || count > capacity_ - offset_) {
            ok_ = false;
            return false;
        }
        return true;
    }

    uint8_t* buffer_;
    size_t capacity_;
    size_t offset_;
    bool ok_;
};

uint8_t clampPercent(uint8_t value)
{
    return value > 100 ? 100 : value;
}

uint8_t normalizeDisplayBattery(uint8_t value)
{
    return value == DISPLAY_STATE_BATTERY_UNKNOWN
        ? DISPLAY_STATE_BATTERY_UNKNOWN
        : clampPercent(value);
}

uint16_t roundToUint16(float value)
{
    if (!isfinite(value) || value <= 0.0f) {
        return 0;
    }
    if (value >= 65535.0f) {
        return 65535;
    }
    return static_cast<uint16_t>(roundf(value));
}

uint32_t roundToUint32(float value)
{
    if (!isfinite(value) || value <= 0.0f) {
        return 0;
    }
    if (static_cast<double>(value) >= static_cast<double>(UINT32_MAX)) {
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(round(static_cast<double>(value)));
}

} // namespace

bool displayProtocolV2Pack(
    const DisplayState& state,
    DisplayProductPage page,
    uint8_t* outBuffer,
    size_t outSize,
    size_t* outLength)
{
    if (outLength != nullptr) {
        *outLength = 0;
    }
    if (outBuffer == nullptr || outLength == nullptr ||
        outSize < PROTOCOL_V2_PACKET_LENGTH) {
        return false;
    }

    PacketWriter writer(outBuffer, outSize);
    const uint16_t speedMps = roundToUint16(state.speedKmh / 3.6f);
    const uint32_t distanceMeters = roundToUint32(state.distanceMeters);
    const uint8_t lapCount = state.lapCount > 255U
        ? 255U
        : static_cast<uint8_t>(state.lapCount);
    uint16_t transmittedCalories = state.caloriesKcal;
    if (page == DisplayProductPage::WorkoutGps) {
        transmittedCalories = 0;
    } else if (page == DisplayProductPage::IgControl && state.caloriesKcal == 0) {
        transmittedCalories = 1;
    }

    writer.appendU8(PROTOCOL_V2_HEADER);
    writer.appendU8(PROTOCOL_V2_PAYLOAD_LENGTH);
    writer.appendFieldU8(0x01, state.projectionOn ? 1 : 0);
    writer.appendFieldU8(0x02, clampPercent(state.brightnessPercent));
    writer.appendFieldU8(0x03, normalizeDisplayBattery(state.displayBatteryPercent));
    writer.appendFieldU16Le(0x04, speedMps);
    writer.appendFieldU32Le(0x05, state.workoutDurationSeconds);
    writer.appendFieldU16Le(0x06, transmittedCalories);
    writer.appendFieldU32Le(0x07, distanceMeters);
    writer.appendFieldU8(0x08, lapCount);

    if (!writer.ok() || writer.size() != PROTOCOL_V2_PACKET_LENGTH) {
        return false;
    }

    *outLength = writer.size();
    return true;
}
