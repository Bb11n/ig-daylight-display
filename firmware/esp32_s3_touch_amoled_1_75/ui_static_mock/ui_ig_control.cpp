#include "ui_ig_control.h"

#include <Arduino.h>
#include <stdio.h>

#include "debug_log_config.h"
#include "display_state.h"
#include "ui_battery_ring.h"
#include "ui_theme.h"

namespace {

constexpr uint32_t kStateRefreshIntervalMs = 150;
constexpr uint8_t kBrightnessStep = 10;

lv_color_t cardSurface() { return lv_color_hex(0x11151A); }
lv_color_t pressedSurface() { return lv_color_hex(0x1B222A); }
lv_color_t borderColor() { return lv_color_hex(0x26313A); }
lv_color_t primaryText() { return lv_color_hex(0xF5F7FA); }
lv_color_t secondaryText() { return lv_color_hex(0x89939D); }
lv_color_t mutedText() { return lv_color_hex(0x5F6973); }
lv_color_t projectorActive() { return lv_color_hex(0x30D158); }
lv_color_t projectorInactive() { return lv_color_hex(0x68727D); }
lv_color_t projectorCircleSurface() { return lv_color_hex(0x1A1E23); }
lv_color_t projectorCircleBorder() { return lv_color_hex(0x46505A); }
lv_color_t brightnessIceBlue() { return lv_color_hex(0x4CC9F0); }
lv_color_t brightnessTrack() { return lv_color_hex(0x1B303A); }
lv_color_t brightnessControlBorder() { return lv_color_hex(0x315766); }

lv_obj_t* pageRoot = nullptr;
UiBatteryRing batteryRing = {};
lv_obj_t* projectorCard = nullptr;
lv_obj_t* projectorStatusCircle = nullptr;
lv_obj_t* projectorStateLabel = nullptr;
lv_obj_t* brightnessValueLabel = nullptr;
lv_obj_t* brightnessBar = nullptr;
lv_obj_t* brightnessMinusButton = nullptr;
lv_obj_t* brightnessMinusLabel = nullptr;
lv_obj_t* brightnessPlusButton = nullptr;
lv_obj_t* brightnessPlusLabel = nullptr;

bool pageActive = false;
bool renderCacheValid = false;
bool lastRenderedProjectionOn = false;
uint8_t lastRenderedBrightness = 0;
uint32_t lastStateRefreshMs = 0;

void onProjectorClicked(lv_event_t* event);

void clearObjectReferences()
{
    pageRoot = nullptr;
    uiBatteryRingReset(&batteryRing);
    projectorCard = nullptr;
    projectorStatusCircle = nullptr;
    projectorStateLabel = nullptr;
    brightnessValueLabel = nullptr;
    brightnessBar = nullptr;
    brightnessMinusButton = nullptr;
    brightnessMinusLabel = nullptr;
    brightnessPlusButton = nullptr;
    brightnessPlusLabel = nullptr;
    pageActive = false;
    renderCacheValid = false;
    lastStateRefreshMs = 0;
}

void onPageDeleted(lv_event_t* event)
{
    (void)event;
    clearObjectReferences();
}

lv_obj_t* createLabel(
    lv_obj_t* parent,
    const char* text,
    int16_t x,
    int16_t y,
    int16_t width,
    const lv_font_t* font,
    lv_color_t color,
    lv_text_align_t align)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return label;
}

