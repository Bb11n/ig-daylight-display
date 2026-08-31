#include "ui_home_status.h"

#include <stdio.h>
#include <string.h>

#include "ui_battery_ring.h"
#include "ui_theme.h"

namespace {

lv_color_t pageBackground() { return lv_color_hex(0x000000); }
lv_color_t borderColor() { return lv_color_hex(0x26313A); }
lv_color_t primaryText() { return lv_color_hex(0xF5F7FA); }
lv_color_t secondaryText() { return lv_color_hex(0x89939D); }
lv_color_t inactiveStatus() { return lv_color_hex(0x68727D); }
lv_color_t iceBlue() { return lv_color_hex(0x4CC9F0); }
lv_color_t validGreen() { return lv_color_hex(0x30D158); }
lv_color_t statusStripSurface() { return lv_color_hex(0x0D1115); }

lv_obj_t* pageRoot = nullptr;
UiBatteryRing batteryRing = {};
lv_obj_t* dateLabel = nullptr;
lv_obj_t* timeLabel = nullptr;
lv_obj_t* timeSourceLabel = nullptr;
lv_obj_t* batteryDot = nullptr;
lv_obj_t* batteryLabel = nullptr;
lv_obj_t* bleDot = nullptr;
lv_obj_t* bleLabel = nullptr;
lv_obj_t* gpsDot = nullptr;
lv_obj_t* gpsLabel = nullptr;
lv_obj_t* usbDot = nullptr;
lv_obj_t* usbLabel = nullptr;

bool pageActive = false;
bool renderCacheValid = false;
int lastRenderedHour = -1;
int lastRenderedMinute = -1;
char lastRenderedDate[20] = {};
bool lastRenderedRtcValid = false;
bool lastRenderedBatteryKnown = false;
int lastRenderedBattery = -1;
bool lastRenderedBleReady = false;
bool lastRenderedBleConnected = false;
bool lastRenderedGpsFixed = false;
int lastRenderedSatellites = -1;
bool lastRenderedUsbPresent = false;

void clearObjectReferences()
{
    pageRoot = nullptr;
    uiBatteryRingReset(&batteryRing);
    dateLabel = nullptr;
    timeLabel = nullptr;
    timeSourceLabel = nullptr;
    batteryDot = nullptr;
    batteryLabel = nullptr;
    bleDot = nullptr;
    bleLabel = nullptr;
    gpsDot = nullptr;
    gpsLabel = nullptr;
    usbDot = nullptr;
    usbLabel = nullptr;

    pageActive = false;
    renderCacheValid = false;
    lastRenderedHour = -1;
    lastRenderedMinute = -1;
    lastRenderedDate[0] = '\0';
    lastRenderedRtcValid = false;
    lastRenderedBatteryKnown = false;
    lastRenderedBattery = -1;
    lastRenderedBleReady = false;
    lastRenderedBleConnected = false;
    lastRenderedGpsFixed = false;
    lastRenderedSatellites = -1;
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
    lv_obj_set_width(label, width);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return label;
}

lv_obj_t* createStatusDot(lv_obj_t* parent, int16_t x)
{
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_pos(dot, x, 21);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, inactiveStatus(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return dot;
}

void createStatusItem(
    lv_obj_t* parent,
    int16_t slotX,
    lv_obj_t** outDot,
    lv_obj_t** outLabel)
{
    *outDot = createStatusDot(parent, slotX + 7);
    *outLabel = createLabel(
        parent,
        "--",
        slotX + 20,
        15,
        62,
        ui_theme::fontStatus(),
        inactiveStatus(),
        LV_TEXT_ALIGN_LEFT
    );
    lv_label_set_long_mode(*outLabel, LV_LABEL_LONG_CLIP);
}

void createSystemStatusStrip(lv_obj_t* parent)
{
    lv_obj_t* strip = lv_obj_create(parent);
    lv_obj_remove_style_all(strip);
    lv_obj_set_pos(strip, 68, 181);
    lv_obj_set_size(strip, 330, 50);
    lv_obj_set_style_radius(strip, 25, 0);
    lv_obj_set_style_bg_color(strip, statusStripSurface(), 0);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(strip, 1, 0);
    lv_obj_set_style_border_color(strip, borderColor(), 0);
    lv_obj_set_style_border_opa(strip, LV_OPA_COVER, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(strip, LV_OBJ_FLAG_GESTURE_BUBBLE);

    createStatusItem(strip, 0, &batteryDot, &batteryLabel);
    createStatusItem(strip, 82, &bleDot, &bleLabel);
    createStatusItem(strip, 164, &gpsDot, &gpsLabel);
    createStatusItem(strip, 246, &usbDot, &usbLabel);
}

void updateStatusItem(
    lv_obj_t* dot,
    lv_obj_t* label,
    const char* text,
    bool active,
    lv_color_t activeDotColor)
{
    if (dot == nullptr || label == nullptr) {
        return;
    }

    lv_label_set_text(label, text);
    lv_obj_set_style_bg_color(dot, active ? activeDotColor : inactiveStatus(), 0);
    lv_obj_set_style_text_color(label, active ? primaryText() : inactiveStatus(), 0);
}

void refreshTimeSection(const UiState& state, bool force)
{
    const bool rtcValid = state.modeText != nullptr && strcmp(state.modeText, "LOCAL TIME") == 0;
    const char* currentDate = state.dateText != nullptr ? state.dateText : "--- --";

    if (force || !renderCacheValid || state.hour != lastRenderedHour || state.minute != lastRenderedMinute) {
        char timeText[8];
        if (state.hour >= 0 && state.hour <= 23 && state.minute >= 0 && state.minute <= 59) {
            snprintf(timeText, sizeof(timeText), "%02d:%02d", state.hour, state.minute);
        } else {
            snprintf(timeText, sizeof(timeText), "--:--");
        }
        lv_label_set_text(timeLabel, timeText);
    }

    if (force || !renderCacheValid || strncmp(currentDate, lastRenderedDate, sizeof(lastRenderedDate)) != 0) {
        lv_label_set_text(dateLabel, currentDate);
        snprintf(lastRenderedDate, sizeof(lastRenderedDate), "%s", currentDate);
    }

    if (force || !renderCacheValid || rtcValid != lastRenderedRtcValid) {
        lv_label_set_text(timeSourceLabel, rtcValid ? "LOCAL TIME" : "RTC --");
        lv_obj_set_style_text_color(timeSourceLabel, rtcValid ? secondaryText() : inactiveStatus(), 0);
    }

    lastRenderedHour = state.hour;
    lastRenderedMinute = state.minute;
    lastRenderedRtcValid = rtcValid;
}

void refreshSystemStatus(const UiState& state, bool force)
{
    const bool batteryKnown = uiBatteryRingBatteryValid(
        state.pmuReady,
        state.batteryVoltage,
        state.batteryPercent);
    uiBatteryRingUpdate(&batteryRing, batteryKnown, state.batteryPercent);
    if (force || !renderCacheValid ||
        batteryKnown != lastRenderedBatteryKnown ||
        state.batteryPercent != lastRenderedBattery) {
        char batteryText[12];
        if (batteryKnown) {
            snprintf(batteryText, sizeof(batteryText), "BAT %d", state.batteryPercent);
        } else {
            snprintf(batteryText, sizeof(batteryText), "BAT --");
        }
        updateStatusItem(batteryDot, batteryLabel, batteryText, batteryKnown, iceBlue());
    }

    if (force || !renderCacheValid ||
        state.bleEnabled != lastRenderedBleReady ||
        state.bleConnected != lastRenderedBleConnected) {
        const bool bleActive = state.bleEnabled || state.bleConnected;
        updateStatusItem(
            bleDot,
            bleLabel,
            state.bleConnected ? "BLE LINK" : (state.bleEnabled ? "BLE ON" : "BLE --"),
            bleActive,
            validGreen()
        );
    }

    if (force || !renderCacheValid ||
        state.gpsFixed != lastRenderedGpsFixed ||
        state.satellites != lastRenderedSatellites) {
        char gpsText[12];
        if (state.gpsFixed) {
            snprintf(gpsText, sizeof(gpsText), "GPS %d", state.satellites);
        } else {
            snprintf(gpsText, sizeof(gpsText), "GPS --");
        }
        updateStatusItem(gpsDot, gpsLabel, gpsText, state.gpsFixed, validGreen());
    }

    const bool usbPresent = state.pmuReady && (state.vbusIn || state.vbusGood);
    if (force || !renderCacheValid || usbPresent != lastRenderedUsbPresent) {
        updateStatusItem(
            usbDot,
            usbLabel,
            usbPresent ? "USB ON" : "USB --",
            usbPresent,
            iceBlue()
        );
    }

    lastRenderedBatteryKnown = batteryKnown;
    lastRenderedBattery = state.batteryPercent;
    lastRenderedBleReady = state.bleEnabled;
    lastRenderedBleConnected = state.bleConnected;
    lastRenderedGpsFixed = state.gpsFixed;
    lastRenderedSatellites = state.satellites;
    lastRenderedUsbPresent = usbPresent;
}

void refreshHomeStatus(const UiState& state, bool force)
{
    if (!pageActive ||
        dateLabel == nullptr ||
        timeLabel == nullptr ||
        timeSourceLabel == nullptr) {
        return;
    }

    refreshTimeSection(state, force);
    refreshSystemStatus(state, force);
    renderCacheValid = true;
}

} // namespace

void ui_home_status_create(lv_obj_t* parent, const UiState& state)
{
    clearObjectReferences();

    pageRoot = lv_obj_create(parent);
    lv_obj_remove_style_all(pageRoot);
    lv_obj_set_pos(pageRoot, 0, 0);
    lv_obj_set_size(pageRoot, ui_theme::kScreenWidth, ui_theme::kScreenHeight);
    lv_obj_set_style_bg_color(pageRoot, pageBackground(), 0);
    lv_obj_set_style_bg_opa(pageRoot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(pageRoot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(pageRoot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pageRoot, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(pageRoot, onPageDeleted, LV_EVENT_DELETE, nullptr);
    uiBatteryRingCreate(pageRoot, &batteryRing);

    dateLabel = createLabel(
        pageRoot,
        "--- --",
        0,
        49,
        ui_theme::kScreenWidth,
        ui_theme::fontSmall(),
        secondaryText(),
        LV_TEXT_ALIGN_CENTER
    );
    timeLabel = createLabel(
        pageRoot,
        "--:--",
        0,
        78,
        ui_theme::kScreenWidth,
        ui_theme::fontTime(),
        primaryText(),
        LV_TEXT_ALIGN_CENTER
    );
    timeSourceLabel = createLabel(
        pageRoot,
        "RTC --",
        0,
        143,
        ui_theme::kScreenWidth,
        ui_theme::fontStatus(),
        inactiveStatus(),
        LV_TEXT_ALIGN_CENTER
    );

    createSystemStatusStrip(pageRoot);

    pageActive = true;
    refreshHomeStatus(state, true);
}

void ui_home_status_update(const UiState& state)
{
    refreshHomeStatus(state, false);
}
