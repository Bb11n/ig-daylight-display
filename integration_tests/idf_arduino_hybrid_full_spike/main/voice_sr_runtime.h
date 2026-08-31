#pragma once

#include "voice_command.h"

struct VoiceCommandRequest {
    VoiceCommand command;
    int value;
};

bool voice_sr_runtime_init();
bool voice_sr_runtime_get_command(VoiceCommandRequest* request);
