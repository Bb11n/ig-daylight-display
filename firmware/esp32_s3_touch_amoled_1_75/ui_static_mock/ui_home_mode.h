#pragma once

#include <lvgl.h>

#include "ui_state.h"

void ui_home_mode_create(lv_obj_t* parent, const UiState& state);
void ui_home_mode_update_battery(const UiState& state);
