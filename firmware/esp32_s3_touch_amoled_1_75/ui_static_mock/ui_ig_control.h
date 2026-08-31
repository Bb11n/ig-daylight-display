#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "ui_state.h"

void ui_ig_control_create(lv_obj_t* parent, const UiState& state);
void ui_ig_control_update_battery(const UiState& state);
void ui_ig_control_loop(uint32_t now);
