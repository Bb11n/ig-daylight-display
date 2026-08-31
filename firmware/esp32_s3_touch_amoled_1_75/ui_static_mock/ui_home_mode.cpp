#include "ui_home_mode.h"

#include <stdio.h>

#include "ui_battery_ring.h"
#include "ui_theme.h"

namespace {

UiBatteryRing batteryRing = {};

lv_obj_t* create_label(
    lv_obj_t* parent,
    const char* text,
    int16_t y,
    const lv_font_t* font,
    lv_color_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, ui_theme::kScreenWidth);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_pos(label, 0, y);
    return label;
}

void create_back_button_visual(lv_obj_t* parent)
{
    lv_obj_t* button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 60, 60);
    lv_obj_set_pos(button, 58, 58);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, ui_theme::surface(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, ui_theme::surfaceAlt(), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_90, 0);

    lv_obj_t* icon = lv_label_create(button);
    lv_label_set_text(icon, "<");
    lv_obj_set_style_text_font(icon, ui_theme::fontLarge(), 0);
    lv_obj_set_style_text_color(icon, ui_theme::text(), 0);
    lv_obj_center(icon);
}

void create_left_status(lv_obj_t* parent, const char* text)
{
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 164, 60);
    lv_obj_set_pos(panel, 66, 286);
    lv_obj_set_style_radius(panel, 14, 0);
    lv_obj_set_style_bg_color(panel, ui_theme::surface(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_20, 0);

    lv_obj_t* label = lv_label_create(panel);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_theme::fontSmall(), 0);
    lv_obj_set_style_text_color(label, ui_theme::muted(), 0);
    lv_obj_center(label);
}

void create_brightness_panel(lv_obj_t* parent, int brightness)
{
    char valueText[12];
    snprintf(valueText, sizeof(valueText), "%d%%", brightness);

    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 136, 72);
    lv_obj_set_pos(panel, 244, 280);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 5, 0);

    lv_obj_t* value = lv_label_create(panel);
    lv_label_set_text(value, valueText);
    lv_obj_set_style_text_font(value, ui_theme::fontLarge(), 0);
    lv_obj_set_style_text_color(value, ui_theme::text(), 0);

    lv_obj_t* unit = lv_label_create(panel);
    lv_label_set_text(unit, "BRIGHT");
    lv_obj_set_style_text_font(unit, ui_theme::fontSmall(), 0);
    lv_obj_set_style_text_color(unit, ui_theme::muted(), 0);
}

void create_action_capsule(lv_obj_t* parent, const char* text)
{
    lv_obj_t* capsule = lv_obj_create(parent);
    lv_obj_remove_style_all(capsule);
    lv_obj_set_size(capsule, 190, 42);
    lv_obj_set_pos(capsule, 138, 374);
    lv_obj_set_style_radius(capsule, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(capsule, ui_theme::surface(), 0);
    lv_obj_set_style_bg_opa(capsule, LV_OPA_70, 0);
    lv_obj_set_style_border_width(capsule, 1, 0);
    lv_obj_set_style_border_color(capsule, ui_theme::green(), 0);
    lv_obj_set_style_border_opa(capsule, LV_OPA_30, 0);

    lv_obj_t* label = lv_label_create(capsule);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_theme::fontSmall(), 0);
    lv_obj_set_style_text_color(label, ui_theme::green(), 0);
    lv_obj_center(label);
}

} // namespace

void ui_home_mode_create(lv_obj_t* parent, const UiState& state)
{
    lv_obj_remove_style_all(parent);
    lv_obj_set_size(parent, ui_theme::kScreenWidth, ui_theme::kScreenHeight);
    lv_obj_set_style_bg_color(parent, ui_theme::bg(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    uiBatteryRingCreate(parent, &batteryRing);
    ui_home_mode_update_battery(state);
    create_back_button_visual(parent);

    create_label(parent, state.indoorStatus, 84, ui_theme::fontBody(), ui_theme::muted());

    char timeText[8];
    snprintf(timeText, sizeof(timeText), "%02d:%02d", state.homeHour, state.homeMinute);
    create_label(parent, timeText, 146, ui_theme::fontTime(), ui_theme::text());
    create_label(parent, state.homeModeText, 218, ui_theme::fontBody(), ui_theme::green());

    create_left_status(parent, state.homeIgStatus);
    create_brightness_panel(parent, state.homeBrightness);
    create_action_capsule(parent, state.homeActionText);
}

void ui_home_mode_update_battery(const UiState& state)
{
    uiBatteryRingUpdate(
        &batteryRing,
        uiBatteryRingBatteryValid(
            state.pmuReady,
            state.batteryVoltage,
            state.batteryPercent
        ),
        state.batteryPercent
    );
}
