#include "voice_command.h"

const char* voiceCommandName(VoiceCommand command)
{
    switch (command) {
        case VOICE_COMMAND_PROJECTOR_OFF: return "projector_off";
        case VOICE_COMMAND_PROJECTOR_ON: return "projector_on";
        case VOICE_COMMAND_SHOW_BRIGHTNESS: return "show_brightness";
        case VOICE_COMMAND_SHOW_BATTERY: return "show_battery";
        case VOICE_COMMAND_SHOW_SPEED: return "show_speed";
        case VOICE_COMMAND_SHOW_TIME: return "show_time";
        case VOICE_COMMAND_SHOW_CALORIES: return "show_calories";
        case VOICE_COMMAND_SHOW_DISTANCE: return "show_distance";
        case VOICE_COMMAND_SHOW_LAPS: return "show_laps";
        case VOICE_COMMAND_SHOW_PAGE_62: return "show_page_62";
        case VOICE_COMMAND_SHOW_PAGE_63: return "show_page_63";
        case VOICE_COMMAND_SET_BRIGHTNESS: return "set_brightness";
        case VOICE_COMMAND_BRIGHTNESS_UP: return "brightness_up";
        case VOICE_COMMAND_BRIGHTNESS_DOWN: return "brightness_down";
        default: return "none";
    }
}
