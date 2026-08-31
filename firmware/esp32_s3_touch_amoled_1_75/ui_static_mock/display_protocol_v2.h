#pragma once

#include <stddef.h>
#include <stdint.h>

struct DisplayState;
enum class DisplayProductPage : uint8_t;

constexpr uint8_t PROTOCOL_V2_HEADER = 0xAB;
constexpr uint8_t PROTOCOL_V2_PAYLOAD_LENGTH = 0x20;
constexpr size_t PROTOCOL_V2_PACKET_LENGTH = 34;
constexpr uint16_t PROTOCOL_V2_PREFERRED_MTU = 64;
constexpr uint16_t PROTOCOL_V2_MIN_MTU = 37;

static_assert(
    PROTOCOL_V2_PACKET_LENGTH ==
        static_cast<size_t>(PROTOCOL_V2_PAYLOAD_LENGTH) + 2U,
    "Protocol v2 packet length must be payload length plus header and length"
);

bool displayProtocolV2Pack(
    const DisplayState& state,
    DisplayProductPage page,
    uint8_t* outBuffer,
    size_t outSize,
    size_t* outLength
);
