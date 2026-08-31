#include <assert.h>

#include "Arduino.h"
#include "display_protocol_v2.h"
#include "display_state.h"
#include "voice_command.h"
#include "voice_command_adapter.h"
#include "voice_feedback_runtime.h"

TestSerial Serial;
static VoiceFeedbackEvent lastFeedbackEvent = VoiceFeedbackEvent::PlaybackError;
static int lastFeedbackValue = -1;
static int feedbackCount = 0;

bool voiceFeedbackEnqueue(VoiceFeedbackEvent event, int value)
{
    lastFeedbackEvent = event;
    lastFeedbackValue = value;
    ++feedbackCount;
    return true;
}

int main()
{
    displayStateBegin();

    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_PROJECTOR_ON, 1));
    assert(displayStateGetSnapshot().projectionOn);
    assert(lastFeedbackEvent == VoiceFeedbackEvent::ProjectorOn);

    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_PROJECTOR_OFF, 0));
    assert(!displayStateGetSnapshot().projectionOn);
    assert(lastFeedbackEvent == VoiceFeedbackEvent::ProjectorOff);

    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_SET_BRIGHTNESS, 0));
    assert(displayStateGetSnapshot().brightnessPercent == 0);

    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_SET_BRIGHTNESS, 60));
    assert(displayStateGetSnapshot().brightnessPercent == 60);
    assert(lastFeedbackEvent == VoiceFeedbackEvent::BrightnessSet);
    assert(lastFeedbackValue == 60);

    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_SET_BRIGHTNESS, 100));
    assert(displayStateGetSnapshot().brightnessPercent == 100);

    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_SET_BRIGHTNESS, -5));
    assert(displayStateGetSnapshot().brightnessPercent == 0);

    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_SET_BRIGHTNESS, 140));
    assert(displayStateGetSnapshot().brightnessPercent == 100);

    const int feedbackBeforeRelativeBrightness = feedbackCount;
    displayStateSetBrightnessPercent(50);
    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_BRIGHTNESS_UP, 0));
    assert(displayStateGetSnapshot().brightnessPercent == 60);
    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_BRIGHTNESS_DOWN, 0));
    assert(displayStateGetSnapshot().brightnessPercent == 50);

    displayStateSetBrightnessPercent(95);
    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_BRIGHTNESS_UP, 0));
    assert(displayStateGetSnapshot().brightnessPercent == 100);

    displayStateSetBrightnessPercent(5);
    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_BRIGHTNESS_DOWN, 0));
    assert(displayStateGetSnapshot().brightnessPercent == 0);
    assert(feedbackCount == feedbackBeforeRelativeBrightness);

    const uint32_t revisionBeforeRejectedCommands = voiceCommandAdapterRevision();
    const int feedbackBeforeRejectedCommands = feedbackCount;
    assert(!voiceCommandAdapterDispatch(999, 0));
    assert(!voiceCommandAdapterDispatch(VOICE_COMMAND_SHOW_SPEED, 0));
    assert(voiceCommandAdapterRevision() == revisionBeforeRejectedCommands);
    assert(feedbackCount == feedbackBeforeRejectedCommands);

    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_PROJECTOR_ON, 1));
    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_SET_BRIGHTNESS, 60));
    assert(voiceCommandAdapterDispatch(VOICE_COMMAND_PROJECTOR_OFF, 0));

    const DisplayState finalState = displayStateGetSnapshot();
    assert(!finalState.projectionOn);
    assert(finalState.brightnessPercent == 60);

    uint8_t packet[PROTOCOL_V2_PACKET_LENGTH] = {};
    size_t packetLength = 0;
    assert(displayStatePackProtocolV2Packet(packet, sizeof(packet), &packetLength));
    assert(packetLength == PROTOCOL_V2_PACKET_LENGTH);
    assert(packet[0] == 0xAB && packet[1] == 0x20);
    assert(packet[4] == 0);
    assert(packet[7] == 60);
    assert(packet[31] == 0x08 && packet[32] == 0x01 && packet[33] == 0x00);
    return 0;
}
