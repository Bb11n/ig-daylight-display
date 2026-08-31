#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace ui_theme {

constexpr int16_t kScreenWidth = 466;
constexpr int16_t kScreenHeight = 466;
constexpr int16_t kCenterX = 233;
constexpr int16_t kCenterY = 233;
constexpr int16_t kOuterRingSize = 458;
constexpr int16_t kOuterRingX = 4;
constexpr int16_t kOuterRingY = 4;

constexpr int16_t kStatusTopY = 78;
constexpr int16_t kTimeY = 138;
constexpr int16_t kModeY = 248;
constexpr int16_t kDateY = 276;
constexpr int16_t kStatusY = 316;
constexpr int16_t kNavY = 360;

constexpr int16_t kFontStatus = 14;
constexpr int16_t kFontSmall = 16;
constexpr int16_t kFontBody = 20;
constexpr int16_t kFontTime = 48;

inline lv_color_t bg() { return lv_color_hex(0x000000); }
inline lv_color_t surface() { return lv_color_hex(0x18181B); }
inline lv_color_t surfaceAlt() { return lv_color_hex(0x27272A); }
inline lv_color_t ringBase() { return lv_color_hex(0x202024); }
inline lv_color_t text() { return lv_color_hex(0xFFFFFF); }
inline lv_color_t muted() { return lv_color_hex(0x71717A); }
inline lv_color_t dim() { return lv_color_hex(0x3F3F46); }
inline lv_color_t lime() { return lv_color_hex(0xA3E635); }
inline lv_color_t green() { return lv_color_hex(0x4ADE80); }
inline lv_color_t cyan() { return lv_color_hex(0x38BDF8); }

inline const lv_font_t* fontStatus()
{
#if LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#elif LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#elif LV_FONT_MONTSERRAT_12
    return &lv_font_montserrat_12;
#else
    return LV_FONT_DEFAULT;
#endif
}

inline const lv_font_t* fontSmall()
{
#if LV_FONT_MONTSERRAT_18
    return &lv_font_montserrat_18;
#elif LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#elif LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#elif LV_FONT_SIMSUN_16_CJK
    return &lv_font_simsun_16_cjk;
#else
    return LV_FONT_DEFAULT;
#endif
}

inline const lv_font_t* fontBody()
{
#if LV_FONT_MONTSERRAT_24
    return &lv_font_montserrat_24;
#elif LV_FONT_MONTSERRAT_22
    return &lv_font_montserrat_22;
#elif LV_FONT_MONTSERRAT_20
    return &lv_font_montserrat_20;
#elif LV_FONT_MONTSERRAT_18
    return &lv_font_montserrat_18;
#elif LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#elif LV_FONT_SIMSUN_16_CJK
    return &lv_font_simsun_16_cjk;
#else
    return LV_FONT_DEFAULT;
#endif
}

inline const lv_font_t* fontLarge()
{
#if LV_FONT_MONTSERRAT_28
    return &lv_font_montserrat_28;
#elif LV_FONT_MONTSERRAT_26
    return &lv_font_montserrat_26;
#elif LV_FONT_MONTSERRAT_24
    return &lv_font_montserrat_24;
#elif LV_FONT_MONTSERRAT_22
    return &lv_font_montserrat_22;
#else
    return fontBody();
#endif
}

inline const lv_font_t* fontTime()
{
#if LV_FONT_MONTSERRAT_48
    return &lv_font_montserrat_48;
#elif LV_FONT_MONTSERRAT_44
    return &lv_font_montserrat_44;
#else
    return LV_FONT_DEFAULT;
#endif
}

} // namespace ui_theme
