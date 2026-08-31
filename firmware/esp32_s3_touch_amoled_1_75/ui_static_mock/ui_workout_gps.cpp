#include "ui_workout_gps.h"

#include <stdio.h>

#include "ui_battery_ring.h"
#include "ui_theme.h"

namespace {

lv_obj_t* pageRoot = nullptr;
lv_obj_t* gpsStatusLabel = nullptr;
lv_obj_t* satsPowerLabel = nullptr;
lv_obj_t* speedValueLabel = nullptr;
lv_obj_t* distanceValueLabel = nullptr;
lv_obj_t* elapsedValueLabel = nullptr;
lv_obj_t* recordingStatusLabel = nullptr;
lv_obj_t* playIconLabel = nullptr;
UiBatteryRing batteryRing = {};
bool pageActive = false;

void clear_object_references()
{
    pageRoot = nullptr;
    gpsStatusLabel = nullptr;
    satsPowerLabel = nullptr;
    speedValueLabel = nullptr;
    distanceValueLabel = nullptr;
    elapsedValueLabel = nullptr;
    recordingStatusLabel = nullptr;
    playIconLabel = nullptr;
    uiBatteryRingReset(&batteryRing);
    pageActive = false;
}

void on_page_deleted(lv_event_t* event)
{
    if (lv_event_get_target(event) != pageRoot) {
        return;
    }
    clear_object_references();
}

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

lv_obj_t* create_metric_column(
    lv_obj_t* parent,
    const char* value,
    const char* unit,
    int16_t x)
{
    lv_obj_t* column = lv_obj_create(parent);
    lv_obj_remove_style_all(column);
    lv_obj_set_size(column, 136, 72);
    lv_obj_set_pos(column, x, 266);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(column, 4, 0);

    lv_obj_t* valueLabel = lv_label_create(column);
    lv_label_set_text(valueLabel, value);
    lv_obj_set_style_text_font(valueLabel, ui_theme::fontLarge(), 0);
    lv_obj_set_style_text_color(valueLabel, ui_theme::text(), 0);

    lv_obj_t* unitLabel = lv_label_create(column);
    lv_label_set_text(unitLabel, unit);
    lv_obj_set_style_text_font(unitLabel, ui_theme::fontSmall(), 0);
    lv_obj_set_style_text_color(unitLabel, ui_theme::muted(), 0);

    return valueLabel;
}

lv_obj_t* create_status_capsule(lv_obj_t* parent, const char* label)
{
    lv_obj_t* capsule = lv_obj_create(parent);
    lv_obj_remove_style_all(capsule);
    lv_obj_set_size(capsule, 142, 42);
    lv_obj_set_pos(capsule, 162, 326);
    lv_obj_set_style_radius(capsule, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(capsule, ui_theme::surface(), 0);
    lv_obj_set_style_bg_opa(capsule, LV_OPA_70, 0);
    lv_obj_set_style_border_width(capsule, 1, 0);
    lv_obj_set_style_border_color(capsule, ui_theme::surfaceAlt(), 0);

    lv_obj_t* text = lv_label_create(capsule);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_font(text, ui_theme::fontSmall(), 0);
    lv_obj_set_style_text_color(text, ui_theme::muted(), 0);
    lv_obj_center(text);

    return text;
}

void format_elapsed(uint32_t elapsedSeconds, char* buffer, size_t bufferSize)
{
    const uint32_t hours = elapsedSeconds / 3600;
    const uint32_t minutes = (elapsedSeconds / 60) % 60;
    const uint32_t seconds = elapsedSeconds % 60;
    snprintf(
        buffer,
        bufferSize,
        "%02lu:%02lu:%02lu",
        static_cast<unsigned long>(hours),
        static_cast<unsigned long>(minutes),
        static_cast<unsigned long>(seconds)
    );
}

void format_sats_power(const UiState& state, char* buffer, size_t bufferSize)
{
    char satsText[12];
    if (state.satellites > 0) {
        snprintf(satsText, sizeof(satsText), "%d SATS", state.satellites);
    } else {
        snprintf(satsText, sizeof(satsText), "SAT --");
    }

    char powerText[12];
    if (!state.pmuReady) {
        snprintf(powerText, sizeof(powerText), "BAT --");
    } else if (state.vbusIn) {
        snprintf(powerText, sizeof(powerText), "USB");
    } else if (state.batteryPercent >= 0) {
        snprintf(powerText, sizeof(powerText), "BAT %d%%", state.batteryPercent);
    } else {
        snprintf(powerText, sizeof(powerText), "BAT --");
    }

    snprintf(buffer, bufferSize, "%s  %s", satsText, powerText);
}

lv_obj_t* create_play_button(lv_obj_t* parent, bool running)
{
    lv_obj_t* button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 70, 70);
    lv_obj_set_pos(button, 198, 374);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, ui_theme::surface(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_80, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, ui_theme::green(), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_70, 0);

    lv_obj_t* icon = lv_label_create(button);
    lv_label_set_text(icon, running ? "||" : ">");
    lv_obj_set_style_text_font(icon, ui_theme::fontLarge(), 0);
    lv_obj_set_style_text_color(icon, ui_theme::green(), 0);
    lv_obj_center(icon);

    return icon;
}

} // namespace

