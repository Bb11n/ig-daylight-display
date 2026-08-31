#pragma once

#include <lvgl.h>

#include "ui_state.h"

void ui_workout_gps_create(lv_obj_t* parent, const UiState& state);
void ui_workout_gps_update(const UiState& state);