void createBackButtonVisual(lv_obj_t* parent)
{
    lv_obj_t* button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 54, 54);
    lv_obj_set_pos(button, 58, 55);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, cardSurface(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, borderColor(), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t* icon = lv_label_create(button);
    lv_label_set_text(icon, "<");
    lv_obj_set_style_text_font(icon, ui_theme::fontBody(), 0);
    lv_obj_set_style_text_color(icon, primaryText(), 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(icon);
}

void createProjectorCard(lv_obj_t* parent)
{
    projectorCard = lv_obj_create(parent);
    lv_obj_remove_style_all(projectorCard);
    lv_obj_set_pos(projectorCard, 68, 138);
    lv_obj_set_size(projectorCard, 330, 128);
    lv_obj_set_style_radius(projectorCard, 32, 0);
    lv_obj_set_style_bg_color(projectorCard, cardSurface(), 0);
    lv_obj_set_style_bg_opa(projectorCard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(projectorCard, 1, 0);
    lv_obj_set_style_border_color(projectorCard, borderColor(), 0);
    lv_obj_set_style_border_opa(projectorCard, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(projectorCard, pressedSurface(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(projectorCard, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_flag(projectorCard, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(projectorCard, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(projectorCard, LV_OBJ_FLAG_SCROLLABLE);

    projectorStatusCircle = lv_obj_create(projectorCard);
    lv_obj_remove_style_all(projectorStatusCircle);
    lv_obj_set_pos(projectorStatusCircle, 20, 21);
    lv_obj_set_size(projectorStatusCircle, 86, 86);
    lv_obj_set_style_radius(projectorStatusCircle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(projectorStatusCircle, projectorCircleSurface(), 0);
    lv_obj_set_style_bg_opa(projectorStatusCircle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(projectorStatusCircle, 2, 0);
    lv_obj_set_style_border_color(projectorStatusCircle, projectorCircleBorder(), 0);
    lv_obj_set_style_border_opa(projectorStatusCircle, LV_OPA_COVER, 0);
    lv_obj_clear_flag(projectorStatusCircle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(projectorStatusCircle, LV_OBJ_FLAG_GESTURE_BUBBLE);

    createLabel(
        projectorCard,
        "PROJECTOR",
        130,
        27,
        174,
        ui_theme::fontStatus(),
        secondaryText(),
        LV_TEXT_ALIGN_LEFT
    );
    projectorStateLabel = createLabel(
        projectorCard,
        "OFF",
        130,
        52,
        174,
        ui_theme::fontTime(),
        primaryText(),
        LV_TEXT_ALIGN_LEFT
    );

    lv_obj_add_event_cb(projectorCard, onProjectorClicked, LV_EVENT_CLICKED, nullptr);
}

lv_obj_t* createRoundButton(
    lv_obj_t* parent,
    int16_t x,
    int16_t y,
    int16_t size,
    const char* text,
    const lv_font_t* font,
    lv_obj_t** outLabel)
{
    lv_obj_t* button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, size, size);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, cardSurface(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, brightnessControlBorder(), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, pressedSurface(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, ui_theme::text(), 0);
    lv_obj_center(label);
    *outLabel = label;
    return button;
}

void createBrightnessBar(lv_obj_t* parent)
{
    brightnessBar = lv_bar_create(parent);
    lv_obj_remove_style_all(brightnessBar);
    lv_obj_set_pos(brightnessBar, 119, 356);
    lv_obj_set_size(brightnessBar, 228, 9);
    lv_bar_set_range(brightnessBar, 0, 100);
    lv_bar_set_value(brightnessBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightnessBar, brightnessTrack(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(brightnessBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(brightnessBar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(brightnessBar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightnessBar, brightnessIceBlue(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(brightnessBar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(brightnessBar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_clear_flag(brightnessBar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(brightnessBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(brightnessBar, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

void updateProjectorVisual(bool projectionOn)
{
    if (projectorCard == nullptr || projectorStatusCircle == nullptr || projectorStateLabel == nullptr) {
        return;
    }

    lv_obj_set_style_border_color(
        projectorStatusCircle,
        projectionOn ? projectorActive() : projectorCircleBorder(),
        0
    );
    lv_obj_set_style_border_width(projectorStatusCircle, projectionOn ? 3 : 2, 0);
    lv_label_set_text(projectorStateLabel, projectionOn ? "ON" : "OFF");
    lv_obj_set_style_text_color(projectorStateLabel, projectionOn ? projectorActive() : projectorInactive(), 0);
}

void updateBrightnessButtonVisual(lv_obj_t* button, lv_obj_t* label, bool canAdjust)
{
    if (button == nullptr || label == nullptr) {
        return;
    }

    lv_obj_set_style_border_color(button, canAdjust ? brightnessControlBorder() : mutedText(), 0);
    lv_obj_set_style_border_opa(button, canAdjust ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_text_color(label, canAdjust ? brightnessIceBlue() : mutedText(), 0);
}

void updateBrightnessVisual(uint8_t brightness)
{
    if (brightnessValueLabel == nullptr || brightnessBar == nullptr) {
        return;
    }

    char valueText[8];
    snprintf(valueText, sizeof(valueText), "%u%%", static_cast<unsigned>(brightness));
    lv_label_set_text(brightnessValueLabel, valueText);
    lv_bar_set_value(brightnessBar, brightness, LV_ANIM_OFF);
    updateBrightnessButtonVisual(brightnessMinusButton, brightnessMinusLabel, brightness > 0);
    updateBrightnessButtonVisual(brightnessPlusButton, brightnessPlusLabel, brightness < 100);
}

void refreshIgControlPageFromState(bool force)
{
    if (!pageActive) {
        return;
    }

    const DisplayState snapshot = displayStateGetSnapshot();
    if (force || !renderCacheValid || snapshot.projectionOn != lastRenderedProjectionOn) {
        updateProjectorVisual(snapshot.projectionOn);
    }
    if (force || !renderCacheValid || snapshot.brightnessPercent != lastRenderedBrightness) {
        updateBrightnessVisual(snapshot.brightnessPercent);
    }
    lastRenderedProjectionOn = snapshot.projectionOn;
    lastRenderedBrightness = snapshot.brightnessPercent;
    renderCacheValid = true;
}

void onProjectorClicked(lv_event_t* event)
{
    (void)event;
    const DisplayState snapshot = displayStateGetSnapshot();
    displayStateSetProjection(!snapshot.projectionOn);

    const DisplayState updated = displayStateGetSnapshot();
#if ENABLE_DISPLAY_STATE_LOG
    Serial.printf("EVENT: ig_projector value=%d\n", updated.projectionOn ? 1 : 0);
#endif
    refreshIgControlPageFromState(true);
}

void adjustBrightness(int16_t delta)
{
    const DisplayState snapshot = displayStateGetSnapshot();
    int16_t nextBrightness = static_cast<int16_t>(snapshot.brightnessPercent) + delta;
    if (nextBrightness < 0) {
        nextBrightness = 0;
    } else if (nextBrightness > 100) {
        nextBrightness = 100;
    }

    if (nextBrightness != snapshot.brightnessPercent) {
        displayStateSetBrightnessPercent(static_cast<uint8_t>(nextBrightness));
        const DisplayState updated = displayStateGetSnapshot();
#if ENABLE_DISPLAY_STATE_LOG
        Serial.printf("EVENT: ig_brightness value=%u\n", static_cast<unsigned>(updated.brightnessPercent));
#endif
    }
    refreshIgControlPageFromState(true);
}

void onBrightnessMinusClicked(lv_event_t* event)
{
    (void)event;
    adjustBrightness(-static_cast<int16_t>(kBrightnessStep));
}

void onBrightnessPlusClicked(lv_event_t* event)
{
    (void)event;
    adjustBrightness(static_cast<int16_t>(kBrightnessStep));
}

} // namespace

void ui_ig_control_create(lv_obj_t* parent, const UiState& state)
{
    clearObjectReferences();

    pageRoot = lv_obj_create(parent);
    lv_obj_remove_style_all(pageRoot);
    lv_obj_set_pos(pageRoot, 0, 0);
    lv_obj_set_size(pageRoot, ui_theme::kScreenWidth, ui_theme::kScreenHeight);
    lv_obj_set_style_bg_color(pageRoot, ui_theme::bg(), 0);
    lv_obj_set_style_bg_opa(pageRoot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(pageRoot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(pageRoot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pageRoot, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(pageRoot, onPageDeleted, LV_EVENT_DELETE, nullptr);
    uiBatteryRingCreate(pageRoot, &batteryRing);
    ui_ig_control_update_battery(state);

    createBackButtonVisual(pageRoot);
    createLabel(
        pageRoot,
        "IG CONTROL",
        0,
        66,
        ui_theme::kScreenWidth,
        ui_theme::fontSmall(),
        secondaryText(),
        LV_TEXT_ALIGN_CENTER
    );
    createProjectorCard(pageRoot);

    createLabel(
        pageRoot,
        "BRIGHTNESS",
        0,
        280,
        ui_theme::kScreenWidth,
        ui_theme::fontStatus(),
        secondaryText(),
        LV_TEXT_ALIGN_CENTER
    );
    brightnessValueLabel = createLabel(
        pageRoot,
        "0%",
        0,
        302,
        ui_theme::kScreenWidth,
        ui_theme::fontTime(),
        brightnessIceBlue(),
        LV_TEXT_ALIGN_CENTER
    );
    createBrightnessBar(pageRoot);

    brightnessMinusButton = createRoundButton(
        pageRoot,
        94,
        377,
        56,
        "-",
        ui_theme::fontLarge(),
        &brightnessMinusLabel
    );
    lv_obj_add_event_cb(brightnessMinusButton, onBrightnessMinusClicked, LV_EVENT_CLICKED, nullptr);

    brightnessPlusButton = createRoundButton(
        pageRoot,
        316,
        377,
        56,
        "+",
        ui_theme::fontLarge(),
        &brightnessPlusLabel
    );
    lv_obj_add_event_cb(brightnessPlusButton, onBrightnessPlusClicked, LV_EVENT_CLICKED, nullptr);

    pageActive = true;
    refreshIgControlPageFromState(true);
}

void ui_ig_control_update_battery(const UiState& state)
{
    uiBatteryRingUpdate(
        &batteryRing,
        uiBatteryRingBatteryValid(
            state.pmuReady,
            state.batteryVoltage,
            state.batteryPercent),
        state.batteryPercent);
}

void ui_ig_control_loop(uint32_t now)
{
    if (!pageActive) {
        return;
    }
    if (lastStateRefreshMs != 0 && (now - lastStateRefreshMs) < kStateRefreshIntervalMs) {
        return;
    }

    lastStateRefreshMs = now;
    refreshIgControlPageFromState(false);
}
