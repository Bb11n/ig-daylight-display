#include "display_state_serial_control.h"

#include <Arduino.h>

#include "debug_log_config.h"
#include "display_state.h"

namespace {

String commandBuffer;

bool applyCommand(const String& command)
{
    if (command == "打开投影" || command == "projection on") {
        displayStateSetProjection(true);
    } else if (command == "关闭投影" || command == "projection off") {
        displayStateSetProjection(false);
    } else if (command == "亮度高" || command == "brightness high") {
        displayStateSetBrightnessHigh(true);
    } else if (command == "亮度低" || command == "brightness low") {
        displayStateSetBrightnessHigh(false);
    } else if (command == "亮度100" || command == "brightness 100") {
        displayStateSetBrightnessPercent(100);
    } else if (command == "亮度40" || command == "brightness 40") {
        displayStateSetBrightnessPercent(40);
    } else if (command == "显示速度" || command == "show speed") {
        displayStateSetShowSpeed(true);
    } else if (command == "隐藏速度" || command == "hide speed") {
        displayStateSetShowSpeed(false);
    } else if (command == "显示距离" || command == "show distance") {
        displayStateSetShowDistance(true);
    } else if (command == "隐藏距离" || command == "hide distance") {
        displayStateSetShowDistance(false);
    } else if (command == "显示时间" || command == "show duration") {
        displayStateSetShowDuration(true);
    } else if (command == "隐藏时间" || command == "hide duration") {
        displayStateSetShowDuration(false);
    } else if (command == "显示卡路里" || command == "show calories") {
        displayStateSetShowCalories(true);
    } else if (command == "隐藏卡路里" || command == "hide calories") {
        displayStateSetShowCalories(false);
    } else if (command == "显示电量" || command == "show battery") {
        displayStateSetShowBattery(true);
    } else if (command == "隐藏电量" || command == "hide battery") {
        displayStateSetShowBattery(false);
    } else if (command == "显示圈数" || command == "show laps") {
        displayStateSetShowLaps(true);
    } else if (command == "隐藏圈数" || command == "hide laps") {
        displayStateSetShowLaps(false);
    } else if (command == "记录一圈" || command == "add lap") {
        displayStateAddLap();
    } else if (command == "重置圈数" || command == "reset laps") {
        displayStateResetLaps();
    } else {
        return false;
    }

    return true;
}

void handleCommand(String command)
{
    command.trim();
    if (command.length() == 0) {
        return;
    }

    if (!applyCommand(command)) {
#if ENABLE_DISPLAY_STATE_SERIAL_LOG
        Serial.printf("ERR: unknown serial command: %s\n", command.c_str());
#endif
        return;
    }

#if ENABLE_DISPLAY_STATE_SERIAL_LOG
    Serial.printf("EVENT: serial_cmd=\"%s\"\n", command.c_str());
#endif
    displayStatePrintSnapshot();
}

} // namespace

void displayStateSerialControlBegin()
{
    Serial.setTimeout(10);
    commandBuffer.reserve(64);
#if ENABLE_DISPLAY_STATE_SERIAL_LOG
    Serial.println("EVENT: serial control ready");
#endif
}

void displayStateSerialControlLoop()
{
    while (Serial.available() > 0) {
        const char input = static_cast<char>(Serial.read());

        if (input == '\r' || input == '\n') {
            handleCommand(commandBuffer);
            commandBuffer = "";
            continue;
        }

        if (commandBuffer.length() < 80) {
            commandBuffer += input;
        } else {
            commandBuffer = "";
#if ENABLE_DISPLAY_STATE_SERIAL_LOG
            Serial.println("ERR: serial command too long");
#endif
        }
    }
}
