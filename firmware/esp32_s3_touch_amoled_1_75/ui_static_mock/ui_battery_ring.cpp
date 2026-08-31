#include "ui_battery_ring.h"

namespace {

void uiBatteryRingDeleteEvent(lv_event_t* event)
{
    UiBatteryRing* ring = static_cast<UiBatteryRing*>(lv_event_get_user_data(event));
    uiBatteryRingReset(ring);
}

void uiBatteryRingSetIndicatorVisible(UiBatteryRing* ring, bool visible)
{
    lv_obj_set_style_arc_opa(
        ring->arc,
        visible ? LV_OPA_COVER : LV_OPA_TRANSP,
        LV_PART_INDICATOR);
}

} // namespace

void uiBatteryRingCreate(lv_obj_t* parent, UiBatteryRing* ring)
{
    if (parent == nullptr || ring == nullptr) {
        return;
    }
    if (ring->created) {
        return;
    }

    ring->arc = lv_arc_create(parent);
    lv_obj_set_pos(ring->arc, 14, 14);
    lv_obj_set_size(ring->arc, 438, 438);
    lv_arc_set_rotation(ring->arc, 270);
    lv_arc_set_bg_angles(ring->arc, 0, 360);
    lv_arc_set_range(ring->arc, 0, 100);
    lv_arc_set_value(ring->arc, 0);
    lv_arc_set_angles(ring->arc, 0, 0);

    lv_obj_set_style_arc_width(ring->arc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring->arc, lv_color_hex(0x1B222A), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring->arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring->arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ring->arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring->arc, lv_color_hex(0x4CC9F0), LV_PART_INDICATOR);
    uiBatteryRingSetIndicatorVisible(ring, false);

    lv_obj_remove_style(ring->arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(
        ring->arc,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(ring->arc, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(ring->arc, uiBatteryRingDeleteEvent, LV_EVENT_DELETE, ring);

    ring->lastPercent = -1;
    ring->lastValid = false;
    ring->lastLowBattery = false;
    ring->created = true;
}

void uiBatteryRingUpdate(UiBatteryRing* ring, bool batteryValid, int batteryPercent)
{
    if (ring == nullptr || !ring->created || ring->arc == nullptr) {
        return;
    }

    const int percent = batteryValid ? uiBatteryRingClampPercent(batteryPercent) : 0;
    const bool lowBattery = batteryValid && uiBatteryRingIsLowBattery(percent);
    const bool validityChanged = ring->lastValid != batteryValid;
    const bool percentChanged = ring->lastPercent != percent;
    const bool lowBatteryChanged = ring->lastLowBattery != lowBattery;

    if (validityChanged || percentChanged) {
        lv_arc_set_value(ring->arc, percent);
        lv_arc_set_angles(ring->arc, 0, uiBatteryRingSweepDegrees(percent));
        uiBatteryRingSetIndicatorVisible(
            ring,
            uiBatteryRingIndicatorVisible(batteryValid, percent));
    }
    if (lowBatteryChanged) {
        lv_obj_set_style_arc_color(
            ring->arc,
            lv_color_hex(lowBattery ? 0xFF453A : 0x4CC9F0),
            LV_PART_INDICATOR);
    }

    ring->lastPercent = percent;
    ring->lastValid = batteryValid;
    ring->lastLowBattery = lowBattery;
}

void uiBatteryRingReset(UiBatteryRing* ring)
{
    if (ring == nullptr) {
        return;
    }

    ring->arc = nullptr;
    ring->lastPercent = -1;
    ring->lastValid = false;
    ring->lastLowBattery = false;
    ring->created = false;
}