void ui_workout_gps_create(lv_obj_t* parent, const UiState& state)
{
    if (pageActive && pageRoot != nullptr) {
        ui_workout_gps_update(state);
        return;
    }
    clear_object_references();

    pageRoot = lv_obj_create(parent);
    lv_obj_remove_style_all(pageRoot);
    lv_obj_set_pos(pageRoot, 0, 0);
    lv_obj_set_size(pageRoot, ui_theme::kScreenWidth, ui_theme::kScreenHeight);
    lv_obj_set_style_bg_color(pageRoot, ui_theme::bg(), 0);
    lv_obj_set_style_bg_opa(pageRoot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(pageRoot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(pageRoot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pageRoot, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(pageRoot, on_page_deleted, LV_EVENT_DELETE, nullptr);
    uiBatteryRingCreate(pageRoot, &batteryRing);

    create_back_button_visual(pageRoot);

    gpsStatusLabel = create_label(pageRoot, "NO FIX", 78, ui_theme::fontBody(), ui_theme::muted());
    satsPowerLabel = create_label(pageRoot, "SAT --  BAT --", 108, ui_theme::fontSmall(), ui_theme::muted());
    speedValueLabel = create_label(pageRoot, "0.0", 146, ui_theme::fontTime(), ui_theme::text());
    create_label(pageRoot, "KM/H", 220, ui_theme::fontBody(), ui_theme::muted());

    distanceValueLabel = create_metric_column(pageRoot, "0.00", "KM", 93);
    elapsedValueLabel = create_metric_column(pageRoot, "00:00:00", "TIME", 237);

    recordingStatusLabel = create_status_capsule(pageRoot, "PAUSED");
    playIconLabel = create_play_button(pageRoot, false);

    pageActive = true;
    ui_workout_gps_update(state);
}

void ui_workout_gps_update(const UiState& state)
{
    if (!pageActive || pageRoot == nullptr) {
        return;
    }

    uiBatteryRingUpdate(
        &batteryRing,
        uiBatteryRingBatteryValid(
            state.pmuReady,
            state.batteryVoltage,
            state.batteryPercent
        ),
        state.batteryPercent
    );

    const bool usingImuSpeed = state.speedSource == SPEED_SOURCE_IMU_EST;
    lv_label_set_text(gpsStatusLabel, usingImuSpeed ? "IMU EST" : (state.gpsFixed ? "GPS FIX" : "NO FIX"));
    lv_obj_set_style_text_color(
        gpsStatusLabel,
        (state.gpsFixed || usingImuSpeed) ? ui_theme::green() : ui_theme::muted(),
        0
    );

    char statusText[24];
    format_sats_power(state, statusText, sizeof(statusText));
    lv_label_set_text(satsPowerLabel, statusText);

    char speedText[16];
    snprintf(speedText, sizeof(speedText), "%.1f", state.speedKmh);
    lv_label_set_text(speedValueLabel, speedText);

    char distanceText[16];
    snprintf(distanceText, sizeof(distanceText), "%.2f", state.distanceKm);
    lv_label_set_text(distanceValueLabel, distanceText);

    char elapsedText[16];
    format_elapsed(state.elapsedSeconds, elapsedText, sizeof(elapsedText));
    lv_label_set_text(elapsedValueLabel, elapsedText);

    lv_label_set_text(recordingStatusLabel, state.recording ? "RUNNING" : "PAUSED");
    lv_label_set_text(playIconLabel, state.recording ? "||" : ">");
}
