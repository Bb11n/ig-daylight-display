#include "ui_settings.h"

#include <stdio.h>
#include <string.h>

#include "ui_battery_ring.h"
#include "ui_theme.h"

namespace {

constexpr uint8_t kScreenBrightnessStep = 10;

lv_color_t pageBackground() { return lv_color_hex(0x000000); }
lv_color_t cardSurface() { return lv_color_hex(0x11151A); }
lv_color_t pressedSurface() { return lv_color_hex(0x1B222A); }
lv_color_t borderColor() { return lv_color_hex(0x26313A); }
lv_color_t primaryText() { return lv_color_hex(0xF5F7FA); }
lv_color_t secondaryText() { return lv_color_hex(0x89939D); }
lv_color_t mutedText() { return lv_color_hex(0x5F6973); }
lv_color_t iceBlue() { return lv_color_hex(0x4CC9F0); }
lv_color_t validGreen() { return lv_color_hex(0x30D158); }
lv_color_t inactiveStatus() { return lv_color_hex(0x68727D); }
lv_color_t barTrack() { return lv_color_hex(0x0D1115); }

UiSettingsScreenBrightnessControl screenControl = {};

lv_obj_t* settingsRoot = nullptr;
UiBatteryRing batteryRing = {};
lv_obj_t* backButton = nullptr;
lv_obj_t* screenCard = nullptr;
lv_obj_t* screenValueLabel = nullptr;
lv_obj_t* screenBar = nullptr;
lv_obj_t* screenMinusButton = nullptr;
lv_obj_t* screenMinusLabel = nullptr;
lv_obj_t* screenPlusButton = nullptr;
lv_obj_t* screenPlusLabel = nullptr;
lv_obj_t* connectionCard = nullptr;
lv_obj_t* bleStatusLabel = nullptr;
lv_obj_t* sensorsCard = nullptr;
lv_obj_t* gpsStatusDot = nullptr;
lv_obj_t* gpsStatusLabel = nullptr;
lv_obj_t* rtcStatusDot = nullptr;
lv_obj_t* rtcStatusLabel = nullptr;
lv_obj_t* pmuStatusDot = nullptr;
lv_obj_t* pmuStatusLabel = nullptr;
lv_obj_t* usbStatusDot = nullptr;
lv_obj_t* usbStatusLabel = nullptr;
lv_obj_t* aboutCard = nullptr;

bool pageActive = false;
bool renderCacheValid = false;
int lastRenderedScreenBrightness = -1;
bool lastRenderedBleReady = false;
bool lastRenderedBleConnected = false;
bool lastRenderedGpsFixed = false;
int lastRenderedSatellites = -1;
bool lastRenderedRtcValid = false;
bool lastRenderedPmuValid = false;
bool lastRenderedUsbPresent = false;

void onScreenMinusClicked(lv_event_t* event);
void onScreenPlusClicked(lv_event_t* event);

bool screenBrightnessControlAvailable()
{
    return screenControl.getPercent != nullptr && screenControl.setPercent != nullptr;
}

void clearObjectReferences()
{
    settingsRoot = nullptr;
    uiBatteryRingReset(&batteryRing);
    backButton = nullptr;
    screenCard = nullptr;
    screenValueLabel = nullptr;
    screenBar = nullptr;
    screenMinusButton = nullptr;
    screenMinusLabel = nullptr;
    screenPlusButton = nullptr;
    screenPlusLabel = nullptr;
    connectionCard = nullptr;
    bleStatusLabel = nullptr;
    sensorsCard = nullptr;
    gpsStatusDot = nullptr;
    gpsStatusLabel = nullptr;
    rtcStatusDot = nullptr;
    rtcStatusLabel = nullptr;
    pmuStatusDot = nullptr;
    pmuStatusLabel = nullptr;
    usbStatusDot = nullptr;
    usbStatusLabel = nullptr;
    aboutCard = nullptr;

    screenControl = {};
    pageActive = false;
    renderCacheValid = false;
    lastRenderedScreenBrightness = -1;
    lastRenderedBleReady = false;
    lastRenderedBleConnected = false;
    lastRenderedGpsFixed = false;
    lastRenderedSatellites = -1;
    lastRenderedRtcValid = false;
    lastRenderedPmuValid = false;
    lastRenderedUsbPresent = false;
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
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return label;
}

lv_obj_t* createCard(lv_obj_t* parent, int16_t y, int16_t height)
{
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 68, y);
    lv_obj_set_size(card, 330, height);
    lv_obj_set_style_radius(card, 26, 0);
    lv_obj_set_style_bg_color(card, cardSurface(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, borderColor(), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return card;
}

lv_obj_t* createRoundButton(
    lv_obj_t* parent,
    int16_t x,
    const char* text,
    lv_obj_t** outLabel,
    lv_event_cb_t callback)
{
    lv_obj_t* button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, x, 24);
    lv_obj_set_size(button, 48, 48);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, barTrack(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, borderColor(), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, pressedSurface(), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, iceBlue(), LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, ui_theme::fontLarge(), 0);
    lv_obj_set_style_text_color(label, iceBlue(), 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_center(label);
    *outLabel = label;

    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    return button;
}

void createScreenCard(lv_obj_t* parent)
{
    screenCard = createCard(parent, 106, 96);
    createLabel(screenCard, "SCREEN", 20, 15, 140, ui_theme::fontSmall(), primaryText(), LV_TEXT_ALIGN_LEFT);
    createLabel(screenCard, "PANEL", 20, 45, 140, ui_theme::fontStatus(), secondaryText(), LV_TEXT_ALIGN_LEFT);

    if (!screenBrightnessControlAvailable()) {
        screenValueLabel = createLabel(
            screenCard,
            "DEFAULT",
            164,
            23,
            146,
            ui_theme::fontSmall(),
            primaryText(),
            LV_TEXT_ALIGN_RIGHT
        );
        createLabel(
            screenCard,
            "CONTROL --",
            164,
            52,
            146,
            ui_theme::fontStatus(),
            inactiveStatus(),
            LV_TEXT_ALIGN_RIGHT
        );
        return;
    }

    screenBar = lv_bar_create(screenCard);
    lv_obj_remove_style_all(screenBar);
    lv_obj_set_pos(screenBar, 20, 72);
    lv_obj_set_size(screenBar, 136, 6);
    lv_bar_set_range(screenBar, 0, 100);
    lv_bar_set_value(screenBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(screenBar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(screenBar, barTrack(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screenBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(screenBar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(screenBar, iceBlue(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(screenBar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_clear_flag(screenBar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(screenBar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screenBar, LV_OBJ_FLAG_GESTURE_BUBBLE);

    screenMinusButton = createRoundButton(
        screenCard,
        172,
        "-",
        &screenMinusLabel,
        onScreenMinusClicked
    );
    screenValueLabel = createLabel(
        screenCard,
        "0%",
        220,
        38,
        42,
        ui_theme::fontStatus(),
        primaryText(),
        LV_TEXT_ALIGN_CENTER
    );
    screenPlusButton = createRoundButton(
        screenCard,
        262,
        "+",
        &screenPlusLabel,
        onScreenPlusClicked
    );
}

void createConnectionCard(lv_obj_t* parent)
{
    connectionCard = createCard(parent, 211, 64);
    createLabel(
        connectionCard,
        "CONNECTION",
        20,
        22,
        170,
        ui_theme::fontStatus(),
        primaryText(),
        LV_TEXT_ALIGN_LEFT
    );
    bleStatusLabel = createLabel(
        connectionCard,
        "BLE --",
        188,
        22,
        122,
        ui_theme::fontStatus(),
        inactiveStatus(),
        LV_TEXT_ALIGN_RIGHT
    );
}

void createSensorStatusItem(
    lv_obj_t* parent,
    int16_t slotX,
    lv_obj_t** outDot,
    lv_obj_t** outLabel)
{
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_pos(dot, slotX, 51);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, inactiveStatus(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t* label = createLabel(
        parent,
        "--",
        slotX + 14,
        43,
        62,
        ui_theme::fontStatus(),
        inactiveStatus(),
        LV_TEXT_ALIGN_LEFT
    );
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);

    *outDot = dot;
    *outLabel = label;
}

void createSensorsCard(lv_obj_t* parent)
{
    sensorsCard = createCard(parent, 284, 82);
    createLabel(
        sensorsCard,
        "SENSORS",
        20,
        13,
        160,
        ui_theme::fontStatus(),
        secondaryText(),
        LV_TEXT_ALIGN_LEFT
    );
    createSensorStatusItem(sensorsCard, 14, &gpsStatusDot, &gpsStatusLabel);
    createSensorStatusItem(sensorsCard, 90, &rtcStatusDot, &rtcStatusLabel);
    createSensorStatusItem(sensorsCard, 166, &pmuStatusDot, &pmuStatusLabel);
    createSensorStatusItem(sensorsCard, 242, &usbStatusDot, &usbStatusLabel);
}

void createAboutCard(lv_obj_t* parent)
{
    aboutCard = createCard(parent, 375, 58);
    createLabel(aboutCard, "ABOUT", 20, 20, 96, ui_theme::fontStatus(), secondaryText(), LV_TEXT_ALIGN_LEFT);
    createLabel(
        aboutCard,
        "ESP32-S3 AMOLED",
        118,
        10,
        192,
        ui_theme::fontStatus(),
        primaryText(),
        LV_TEXT_ALIGN_RIGHT
    );
    createLabel(
        aboutCard,
        "466x466 / PROTOCOL v2",
        118,
        32,
        192,
        ui_theme::fontStatus(),
        mutedText(),
        LV_TEXT_ALIGN_RIGHT
    );
}

void updateScreenButtonVisual(lv_obj_t* button, lv_obj_t* label, bool enabled)
{
    if (button == nullptr || label == nullptr) {
        return;
    }

    lv_obj_set_style_border_color(button, enabled ? borderColor() : mutedText(), 0);
    lv_obj_set_style_border_opa(button, enabled ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_text_color(label, enabled ? iceBlue() : mutedText(), 0);
}

void refreshScreenBrightness(bool force)
{
    if (!screenBrightnessControlAvailable() || screenValueLabel == nullptr || screenBar == nullptr) {
        return;
    }

    const uint8_t brightness = screenControl.getPercent();
    if (!force && renderCacheValid && brightness == lastRenderedScreenBrightness) {
        return;
    }

    char valueText[8];
    snprintf(valueText, sizeof(valueText), "%u%%", static_cast<unsigned>(brightness));
    lv_label_set_text(screenValueLabel, valueText);
    lv_bar_set_value(screenBar, brightness, LV_ANIM_OFF);
    updateScreenButtonVisual(screenMinusButton, screenMinusLabel, brightness > 0);
    updateScreenButtonVisual(screenPlusButton, screenPlusLabel, brightness < 100);
    lastRenderedScreenBrightness = brightness;
}

void updateStatusLabel(lv_obj_t* label, const char* text, lv_color_t color)
{
    if (label == nullptr) {
        return;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, 0);
}

void updateSensorStatus(
    lv_obj_t* dot,
    lv_obj_t* label,
    const char* text,
    lv_color_t color)
{
    if (dot == nullptr || label == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(dot, color, 0);
    updateStatusLabel(label, text, color);
}

void refreshSystemStatus(const UiState& state, bool force)
{
    if (force || !renderCacheValid ||
        state.bleEnabled != lastRenderedBleReady ||
        state.bleConnected != lastRenderedBleConnected) {
        if (state.bleConnected) {
            updateStatusLabel(bleStatusLabel, "BLE LINK", validGreen());
        } else if (state.bleEnabled) {
            updateStatusLabel(bleStatusLabel, "BLE ON", iceBlue());
        } else {
            updateStatusLabel(bleStatusLabel, "BLE --", inactiveStatus());
        }
    }

    if (force || !renderCacheValid ||
        state.gpsFixed != lastRenderedGpsFixed ||
        state.satellites != lastRenderedSatellites) {
        char gpsText[12];
        if (state.gpsFixed) {
            snprintf(gpsText, sizeof(gpsText), "GPS %02d", state.satellites);
            updateSensorStatus(gpsStatusDot, gpsStatusLabel, gpsText, validGreen());
        } else {
            updateSensorStatus(gpsStatusDot, gpsStatusLabel, "GPS --", inactiveStatus());
        }
    }

    const bool rtcValid = state.modeText != nullptr && strcmp(state.modeText, "LOCAL TIME") == 0;
    if (force || !renderCacheValid || rtcValid != lastRenderedRtcValid) {
        updateSensorStatus(
            rtcStatusDot,
            rtcStatusLabel,
            rtcValid ? "RTC OK" : "RTC --",
            rtcValid ? validGreen() : inactiveStatus()
        );
    }

    const bool pmuValid = state.pmuReady;
    if (force || !renderCacheValid || pmuValid != lastRenderedPmuValid) {
        updateSensorStatus(
            pmuStatusDot,
            pmuStatusLabel,
            pmuValid ? "PMU OK" : "PMU --",
            pmuValid ? validGreen() : inactiveStatus()
        );
    }

    const bool usbPresent = state.pmuReady && (state.vbusIn || state.vbusGood);
    if (force || !renderCacheValid || usbPresent != lastRenderedUsbPresent) {
        updateSensorStatus(
            usbStatusDot,
            usbStatusLabel,
            usbPresent ? "USB ON" : "USB --",
            usbPresent ? iceBlue() : inactiveStatus()
        );
    }

    lastRenderedBleReady = state.bleEnabled;
    lastRenderedBleConnected = state.bleConnected;
    lastRenderedGpsFixed = state.gpsFixed;
    lastRenderedSatellites = state.satellites;
    lastRenderedRtcValid = rtcValid;
    lastRenderedPmuValid = pmuValid;
    lastRenderedUsbPresent = usbPresent;
}

void refreshSettingsPage(const UiState& state, bool force)
{
    if (!pageActive || settingsRoot == nullptr) {
        return;
    }

    uiBatteryRingUpdate(
        &batteryRing,
        uiBatteryRingBatteryValid(
            state.pmuReady,
            state.batteryVoltage,
            state.batteryPercent),
        state.batteryPercent);
    refreshScreenBrightness(force);
    refreshSystemStatus(state, force);
    renderCacheValid = true;
}

void adjustScreenBrightness(int16_t delta)
{
    if (!screenBrightnessControlAvailable()) {
        return;
    }

    int16_t nextBrightness = static_cast<int16_t>(screenControl.getPercent()) + delta;
    if (nextBrightness < 0) {
        nextBrightness = 0;
    } else if (nextBrightness > 100) {
        nextBrightness = 100;
    }

    screenControl.setPercent(static_cast<uint8_t>(nextBrightness));
    refreshScreenBrightness(true);
}

void onScreenMinusClicked(lv_event_t* event)
{
    (void)event;
    adjustScreenBrightness(-static_cast<int16_t>(kScreenBrightnessStep));
}

void onScreenPlusClicked(lv_event_t* event)
{
    (void)event;
    adjustScreenBrightness(static_cast<int16_t>(kScreenBrightnessStep));
}

void onBackHitAreaStateChanged(lv_event_t* event)
{
    if (backButton == nullptr) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(backButton, pressedSurface(), 0);
        lv_obj_set_style_border_color(backButton, iceBlue(), 0);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_bg_color(backButton, cardSurface(), 0);
        lv_obj_set_style_border_color(backButton, borderColor(), 0);
    }
}

} // namespace

void ui_settings_create(
    lv_obj_t* parent,
    const UiState& state,
    const UiSettingsScreenBrightnessControl& brightnessControl)
{
    clearObjectReferences();
    screenControl = brightnessControl;

    settingsRoot = lv_obj_create(parent);
    lv_obj_remove_style_all(settingsRoot);
    lv_obj_set_pos(settingsRoot, 0, 0);
    lv_obj_set_size(settingsRoot, ui_theme::kScreenWidth, ui_theme::kScreenHeight);
    lv_obj_set_style_bg_color(settingsRoot, pageBackground(), 0);
    lv_obj_set_style_bg_opa(settingsRoot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(settingsRoot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(settingsRoot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(settingsRoot, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(settingsRoot, onPageDeleted, LV_EVENT_DELETE, nullptr);
    uiBatteryRingCreate(settingsRoot, &batteryRing);

    createLabel(
        settingsRoot,
        "SETTINGS",
        0,
        66,
        ui_theme::kScreenWidth,
        ui_theme::fontSmall(),
        secondaryText(),
        LV_TEXT_ALIGN_CENTER
    );
    createScreenCard(settingsRoot);
    createConnectionCard(settingsRoot);
    createSensorsCard(settingsRoot);
    createAboutCard(settingsRoot);

    pageActive = true;
    refreshSettingsPage(state, true);
}

void ui_settings_update(const UiState& state)
{
    refreshSettingsPage(state, false);
}

void ui_settings_attach_back_button_visual(lv_obj_t* hitArea)
{
    if (!pageActive || hitArea == nullptr) {
        return;
    }

    backButton = lv_obj_create(hitArea);
    lv_obj_remove_style_all(backButton);
    lv_obj_set_pos(backButton, 18, 15);
    lv_obj_set_size(backButton, 54, 54);
    lv_obj_set_style_radius(backButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(backButton, cardSurface(), 0);
    lv_obj_set_style_bg_opa(backButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(backButton, 1, 0);
    lv_obj_set_style_border_color(backButton, borderColor(), 0);
    lv_obj_set_style_border_opa(backButton, LV_OPA_COVER, 0);
    lv_obj_clear_flag(backButton, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(backButton, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t* icon = lv_label_create(backButton);
    lv_label_set_text(icon, "<");
    lv_obj_set_style_text_font(icon, ui_theme::fontBody(), 0);
    lv_obj_set_style_text_color(icon, primaryText(), 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_center(icon);

    lv_obj_add_event_cb(hitArea, onBackHitAreaStateChanged, LV_EVENT_ALL, nullptr);
}
