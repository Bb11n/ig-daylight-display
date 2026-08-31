#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "ui_state.h"

struct UiSettingsScreenBrightnessControl {
    uint8_t (*getPercent)();
    void (*setPercent)(uint8_t percent);
};

void ui_settings_create(
    lv_obj_t* parent,
    const UiState& state,
    const UiSettingsScreenBrightnessControl& brightnessControl
);

void ui_settings_update(const UiState& state);
void ui_settings_attach_back_button_visual(lv_obj_t* hitArea);
