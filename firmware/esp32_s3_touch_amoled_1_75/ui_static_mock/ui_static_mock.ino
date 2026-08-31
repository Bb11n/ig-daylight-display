#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <Wire.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include "esp32-hal-i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <lvgl.h>

#include "debug_log_config.h"
#include "system_resource_monitor.h"
#include "system_power_state.h"

#ifndef ENABLE_DISPLAY_BLE_SERVER
#define ENABLE_DISPLAY_BLE_SERVER 1
#endif

#ifndef ENABLE_DISPLAY_STATE_SERIAL_CONTROL
#define ENABLE_DISPLAY_STATE_SERIAL_CONTROL 1
#endif

#include "SensorPCF85063.hpp"
#include "TouchDrvCSTXXX.hpp"
#include "XPowersLib.h"
#if ENABLE_DISPLAY_BLE_SERVER
#include <NimBLEDevice.h>
#include "display_ble_server.h"
#endif
#include "display_product_page.h"
#include "display_state.h"
#if ENABLE_DISPLAY_STATE_SERIAL_CONTROL
#include "display_state_serial_control.h"
#endif
#include "gps_parser.h"
#include "gps_i2c_transport.h"
#include "pin_config.h"
#include "ui_home_mode.h"
#include "ui_home_status.h"
#include "ui_ig_control.h"
#include "ui_battery_ring.h"
#include "ui_settings.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_workout_gps.h"
#include "workout_calories.h"
#include "workout_imu_speed.h"
#include "workout_speed_resolver.h"

#define UI_SHOW_PAGE_HOME 1
#define UI_SHOW_PAGE_WORKOUT 2
#define UI_SHOW_PAGE_IG 3
#define UI_SHOW_PAGE_HOME_MODE 4
#define UI_SHOW_PAGE_SETTINGS 5
#define UI_SHOW_PAGE_AUTO 99
#define UI_SHOW_PAGE_TOUCH 100

#ifndef UI_STATIC_MOCK_PAGE
#define UI_STATIC_MOCK_PAGE UI_SHOW_PAGE_TOUCH
#endif

#ifndef GPS_DEBUG_RAW_NMEA
#define GPS_DEBUG_RAW_NMEA 0
#endif

#ifndef DIAG_ENABLE_GPS
#define DIAG_ENABLE_GPS 1
#endif

#ifndef ENABLE_GPS_RUNTIME
#define ENABLE_GPS_RUNTIME 1
#endif

#if !ENABLE_GPS_RUNTIME
#undef DIAG_ENABLE_GPS
#define DIAG_ENABLE_GPS 0
#endif

#ifndef DIAG_INIT_TOUCH
#define DIAG_INIT_TOUCH 1
#endif

#ifndef DIAG_POLL_TOUCH
#define DIAG_POLL_TOUCH 1
#endif

#ifndef DIAG_INIT_PMU
#define DIAG_INIT_PMU 1
#endif

#ifndef DIAG_POLL_PMU
#define DIAG_POLL_PMU 1
#endif

#ifndef DIAG_INIT_RTC
#define DIAG_INIT_RTC 1
#endif

#ifndef DIAG_POLL_RTC
#define DIAG_POLL_RTC 1
#endif

#ifndef DIAG_I2C_INIT_ORDER_GPS_FIRST
#define DIAG_I2C_INIT_ORDER_GPS_FIRST 0
#endif

// I2C conflict matrix:
// A GPS only: GPS=1, all touch/PMU/RTC INIT/POLL=0. Expect BYTES_1S>0.
// B GPS + touch init: TOUCH_INIT=1, TOUCH_POLL=0. Failing here points to touch init.
// C GPS + touch init/poll: TOUCH_INIT=1, TOUCH_POLL=1. B pass + C fail points to touch polling.
// D GPS + PMU init: PMU_INIT=1, PMU_POLL=0. Failing here points to PMU init.
// E GPS + PMU init/poll: PMU_INIT=1, PMU_POLL=1. D pass + E fail points to PMU polling.
// F GPS + RTC init: RTC_INIT=1, RTC_POLL=0. Failing here points to RTC init.
// G GPS + RTC init/poll: RTC_INIT=1, RTC_POLL=1. F pass + G fail points to RTC polling.
// H GPS + all init, no polling. Single modules pass + H fail points to combined init conflict.
// I GPS + all init/poll. H pass + I fail points to bus scheduling or polling conflict.
// If INIT passes but POLL fails, prefer an I2C mutex or one unified I2C backend next.
// DIAG_I2C_INIT_ORDER_GPS_FIRST=1 tests GPS before touch on the shared Wire NG bus.
// DIAG_I2C_INIT_ORDER_GPS_FIRST=0 tests the previous touch-first order.

namespace {

constexpr uint32_t kLvglTickPeriodMs = 2;
constexpr uint32_t kAutoPageIntervalMs = 4000;
constexpr uint32_t kGpsPollIntervalMs = 500;
constexpr uint32_t kGpsI2cDelayMs = 100;
constexpr uint32_t kGpsMaxNmeaBlockLength = 4096;
constexpr uint32_t kGpsLargeNmeaBlockWarningLength = 1024;
constexpr uint32_t kGpsMaxDynamicReadLength = 16384;
constexpr uint32_t kGpsDiagnosticsIntervalMs = 1000;
constexpr uint32_t kGpsTransportLogIntervalMs = 10000;
constexpr uint32_t kTouchPollIntervalMs = 200;
constexpr uint32_t kTouchTimeoutBackoffMs = 1000;
constexpr uint32_t kTouchI2cTimeoutMs = 50;
constexpr uint8_t kTouchTimeoutBackoffThreshold = 3;
constexpr uint32_t kPmuPollIntervalMs = 2000;
constexpr uint32_t kPowerKeyPollIntervalMs = 100;
constexpr uint32_t kPmuI2cTimeoutMs = 100;
constexpr uint32_t kPmuTimeoutBackoffMs = 3000;
constexpr uint8_t kPmuTimeoutBackoffThreshold = 3;
constexpr uint32_t kRtcPollIntervalMs = 1000;
constexpr uint32_t kRtcI2cTimeoutMs = 100;
constexpr uint32_t kRtcTimeoutBackoffMs = 3000;
constexpr uint8_t kRtcTimeoutBackoffThreshold = 3;
constexpr uint8_t kAutoPageCount = 5;
constexpr double kEarthRadiusM = 6371000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr float kMinGpsDistanceM = 0.5f;
constexpr float kMaxGpsDistanceM = 50.0f;
constexpr float kMinGpsSpeedKmhForDistance = 1.0f;
constexpr uint8_t kScreenBrightnessDefaultDriverValue = 200;

constexpr int16_t kHomeNavLeftX = 72;
constexpr int16_t kHomeNavRightX = 244;
constexpr int16_t kHomeNavFirstRowY = 250;
constexpr int16_t kHomeNavSecondRowY = 337;
constexpr int16_t kHomeNavCardWidth = 150;
constexpr int16_t kHomeNavCardHeight = 74;
constexpr int16_t kBackHitX = 40;
constexpr int16_t kBackHitY = 40;
constexpr int16_t kBackHitSize = 96;
constexpr int16_t kWorkoutControlHitX = 185;
constexpr int16_t kWorkoutControlHitY = 360;
constexpr int16_t kWorkoutControlHitSize = 96;

constexpr uint8_t kLc76gDeviceAddress = 0x50;
constexpr uint8_t kLc76gDeviceReadAddress = 0x54;
constexpr int kLc76gI2cSda = 15;
constexpr int kLc76gI2cScl = 14;
constexpr uint32_t kLc76gI2cFreqHz = 100000;
constexpr uint8_t kTouchI2cAddress = 0x5A;
constexpr uint8_t kRtcI2cAddress = 0x51;
constexpr uint8_t kRtcControl1Register = 0x00;
constexpr uint8_t kRtcSecondsRegister = 0x04;
constexpr uint8_t kRtcTimeRegisterCount = 7;

Arduino_DataBus* bus = new Arduino_ESP32QSPI(
    LCD_CS,
    LCD_SCLK,
    LCD_SDIO0,
    LCD_SDIO1,
    LCD_SDIO2,
    LCD_SDIO3
);

Arduino_CO5300* gfx = new Arduino_CO5300(
    bus,
    LCD_RESET,
    0,
    LCD_WIDTH,
    LCD_HEIGHT,
    6,
    0,
    0,
    0
);

lv_disp_draw_buf_t drawBuf;
lv_disp_drv_t dispDrv;
static lv_indev_drv_t indevDrv;
static lv_indev_t* touchIndev = nullptr;
lv_color_t* lvBuf1 = nullptr;
uint8_t currentAutoPage = 0;
uint8_t currentRenderedPage = 0;
bool displayBleServerReady = false;
bool powerKeyReady = false;
// CO5300 panel brightness state only; never mirror this into IG Display State.
uint8_t screenBrightnessDriverValue = kScreenBrightnessDefaultDriverValue;
uint32_t lastAutoPageMs = 0;
uint32_t lastGpsPollMs = 0;
uint32_t lastGpsDiagnosticsMs = 0;
uint32_t lastGpsTransportLogMs = 0;
uint32_t lastGpsBacklogLogMs = 0;
uint32_t lastI2cModuleDiagnosticsMs = 0;
uint32_t lastSystemSummaryMs = 0;
uint32_t gpsDiagnosticsBytesThisSecond = 0;
uint32_t gpsSummaryBytesLastWindow = 0;
uint32_t gpsDiagnosticsSentencesThisSecond = 0;
uint32_t gpsDiagnosticsLastBlockLength = 0;
uint32_t gpsDiagnosticsReadFailures = 0;
uint32_t gpsTransportTransactionStartMs = 0;
uint32_t gpsTransportTransactionEndMs = 0;
uint32_t gpsTransportLastNmeaLength = 0;
uint32_t gpsTransportSuccessCount = 0;
uint32_t gpsTransportNackCount = 0;
uint32_t gpsDiagnosticsLargeBytesThisSecond = 0;
uint32_t gpsDiagnosticsLargeEventsThisSecond = 0;
uint32_t gpsDiagnosticsLargeFailuresThisSecond = 0;
esp_err_t gpsDiagnosticsLengthWriteResult = ESP_OK;
esp_err_t gpsDiagnosticsLengthReadResult = ESP_OK;
esp_err_t gpsDiagnosticsNmeaWriteResult = ESP_OK;
esp_err_t gpsDiagnosticsNmeaReadResult = ESP_OK;
esp_err_t gpsI2cParamConfigResult = ESP_OK;
esp_err_t gpsI2cDriverInstallResult = ESP_OK;
esp_err_t sharedI2cParamConfigResult = ESP_OK;
esp_err_t sharedI2cDriverInstallResult = ESP_OK;
esp_err_t touchDiagLastI2cResult = ESP_OK;
bool gpsI2cDriverAlreadyInstalled = false;
bool gpsI2cBackendReusedExistingDriver = false;
bool sharedI2cReady = false;
bool sharedI2cDriverAlreadyInstalled = false;
bool sharedI2cMutexCreated = false;
uint32_t sharedI2cInitCount = 0;
bool gpsDiagnosticsSawBytesThisSecond = false;
const char* gpsReadyReason = "not_initialized";
uint32_t lastPmuPollMs = 0;
uint32_t lastPowerKeyPollMs = 0;
uint32_t lastRtcPollMs = 0;
bool touchDiagReadOk = false;
bool touchDiagTouched = false;
volatile bool touchInterruptPending = false;
int16_t touchDiagX = 0;
int16_t touchDiagY = 0;
uint32_t lastTouchPollMs = 0;
uint32_t touchNextPollAllowedMs = 0;
uint32_t lastTouchErrorLogMs = 0;
uint32_t touchTimeoutCount = 0;
const char* touchDiagStage = "idle";
uint16_t touchDiagRegister = 0;
size_t touchDiagLength = 0;
bool touchPollTransactionActive = false;
bool touchBlockNextRawRead = false;
esp_err_t touchBlockedRawReadResult = ESP_OK;
bool pmuDiagReadOk = false;
esp_err_t pmuDiagLastI2cResult = ESP_OK;
uint32_t pmuTimeoutCount = 0;
uint32_t pmuNextPollAllowedMs = 0;
uint32_t lastPmuErrorLogMs = 0;
const char* pmuDiagStage = "idle";
uint8_t pmuDiagRegister = 0;
uint8_t pmuDiagLength = 0;
bool pmuPollTransactionActive = false;
bool rtcDiagReadOk = false;
esp_err_t rtcDiagLastI2cResult = ESP_OK;
uint32_t rtcTimeoutCount = 0;
uint32_t rtcNextPollAllowedMs = 0;
uint32_t lastRtcErrorLogMs = 0;
const char* rtcDiagStage = "idle";
uint8_t rtcDiagRegister = 0;
uint8_t rtcDiagLength = 0;
bool rtcPollTransactionActive = false;
bool rtcFallbackUsed = true;
bool workoutRunning = false;
uint32_t workoutElapsedMs = 0;
uint32_t workoutLastTickMs = 0;
uint32_t workoutLastRenderedSecond = 0xFFFFFFFF;
float workoutDistanceKm = 0.0f;
bool workoutHasLastPoint = false;
double workoutLastLatitude = 0.0;
double workoutLastLongitude = 0.0;

struct PmuUiData {
    bool ready;
    int batteryPercent;
    float batteryVoltage;
    float systemVoltage;
    float temperature;
    bool charging;
    bool vbusIn;
    bool vbusGood;
};

struct RtcUiData {
    bool ready;
    bool valid;
    int hour;
    int minute;
    int second;
    int day;
    int month;
    int year;
};

uint8_t gpsLengthBytes[4] = {};
uint32_t gpsDataLength = 0;
bool gpsReady = false;
PmuUiData latestPmuData = {};
RtcUiData latestRtcData = {};

TouchDrvCST92xx touch;
XPowersAXP2101 power;
SystemPowerStateMachine systemPowerStateMachine;
SensorPCF85063 rtc;
int16_t touchX[5] = {};
int16_t touchY[5] = {};
bool touchReady = false;
GpsParser gpsParser;
GpsData latestGpsData = {};
WorkoutSpeedResolver workoutSpeedResolver;
bool workoutImuReady = false;
bool gpsValidSpeedSamplePending = false;
float effectiveWorkoutSpeedKmh = 0.0f;
WorkoutSpeedSource effectiveWorkoutSpeedSource = WORKOUT_SPEED_GPS;
#if ENABLE_IMU_SPEED_DEBUG_LOG
uint32_t lastImuSpeedDebugMs = 0;
#endif

void logSetupStep(uint8_t step, const char* message)
{
#if ENABLE_SETUP_STEP_LOG
    Serial.printf("SETUP[%02u]: %s\n", static_cast<unsigned>(step), message);
#else
    (void)step;
    (void)message;
#endif
}

void logSetupStepResult(uint8_t step, const char* name, bool ok)
{
#if ENABLE_SETUP_STEP_LOG
    Serial.printf("SETUP[%02u]: %s %s\n", static_cast<unsigned>(step), name, ok ? "ready" : "not ready");
#else
    (void)step;
    (void)name;
    (void)ok;
#endif
}

void renderPage(uint8_t page, bool force = false);
UiState createCurrentUiState();
UiState createHomeStatusUiState();
void applyGpsToUi();
void onPageGesture(lv_event_t* event);
void enableGestureBubble(lv_obj_t* obj);
void installHomeNavHitAreas();
void installBackHitArea();
void installWorkoutControlHitArea();
bool initGpsI2cBackend();
bool initSharedI2cBusOnce();
void printSystemSummaryIfDue(uint32_t now);
void powerManagerLoop(uint32_t now);

bool systemPowerAllowsBusinessWork()
{
    return systemPowerStateMachine.allowsBusinessWork();
}

void lvglTick(void* arg)
{
    (void)arg;
    lv_tick_inc(kLvglTickPeriodMs);
}

void rounderCb(lv_disp_drv_t* driver, lv_area_t* area)
{
    (void)driver;
    if ((area->x1 % 2) != 0) {
        area->x1--;
    }
    if ((area->y1 % 2) != 0) {
        area->y1--;
    }
    if ((area->x2 % 2) == 0) {
        area->x2++;
    }
    if ((area->y2 % 2) == 0) {
        area->y2++;
    }
}

void displayFlush(lv_disp_drv_t* display, const lv_area_t* area, lv_color_t* colors)
{
    if (!systemPowerAllowsBusinessWork()) {
        lv_disp_flush_ready(display);
        return;
    }

    const uint32_t width = area->x2 - area->x1 + 1;
    const uint32_t height = area->y2 - area->y1 + 1;

#if (LV_COLOR_16_SWAP != 0)
    gfx->draw16bitBeRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(&colors->full), width, height);
#else
    gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(&colors->full), width, height);
#endif

    lv_disp_flush_ready(display);
}

uint8_t getScreenBrightnessPercent()
{
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(screenBrightnessDriverValue) * 100U + 127U) / 255U
    );
}

void setScreenBrightnessPercent(uint8_t percent)
{
    const uint8_t clampedPercent = percent > 100 ? 100 : percent;
    const uint8_t driverValue = static_cast<uint8_t>(
        (static_cast<uint16_t>(clampedPercent) * 255U + 50U) / 100U
    );
    gfx->setBrightness(driverValue);
    screenBrightnessDriverValue = driverValue;
}

void initDisplay()
{
    gfx->begin();
    gfx->setBrightness(screenBrightnessDriverValue);
    gfx->fillScreen(0x0000);
}

void initLvgl()
{
    lv_init();

    const uint32_t drawBufferPixels = ui_theme::kScreenWidth * ui_theme::kScreenHeight / 4;
    lvBuf1 = static_cast<lv_color_t*>(heap_caps_malloc(drawBufferPixels * sizeof(lv_color_t), MALLOC_CAP_DMA));

    if (lvBuf1 == nullptr) {
        Serial.println("ERR: failed to allocate LVGL draw buffer");
        while (true) {
            delay(10);
        }
    }

    lv_disp_draw_buf_init(&drawBuf, lvBuf1, nullptr, drawBufferPixels);

    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = ui_theme::kScreenWidth;
    dispDrv.ver_res = ui_theme::kScreenHeight;
    dispDrv.flush_cb = displayFlush;
    dispDrv.rounder_cb = rounderCb;
    dispDrv.draw_buf = &drawBuf;
    dispDrv.sw_rotate = 1;
    lv_disp_drv_register(&dispDrv);
    lv_obj_add_event_cb(lv_scr_act(), onPageGesture, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_flag(lv_scr_act(), LV_OBJ_FLAG_GESTURE_BUBBLE);

    const esp_timer_create_args_t tickTimerArgs = {
        .callback = &lvglTick,
        .name = "lvgl_tick"
    };

    esp_timer_handle_t tickTimer = nullptr;
    esp_timer_create(&tickTimerArgs, &tickTimer);
    esp_timer_start_periodic(tickTimer, kLvglTickPeriodMs * 1000);
}

bool initSharedI2cBusOnce()
{
    if (sharedI2cReady) {
#if ENABLE_GPS_VERBOSE_LOG || ENABLE_TOUCH_DIAG_LOG || ENABLE_PMU_DIAG_LOG || ENABLE_RTC_DIAG_LOG
        Serial.printf(
            "SHARED_I2C: already ready owner=ARDUINO_WIRE_NG SDA=%d SCL=%d FREQ=%lu init_count=%lu\n",
            kLc76gI2cSda,
            kLc76gI2cScl,
            static_cast<unsigned long>(kLc76gI2cFreqHz),
            static_cast<unsigned long>(sharedI2cInitCount)
        );
#endif
        return true;
    }

    sharedI2cInitCount++;
    sharedI2cReady = Wire.begin(kLc76gI2cSda, kLc76gI2cScl, kLc76gI2cFreqHz);
    Wire.setTimeOut(1000);
    sharedI2cParamConfigResult = sharedI2cReady ? ESP_OK : ESP_FAIL;
    sharedI2cDriverInstallResult = sharedI2cParamConfigResult;
    sharedI2cDriverAlreadyInstalled = false;
    gpsI2cParamConfigResult = sharedI2cParamConfigResult;
    gpsI2cDriverInstallResult = sharedI2cDriverInstallResult;
    Serial.printf(
        "SHARED_I2C: owner=ARDUINO_WIRE_NG sda=%d scl=%d freq=%lu init_count=%lu\n",
        kLc76gI2cSda,
        kLc76gI2cScl,
        static_cast<unsigned long>(kLc76gI2cFreqHz),
        static_cast<unsigned long>(sharedI2cInitCount)
    );
    if (!sharedI2cReady) {
        Serial.println("ERR: shared_i2c wire_begin_failed");
    }
    return sharedI2cReady;
}

uint16_t clampWireTimeout(uint32_t timeoutMs)
{
    return static_cast<uint16_t>(timeoutMs > 0xFFFFU ? 0xFFFFU : timeoutMs);
}

esp_err_t wireTransmissionResult(uint8_t result)
{
    if (result == 0) {
        return ESP_OK;
    }
    if (result == 5) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_FAIL;
}

const char* i2cErrName(esp_err_t err)
{
    if (err == ESP_ERR_TIMEOUT) {
        return "ESP_ERR_TIMEOUT";
    }
    return esp_err_to_name(err);
}

uint16_t firstTouchCommandWord(const uint8_t* data, size_t dataLength)
{
    if (data == nullptr || dataLength < 2) {
        return 0;
    }
    return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}

void setTouchDiagStage(const char* stage, uint16_t reg, size_t len)
{
    touchDiagStage = stage;
    touchDiagRegister = reg;
    touchDiagLength = len;
}

void updateTouchI2cResult(esp_err_t result)
{
    touchDiagLastI2cResult = result;

    if (result == ESP_OK) {
        return;
    }

    if (touchPollTransactionActive && result == ESP_ERR_TIMEOUT) {
        touchTimeoutCount++;
        if (touchTimeoutCount >= kTouchTimeoutBackoffThreshold) {
            touchNextPollAllowedMs = millis() + kTouchTimeoutBackoffMs;
        }
    }

    const uint32_t now = millis();
    if (lastTouchErrorLogMs == 0 || (now - lastTouchErrorLogMs) >= kGpsDiagnosticsIntervalMs) {
        lastTouchErrorLogMs = now;
#if ENABLE_TOUCH_DIAG_LOG
        Serial.printf(
            "TOUCH_I2C_ERROR: TOUCH_STAGE=%s TOUCH_REG=0x%04X TOUCH_LEN=%lu TOUCH_I2C_RESULT=%d TOUCH_ERR_NAME=%s TOUCH_TIMEOUT_COUNT=%lu TOUCH_POLL_INTERVAL_MS=%lu ACTION=%s\n",
            touchDiagStage,
            touchDiagRegister,
            static_cast<unsigned long>(touchDiagLength),
            static_cast<int>(result),
            i2cErrName(result),
            static_cast<unsigned long>(touchTimeoutCount),
            static_cast<unsigned long>(kTouchPollIntervalMs),
            (touchPollTransactionActive && touchTimeoutCount >= kTouchTimeoutBackoffThreshold) ? "backoff_touch_poll" : "release_mutex_and_retry_later"
        );
#endif
    }
}

esp_err_t sharedI2cWrite(uint8_t deviceAddress, const uint8_t* data, size_t dataLength, uint32_t timeoutMs = 1000)
{
    if (!sharedI2cReady) {
        return ESP_ERR_INVALID_STATE;
    }
    if (dataLength > 0 && data == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dataLength > I2C_BUFFER_LENGTH) {
        return ESP_ERR_INVALID_SIZE;
    }

    Wire.setTimeOut(clampWireTimeout(timeoutMs));
    Wire.beginTransmission(deviceAddress);
    if (dataLength > 0) {
        const size_t written = Wire.write(data, dataLength);
        if (written != dataLength) {
            Wire.endTransmission();
            return ESP_ERR_INVALID_SIZE;
        }
    }
    return wireTransmissionResult(Wire.endTransmission());
}

struct WireChunkReadContext {
    uint8_t deviceAddress;
};

GpsI2cChunkResult readWireChunk(void* context, uint8_t* destination, size_t requestedLength)
{
    WireChunkReadContext* wireContext = static_cast<WireChunkReadContext*>(context);
    const size_t reported = Wire.requestFrom(wireContext->deviceAddress, requestedLength);
    if (reported == 0) {
        return { false, 0 };
    }

    size_t actual = 0;
    while (actual < requestedLength && Wire.available() > 0) {
        const int value = Wire.read();
        if (value < 0) {
            break;
        }
        destination[actual++] = static_cast<uint8_t>(value);
    }
    return { true, actual };
}

esp_err_t sharedI2cRead(uint8_t deviceAddress, uint8_t* data, size_t dataLength, uint32_t timeoutMs = 1000)
{
    if (!sharedI2cReady) {
        return ESP_ERR_INVALID_STATE;
    }
    if (dataLength > 0 && data == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dataLength == 0) {
        return ESP_OK;
    }

    Wire.setTimeOut(clampWireTimeout(timeoutMs));
    WireChunkReadContext context = { deviceAddress };
    const GpsI2cTransportStatus status = gpsI2cReadExactChunked(
        readWireChunk,
        &context,
        data,
        dataLength,
        dataLength,
        I2C_BUFFER_LENGTH,
        dataLength
    );
    if (status == GpsI2cTransportStatus::OK) {
        return ESP_OK;
    }
    if (status == GpsI2cTransportStatus::INVALID_ARGUMENT ||
        status == GpsI2cTransportStatus::OUTPUT_TOO_SMALL) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_FAIL;
}

esp_err_t sharedI2cWriteRead(
    uint8_t deviceAddress,
    const uint8_t* writeData,
    size_t writeLength,
    uint8_t* readData,
    size_t readLength,
    uint32_t timeoutMs = 1000
)
{
    if (!sharedI2cReady) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((writeLength > 0 && writeData == nullptr) || (readLength > 0 && readData == nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (writeLength > I2C_BUFFER_LENGTH || readLength > I2C_BUFFER_LENGTH) {
        return ESP_ERR_INVALID_SIZE;
    }

    Wire.setTimeOut(clampWireTimeout(timeoutMs));
    Wire.beginTransmission(deviceAddress);
    if (writeLength > 0) {
        const size_t written = Wire.write(writeData, writeLength);
        if (written != writeLength) {
            Wire.endTransmission();
            return ESP_ERR_INVALID_SIZE;
        }
    }
    const esp_err_t writeResult = wireTransmissionResult(Wire.endTransmission(false));
    if (writeResult != ESP_OK) {
        return writeResult;
    }
    if (readLength == 0) {
        return ESP_OK;
    }

    const size_t reported = Wire.requestFrom(deviceAddress, readLength);
    if (reported == 0) {
        return ESP_FAIL;
    }
    size_t actual = 0;
    while (actual < readLength && Wire.available() > 0) {
        const int value = Wire.read();
        if (value < 0) {
            break;
        }
        readData[actual++] = static_cast<uint8_t>(value);
    }
    return actual == readLength ? ESP_OK : ESP_FAIL;
}

bool touchI2cCustomCallback(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len, bool writeReg, bool isWrite)
{
    if (len > 0 && buf == nullptr) {
        setTouchDiagStage(touchPollTransactionActive ? "poll_invalid_arg" : "init_invalid_arg", reg, len);
        updateTouchI2cResult(ESP_ERR_INVALID_ARG);
        return false;
    }

    if (isWrite) {
        if (!writeReg) {
            const uint16_t command = firstTouchCommandWord(buf, len);
            const char* stage = touchPollTransactionActive ?
                ((len == 3 && command == 0xD000) ? "poll_ack" : "poll_write") :
                "init_write";
            setTouchDiagStage(stage, command, len);
            touchDiagLastI2cResult = sharedI2cWrite(addr, buf, len, touchPollTransactionActive ? kTouchI2cTimeoutMs : 1000);
            updateTouchI2cResult(touchDiagLastI2cResult);
            if (len == 2) {
                touchBlockNextRawRead = touchDiagLastI2cResult != ESP_OK;
                touchBlockedRawReadResult = touchDiagLastI2cResult;
            }
            return touchDiagLastI2cResult == ESP_OK;
        }

        uint8_t* packet = static_cast<uint8_t*>(malloc(len + 1));
        if (packet == nullptr) {
            setTouchDiagStage(touchPollTransactionActive ? "poll_write_alloc" : "init_write_alloc", reg, len + 1);
            updateTouchI2cResult(ESP_ERR_NO_MEM);
            return false;
        }
        packet[0] = reg;
        if (len > 0) {
            memcpy(packet + 1, buf, len);
        }
        setTouchDiagStage(touchPollTransactionActive ? "poll_write_reg" : "init_write_reg", reg, len + 1);
        touchDiagLastI2cResult = sharedI2cWrite(addr, packet, len + 1, touchPollTransactionActive ? kTouchI2cTimeoutMs : 1000);
        free(packet);
        updateTouchI2cResult(touchDiagLastI2cResult);
        return touchDiagLastI2cResult == ESP_OK;
    }

    if (writeReg) {
        const uint8_t regByte = reg;
        setTouchDiagStage(touchPollTransactionActive ? "poll_read_reg_write" : "init_read_reg_write", reg, 1);
        touchDiagLastI2cResult = sharedI2cWrite(addr, &regByte, 1, touchPollTransactionActive ? kTouchI2cTimeoutMs : 1000);
        updateTouchI2cResult(touchDiagLastI2cResult);
        if (touchDiagLastI2cResult != ESP_OK) {
            return false;
        }
        setTouchDiagStage(touchPollTransactionActive ? "poll_read_reg" : "init_read_reg", reg, len);
        touchDiagLastI2cResult = sharedI2cRead(addr, buf, len, touchPollTransactionActive ? kTouchI2cTimeoutMs : 1000);
    } else {
        if (touchBlockNextRawRead) {
            touchBlockNextRawRead = false;
            setTouchDiagStage(touchPollTransactionActive ? "poll_read_skipped_after_write_fail" : "init_read_skipped_after_write_fail", touchDiagRegister, len);
            updateTouchI2cResult(touchBlockedRawReadResult);
            return false;
        }
        setTouchDiagStage(touchPollTransactionActive ? "poll_read" : "init_read", touchDiagRegister, len);
        touchDiagLastI2cResult = sharedI2cRead(addr, buf, len, touchPollTransactionActive ? kTouchI2cTimeoutMs : 1000);
    }
    updateTouchI2cResult(touchDiagLastI2cResult);
    return touchDiagLastI2cResult == ESP_OK;
}

uint32_t touchHalCallback(SensorCommCustomHal::Operation op, void* param1, void* param2)
{
    const uintptr_t value1 = reinterpret_cast<uintptr_t>(param1);
    const uintptr_t value2 = reinterpret_cast<uintptr_t>(param2);

    switch (op) {
    case SensorCommCustomHal::OP_PINMODE:
        pinMode(static_cast<uint8_t>(value1), static_cast<uint8_t>(value2));
        return 0;
    case SensorCommCustomHal::OP_DIGITALWRITE:
        digitalWrite(static_cast<uint8_t>(value1), static_cast<uint8_t>(value2));
        return 0;
    case SensorCommCustomHal::OP_DIGITALREAD:
        return digitalRead(static_cast<uint8_t>(value1));
    case SensorCommCustomHal::OP_MILLIS:
        return millis();
    case SensorCommCustomHal::OP_DELAY:
        delay(static_cast<uint32_t>(value1));
        return 0;
    case SensorCommCustomHal::OP_DELAYMICROSECONDS:
        delayMicroseconds(static_cast<uint32_t>(value1));
        return 0;
    default:
        return 0;
    }
}

bool workoutImuI2cCallback(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len, bool writeReg, bool isWrite)
{
    if (len > 0 && buf == nullptr) return false;
    if (isWrite) {
        if (!writeReg) return sharedI2cWrite(addr, buf, len, 100) == ESP_OK;
        if (len > (I2C_BUFFER_LENGTH - 1)) return false;
        uint8_t packet[I2C_BUFFER_LENGTH] = {};
        packet[0] = reg;
        if (len > 0) memcpy(packet + 1, buf, len);
        return sharedI2cWrite(addr, packet, len + 1, 100) == ESP_OK;
    }
    if (writeReg) {
        return sharedI2cWriteRead(addr, &reg, 1, buf, len, 100) == ESP_OK;
    }
    return sharedI2cRead(addr, buf, len, 100) == ESP_OK;
}

uint32_t workoutImuHalCallback(SensorCommCustomHal::Operation op, void* param1, void* param2)
{
    return touchHalCallback(op, param1, param2);
}

void touchpadRead(lv_indev_drv_t* indev, lv_indev_data_t* data)
{
    (void)indev;

#if !DIAG_INIT_TOUCH || !DIAG_POLL_TOUCH
    touchDiagReadOk = false;
    touchDiagTouched = false;
    data->state = LV_INDEV_STATE_REL;
    return;
#else
    if (!touchReady) {
        touchDiagReadOk = false;
        touchDiagTouched = false;
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    const uint32_t now = millis();
    if (!touchInterruptPending) {
        if (touchDiagReadOk && touchDiagTouched) {
            data->state = LV_INDEV_STATE_PR;
            data->point.x = touchDiagX;
            data->point.y = touchDiagY;
        } else {
            data->state = LV_INDEV_STATE_REL;
        }
        return;
    }

    if (now < touchNextPollAllowedMs || (lastTouchPollMs != 0 && (now - lastTouchPollMs) < kTouchPollIntervalMs)) {
        if (touchDiagReadOk && touchDiagTouched) {
            data->state = LV_INDEV_STATE_PR;
            data->point.x = touchDiagX;
            data->point.y = touchDiagY;
        } else {
            data->state = LV_INDEV_STATE_REL;
        }
        return;
    }
    lastTouchPollMs = now;
    touchInterruptPending = false;

    touchDiagLastI2cResult = ESP_OK;
    setTouchDiagStage("poll_begin", 0, 0);
    touchPollTransactionActive = true;
    const uint8_t touched = touch.getPoint(touchX, touchY, touch.getSupportTouchPoint());
    touchPollTransactionActive = false;
    touchDiagReadOk = touchDiagLastI2cResult == ESP_OK;
    touchDiagTouched = touched > 0;
    if (!touchDiagReadOk) {
        touchDiagTouched = false;
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    touchTimeoutCount = 0;
    touchNextPollAllowedMs = 0;
    if (touched > 0) {
        touchDiagX = touchX[0];
        touchDiagY = touchY[0];
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touchX[0];
        data->point.y = touchY[0];
        return;
    }

    data->state = LV_INDEV_STATE_REL;
#endif
}

void initTouch()
{
    touchReady = false;
    touchDiagReadOk = false;
    touchDiagTouched = false;
    touchDiagLastI2cResult = ESP_OK;

    if (!initSharedI2cBusOnce()) {
#if ENABLE_TOUCH_DIAG_LOG
        Serial.printf(
            "TOUCH_I2C_INIT: shared I2C not ready; touch will still register LVGL input. SHARED_I2C_READY=0 PARAM_CONFIG=%d DRIVER_INSTALL=%d\n",
            static_cast<int>(sharedI2cParamConfigResult),
            static_cast<int>(sharedI2cDriverInstallResult)
        );
#endif
    }

#if ENABLE_TOUCH_DIAG_LOG
    Serial.printf(
        "TOUCH_I2C_INIT: Wire.begin called=1 owner_only=1 install_driver=0 shared_i2c=1 callback_only=1 SDA=%d SCL=%d FREQ=%lu ADDR=0x%02X TOUCH_BACKEND=ARDUINO_WIRE_NG TOUCH_SHARED_I2C_WITH_GPS=%d I2C_PORT=%d SHARED_I2C_READY=%d\n",
        IIC_SDA,
        IIC_SCL,
        static_cast<unsigned long>(kLc76gI2cFreqHz),
        kTouchI2cAddress,
        (IIC_SDA == kLc76gI2cSda && IIC_SCL == kLc76gI2cScl) ? 1 : 0,
        static_cast<int>(Wire.getBusNum()),
        sharedI2cReady ? 1 : 0
    );
#endif

    pinMode(TP_RESET, OUTPUT);
    pinMode(TP_INT, INPUT_PULLUP);
    digitalWrite(TP_RESET, LOW);
    delay(30);
    digitalWrite(TP_RESET, HIGH);
    delay(10);

    touch.setPins(TP_RESET, TP_INT);
    touchReady = touch.begin(touchI2cCustomCallback, touchHalCallback, kTouchI2cAddress);

    lv_indev_drv_init(&indevDrv);
    indevDrv.type = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = touchpadRead;
    touchIndev = lv_indev_drv_register(&indevDrv);
    if (touchIndev == nullptr) {
        Serial.println("ERR: lvgl touch input registration failed");
    }

    if (!touchReady) {
#if ENABLE_TOUCH_DIAG_LOG
        Serial.println("Touch not detected; UI pages remain visible.");
        Serial.printf(
            "TOUCH_DIAG: INIT=%d INIT_OK=0 TOUCH_INIT_OK=0 POLL=%d TOUCH_POLL_ENABLED=%d READ_OK=0 TOUCH_READ_OK=0 TOUCHED=0 X=0 Y=0 TOUCH_BACKEND=ARDUINO_WIRE_NG TOUCH_SHARED_I2C=1 TOUCH_INSTALL_DRIVER=0 TOUCH_CALLBACK_ONLY=1 TOUCH_I2C_RESULT=%d TOUCH_ERR_NAME=%s TOUCH_TIMEOUT_COUNT=%lu TOUCH_STAGE=%s TOUCH_REG=0x%04X TOUCH_LEN=%lu TOUCH_POLL_INTERVAL_MS=%lu SHARED_I2C_READY=%d TOUCH_SHARED_I2C_WITH_GPS=%d\n",
            DIAG_INIT_TOUCH,
            DIAG_POLL_TOUCH,
            (DIAG_INIT_TOUCH && DIAG_POLL_TOUCH) ? 1 : 0,
            static_cast<int>(touchDiagLastI2cResult),
            i2cErrName(touchDiagLastI2cResult),
            static_cast<unsigned long>(touchTimeoutCount),
            touchDiagStage,
            touchDiagRegister,
            static_cast<unsigned long>(touchDiagLength),
            static_cast<unsigned long>(kTouchPollIntervalMs),
            sharedI2cReady ? 1 : 0,
            (IIC_SDA == kLc76gI2cSda && IIC_SCL == kLc76gI2cScl) ? 1 : 0
        );
#endif
        return;
    }

#if ENABLE_TOUCH_DIAG_LOG
    Serial.print("Touch ready: ");
    Serial.println(touch.getModelName());
    Serial.printf(
        "TOUCH_DIAG: INIT=%d INIT_OK=1 TOUCH_INIT_OK=1 POLL=%d TOUCH_POLL_ENABLED=%d READ_OK=0 TOUCH_READ_OK=0 TOUCHED=0 X=0 Y=0 TOUCH_BACKEND=ARDUINO_WIRE_NG TOUCH_SHARED_I2C=1 TOUCH_INSTALL_DRIVER=0 TOUCH_CALLBACK_ONLY=1 TOUCH_I2C_RESULT=%d TOUCH_ERR_NAME=%s TOUCH_TIMEOUT_COUNT=%lu TOUCH_STAGE=%s TOUCH_REG=0x%04X TOUCH_LEN=%lu TOUCH_POLL_INTERVAL_MS=%lu SHARED_I2C_READY=%d TOUCH_SHARED_I2C_WITH_GPS=%d\n",
        DIAG_INIT_TOUCH,
        DIAG_POLL_TOUCH,
        (DIAG_INIT_TOUCH && DIAG_POLL_TOUCH) ? 1 : 0,
        static_cast<int>(touchDiagLastI2cResult),
        i2cErrName(touchDiagLastI2cResult),
        static_cast<unsigned long>(touchTimeoutCount),
        touchDiagStage,
        touchDiagRegister,
        static_cast<unsigned long>(touchDiagLength),
        static_cast<unsigned long>(kTouchPollIntervalMs),
        sharedI2cReady ? 1 : 0,
        (IIC_SDA == kLc76gI2cSda && IIC_SCL == kLc76gI2cScl) ? 1 : 0
    );
#endif
    touch.sleep();
    touch.reset();
    touch.setMaxCoordinates(ui_theme::kScreenWidth, ui_theme::kScreenHeight);
    touch.setMirrorXY(true, true);
    touchInterruptPending = false;
    attachInterrupt(digitalPinToInterrupt(TP_INT), []() {
        touchInterruptPending = true;
    }, FALLING);
}

void enablePmuAdc()
{
    power.enableTemperatureMeasure();
    power.enableBattDetection();
    power.enableVbusVoltageMeasure();
    power.enableBattVoltageMeasure();
    power.enableSystemVoltageMeasure();
}

void setPmuDiagStage(const char* stage, uint8_t reg, uint8_t len)
{
    pmuDiagStage = stage;
    pmuDiagRegister = reg;
    pmuDiagLength = len;
}

void updatePmuI2cResult(esp_err_t result)
{
    pmuDiagLastI2cResult = result;

    if (result == ESP_OK) {
        return;
    }

    if (pmuPollTransactionActive && result == ESP_ERR_TIMEOUT) {
        pmuTimeoutCount++;
        if (pmuTimeoutCount >= kPmuTimeoutBackoffThreshold) {
            pmuNextPollAllowedMs = millis() + kPmuTimeoutBackoffMs;
        }
    }

    const uint32_t now = millis();
    if (lastPmuErrorLogMs == 0 || (now - lastPmuErrorLogMs) >= kGpsDiagnosticsIntervalMs) {
        lastPmuErrorLogMs = now;
#if ENABLE_PMU_DIAG_LOG
        Serial.printf(
            "PMU_I2C_ERROR: PMU_STAGE=%s PMU_REG=0x%02X PMU_LEN=%u PMU_I2C_RESULT=%d PMU_ERR_NAME=%s PMU_TIMEOUT_COUNT=%lu PMU_POLL_INTERVAL_MS=%lu ACTION=%s\n",
            pmuDiagStage,
            pmuDiagRegister,
            pmuDiagLength,
            static_cast<int>(result),
            i2cErrName(result),
            static_cast<unsigned long>(pmuTimeoutCount),
            static_cast<unsigned long>(kPmuPollIntervalMs),
            (pmuPollTransactionActive && pmuTimeoutCount >= kPmuTimeoutBackoffThreshold) ? "backoff_pmu_poll" : "release_mutex_and_retry_later"
        );
#endif
    }
}

int pmuRegisterRead(uint8_t devAddr, uint8_t regAddr, uint8_t* data, uint8_t len)
{
    if (len > 0 && data == nullptr) {
        setPmuDiagStage(pmuPollTransactionActive ? "poll_invalid_arg" : "init_invalid_arg", regAddr, len);
        updatePmuI2cResult(ESP_ERR_INVALID_ARG);
        return -1;
    }

    setPmuDiagStage(pmuPollTransactionActive ? "poll_read" : "init_read", regAddr, len);
    const uint8_t reg = regAddr;
    const esp_err_t result = sharedI2cWriteRead(devAddr, &reg, 1, data, len, pmuPollTransactionActive ? kPmuI2cTimeoutMs : 1000);
    updatePmuI2cResult(result);
    return result == ESP_OK ? 0 : -1;
}

int pmuRegisterWrite(uint8_t devAddr, uint8_t regAddr, uint8_t* data, uint8_t len)
{
    if (len > 0 && data == nullptr) {
        setPmuDiagStage(pmuPollTransactionActive ? "poll_invalid_arg" : "init_invalid_arg", regAddr, len);
        updatePmuI2cResult(ESP_ERR_INVALID_ARG);
        return -1;
    }

    const size_t packetLength = static_cast<size_t>(len) + 1;
    uint8_t* packet = static_cast<uint8_t*>(malloc(packetLength));
    if (packet == nullptr) {
        setPmuDiagStage(pmuPollTransactionActive ? "poll_write_alloc" : "init_write_alloc", regAddr, len);
        updatePmuI2cResult(ESP_ERR_NO_MEM);
        return -1;
    }

    packet[0] = regAddr;
    if (len > 0) {
        memcpy(packet + 1, data, len);
    }

    setPmuDiagStage(pmuPollTransactionActive ? "poll_write" : "init_write", regAddr, len);
    const esp_err_t result = sharedI2cWrite(devAddr, packet, packetLength, pmuPollTransactionActive ? kPmuI2cTimeoutMs : 1000);
    free(packet);
    updatePmuI2cResult(result);
    return result == ESP_OK ? 0 : -1;
}

void initPmu()
{
    pmuDiagReadOk = false;
    pmuDiagLastI2cResult = ESP_OK;
    pmuTimeoutCount = 0;
    pmuNextPollAllowedMs = 0;
    pmuPollTransactionActive = false;
    setPmuDiagStage("init_start", 0, 0);
    latestPmuData.ready = false;
    latestPmuData.batteryPercent = 76;
    latestPmuData.batteryVoltage = 0.0f;
    latestPmuData.systemVoltage = 0.0f;
    latestPmuData.temperature = 0.0f;
    latestPmuData.charging = false;
    latestPmuData.vbusIn = false;
    latestPmuData.vbusGood = false;

    if (!initSharedI2cBusOnce()) {
        updatePmuI2cResult(ESP_ERR_INVALID_STATE);
#if ENABLE_PMU_DIAG_LOG
        Serial.printf(
            "PMU_DIAG: INIT=%d INIT_OK=0 POLL=%d READ_OK=0 PMU_BACKEND=ARDUINO_WIRE_NG PMU_SHARED_I2C=1 PMU_INSTALL_DRIVER=0 PMU_CALLBACK_ONLY=1 PMU_I2C_RESULT=%d PMU_ERR_NAME=%s PMU_TIMEOUT_COUNT=%lu PMU_STAGE=%s PMU_REG=0x%02X PMU_LEN=%u PMU_POLL_INTERVAL_MS=%lu BAT=-1 USB=0 VBUS=0 SYS=0.00 TEMP=0.0 SHARED_I2C_READY=%d PMU_SHARED_I2C_WITH_GPS=%d\n",
            DIAG_INIT_PMU,
            DIAG_POLL_PMU,
            static_cast<int>(pmuDiagLastI2cResult),
            i2cErrName(pmuDiagLastI2cResult),
            static_cast<unsigned long>(pmuTimeoutCount),
            pmuDiagStage,
            pmuDiagRegister,
            pmuDiagLength,
            static_cast<unsigned long>(kPmuPollIntervalMs),
            sharedI2cReady ? 1 : 0,
            (IIC_SDA == kLc76gI2cSda && IIC_SCL == kLc76gI2cScl) ? 1 : 0
        );
#endif
        return;
    }

    if (!power.begin(AXP2101_SLAVE_ADDRESS, pmuRegisterRead, pmuRegisterWrite)) {
#if ENABLE_PMU_UI_LOG
        Serial.println("PMU UI: AXP2101 init failed; using mock battery.");
#endif
#if ENABLE_PMU_DIAG_LOG
        Serial.printf(
            "PMU_DIAG: INIT=%d INIT_OK=0 POLL=%d READ_OK=0 PMU_BACKEND=ARDUINO_WIRE_NG PMU_SHARED_I2C=1 PMU_INSTALL_DRIVER=0 PMU_CALLBACK_ONLY=1 PMU_I2C_RESULT=%d PMU_ERR_NAME=%s PMU_TIMEOUT_COUNT=%lu PMU_STAGE=%s PMU_REG=0x%02X PMU_LEN=%u PMU_POLL_INTERVAL_MS=%lu BAT=-1 USB=0 VBUS=0 SYS=0.00 TEMP=0.0 SHARED_I2C_READY=%d PMU_SHARED_I2C_WITH_GPS=%d\n",
            DIAG_INIT_PMU,
            DIAG_POLL_PMU,
            static_cast<int>(pmuDiagLastI2cResult),
            i2cErrName(pmuDiagLastI2cResult),
            static_cast<unsigned long>(pmuTimeoutCount),
            pmuDiagStage,
            pmuDiagRegister,
            pmuDiagLength,
            static_cast<unsigned long>(kPmuPollIntervalMs),
            sharedI2cReady ? 1 : 0,
            (IIC_SDA == kLc76gI2cSda && IIC_SCL == kLc76gI2cScl) ? 1 : 0
        );
#endif
        return;
    }

    power.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    power.setChargeTargetVoltage(3);
    power.clearIrqStatus();
    powerKeyReady = power.enableIRQ(
        XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ |
        XPOWERS_AXP2101_PKEY_POSITIVE_IRQ
    );
    enablePmuAdc();

    latestPmuData.ready = pmuDiagLastI2cResult == ESP_OK;
    powerKeyReady = powerKeyReady && latestPmuData.ready;
    lastPmuPollMs = millis() - kPmuPollIntervalMs;
    lastPowerKeyPollMs = millis();
    ESP_LOGI(
        "POWER",
        "POWER_KEY: source=axp2101_pek press_irq=negative release_irq=positive threshold_ms=%lu ready=%d",
        static_cast<unsigned long>(SystemPowerStateMachine::kLongPressMs),
        powerKeyReady ? 1 : 0
    );
#if ENABLE_PMU_UI_LOG
    Serial.println("PMU UI: AXP2101 ready.");
#endif
#if ENABLE_PMU_DIAG_LOG
    Serial.printf(
        "PMU_DIAG: INIT=%d INIT_OK=%d POLL=%d READ_OK=0 PMU_BACKEND=ARDUINO_WIRE_NG PMU_SHARED_I2C=1 PMU_INSTALL_DRIVER=0 PMU_CALLBACK_ONLY=1 PMU_I2C_RESULT=%d PMU_ERR_NAME=%s PMU_TIMEOUT_COUNT=%lu PMU_STAGE=%s PMU_REG=0x%02X PMU_LEN=%u PMU_POLL_INTERVAL_MS=%lu BAT=%d USB=0 VBUS=0 SYS=%.2f TEMP=%.1f SHARED_I2C_READY=%d PMU_SHARED_I2C_WITH_GPS=%d\n",
        DIAG_INIT_PMU,
        latestPmuData.ready ? 1 : 0,
        DIAG_POLL_PMU,
        static_cast<int>(pmuDiagLastI2cResult),
        i2cErrName(pmuDiagLastI2cResult),
        static_cast<unsigned long>(pmuTimeoutCount),
        pmuDiagStage,
        pmuDiagRegister,
        pmuDiagLength,
        static_cast<unsigned long>(kPmuPollIntervalMs),
        latestPmuData.batteryPercent,
        latestPmuData.systemVoltage,
        latestPmuData.temperature,
        sharedI2cReady ? 1 : 0,
        (IIC_SDA == kLc76gI2cSda && IIC_SCL == kLc76gI2cScl) ? 1 : 0
    );
#endif
}

void applyPmuToUi()
{
    if (currentRenderedPage == UI_SHOW_PAGE_HOME) {
        ui_home_status_update(createHomeStatusUiState());
    } else if (currentRenderedPage == UI_SHOW_PAGE_SETTINGS) {
        ui_settings_update(createHomeStatusUiState());
    } else if (currentRenderedPage == UI_SHOW_PAGE_WORKOUT) {
        ui_workout_gps_update(createCurrentUiState());
    } else if (currentRenderedPage == UI_SHOW_PAGE_IG) {
        ui_ig_control_update_battery(createCurrentUiState());
    } else if (currentRenderedPage == UI_SHOW_PAGE_HOME_MODE) {
        ui_home_mode_update_battery(createCurrentUiState());
    }
}

void enterSystemSoftOff(uint32_t now, uint32_t pressDurationMs)
{
    workoutImuSpeedPause();
    workoutSpeedResolver.pause(now);
    ESP_LOGI(
        "POWER",
        "POWER_STATE: from=running to=shutdown_pending timestamp_ms=%lu duration_ms=%lu",
        static_cast<unsigned long>(now),
        static_cast<unsigned long>(pressDurationMs)
    );

    gfx->fillScreen(0x0000);
    gfx->displayOff();
    systemPowerStateMachine.completeSoftOff();

    ESP_LOGI(
        "POWER",
        "POWER_STATE: from=shutdown_pending to=soft_off timestamp_ms=%lu wait_for_release=1",
        static_cast<unsigned long>(millis())
    );
}

void powerManagerLoop(uint32_t now)
{
    if (!powerKeyReady || (now - lastPowerKeyPollMs) < kPowerKeyPollIntervalMs) {
        return;
    }
    lastPowerKeyPollMs = now;

    pmuDiagLastI2cResult = ESP_OK;
    pmuPollTransactionActive = true;
    const uint64_t irqStatus = power.getIrqStatus();
    const esp_err_t readResult = pmuDiagLastI2cResult;
    const bool pressEdge = readResult == ESP_OK && power.isPekeyNegativeIrq();
    const bool releaseEdge = readResult == ESP_OK && power.isPekeyPositiveIrq();
    if (readResult == ESP_OK && irqStatus != 0) {
        power.clearIrqStatus();
    }
    pmuPollTransactionActive = false;

    if (readResult != ESP_OK) {
        return;
    }

    const PowerButtonUpdate update = systemPowerStateMachine.update(pressEdge, releaseEdge, now);
    if (update.pressAccepted) {
        ESP_LOGI(
            "POWER",
            "POWER_KEY: event=press timestamp_ms=%lu state=%s",
            static_cast<unsigned long>(now),
            systemPowerStateName(systemPowerStateMachine.state())
        );
    }

    if (update.action == PowerButtonAction::EnterSoftOff) {
        ESP_LOGI(
            "POWER",
            "POWER_KEY: event=long_press duration_ms=%lu action=soft_off triggered=1 wait_for_release=1",
            static_cast<unsigned long>(update.durationMs)
        );
        enterSystemSoftOff(now, update.durationMs);
    } else if (update.action == PowerButtonAction::Restart) {
        ESP_LOGI(
            "POWER",
            "POWER_KEY: event=long_press duration_ms=%lu action=restart triggered=1 wait_for_release=1",
            static_cast<unsigned long>(update.durationMs)
        );
        ESP_LOGI(
            "POWER",
            "POWER_STATE: from=soft_off to=restart_requested timestamp_ms=%lu",
            static_cast<unsigned long>(now)
        );
        esp_restart();
    }

    if (update.releaseAccepted) {
        ESP_LOGI(
            "POWER",
            "POWER_KEY: event=release duration_ms=%lu action=%s state=%s",
            static_cast<unsigned long>(update.durationMs),
            powerButtonActionName(update.completedAction),
            systemPowerStateName(systemPowerStateMachine.state())
        );
    }
}

void updatePmu()
{
    if (!latestPmuData.ready) {
        return;
    }

    const uint32_t now = millis();
    if ((now - lastPmuPollMs) < kPmuPollIntervalMs) {
        return;
    }
    if (now < pmuNextPollAllowedMs) {
        return;
    }
    lastPmuPollMs = now;

    pmuDiagReadOk = false;
    pmuDiagLastI2cResult = ESP_OK;
    pmuPollTransactionActive = true;
    const bool batteryConnected = power.isBatteryConnect();
    if (pmuDiagLastI2cResult != ESP_OK) {
        pmuPollTransactionActive = false;
        displayStateSetDisplayBattery(DISPLAY_STATE_BATTERY_UNKNOWN);
        return;
    }
    const int batteryPercent = batteryConnected ? power.getBatteryPercent() : -1;
    const float batteryVoltage = power.getBattVoltage() / 1000.0f;
    const float systemVoltage = power.getSystemVoltage() / 1000.0f;
    const float temperature = power.getTemperature();
    const bool charging = power.isCharging();
    const bool vbusIn = power.isVbusIn();
    const bool vbusGood = power.isVbusGood();
    pmuPollTransactionActive = false;

    if (pmuDiagLastI2cResult != ESP_OK) {
        displayStateSetDisplayBattery(DISPLAY_STATE_BATTERY_UNKNOWN);
        return;
    }

    latestPmuData.batteryPercent = batteryPercent;
    latestPmuData.batteryVoltage = batteryVoltage;
    latestPmuData.systemVoltage = systemVoltage;
    latestPmuData.temperature = temperature;
    latestPmuData.charging = charging;
    latestPmuData.vbusIn = vbusIn;
    latestPmuData.vbusGood = vbusGood;
    pmuDiagReadOk = true;

    const bool displayBatteryValid = uiBatteryRingBatteryValid(
        latestPmuData.ready,
        latestPmuData.batteryVoltage,
        latestPmuData.batteryPercent
    );
    displayStateSetDisplayBattery(
        displayBatteryValid
            ? static_cast<uint8_t>(latestPmuData.batteryPercent)
            : DISPLAY_STATE_BATTERY_UNKNOWN
    );

    if (pmuTimeoutCount > 0) {
        pmuTimeoutCount = 0;
        pmuNextPollAllowedMs = 0;
    }

#if ENABLE_PMU_UI_LOG
    Serial.printf(
        "PMU UI: BAT=%d VBUS=%d SYS=%.2fV TEMP=%.1f\n",
        latestPmuData.batteryPercent,
        latestPmuData.vbusIn ? 1 : 0,
        latestPmuData.systemVoltage,
        latestPmuData.temperature
    );
#endif

    applyPmuToUi();
}

uint8_t bcdToDec(uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0F);
}

bool isRtcFieldsValid(
    int year,
    int month,
    int day,
    int hour,
    int minute,
    int second,
    bool clockIntegrityGuaranteed
)
{
    return clockIntegrityGuaranteed &&
        year >= 2024 &&
        year <= 2099 &&
        month >= 1 &&
        month <= 12 &&
        day >= 1 &&
        day <= 31 &&
        hour >= 0 &&
        hour <= 23 &&
        minute >= 0 &&
        minute <= 59 &&
        second >= 0 &&
        second <= 59;
}

void setRtcDiagStage(const char* stage, uint8_t reg, uint8_t len)
{
    rtcDiagStage = stage;
    rtcDiagRegister = reg;
    rtcDiagLength = len;
}

void updateRtcI2cResult(esp_err_t result)
{
    rtcDiagLastI2cResult = result;

    if (result == ESP_OK) {
        return;
    }

    if (rtcPollTransactionActive && result == ESP_ERR_TIMEOUT) {
        rtcTimeoutCount++;
        if (rtcTimeoutCount >= kRtcTimeoutBackoffThreshold) {
            rtcNextPollAllowedMs = millis() + kRtcTimeoutBackoffMs;
        }
    }

    const uint32_t now = millis();
    if (lastRtcErrorLogMs == 0 || (now - lastRtcErrorLogMs) >= kGpsDiagnosticsIntervalMs) {
        lastRtcErrorLogMs = now;
#if ENABLE_RTC_DIAG_LOG
        Serial.printf(
            "RTC_I2C_ERROR: RTC_STAGE=%s RTC_REG=0x%02X RTC_LEN=%u RTC_I2C_RESULT=%d RTC_ERR_NAME=%s RTC_TIMEOUT_COUNT=%lu RTC_POLL_INTERVAL_MS=%lu ACTION=%s\n",
            rtcDiagStage,
            rtcDiagRegister,
            rtcDiagLength,
            static_cast<int>(result),
            i2cErrName(result),
            static_cast<unsigned long>(rtcTimeoutCount),
            static_cast<unsigned long>(kRtcPollIntervalMs),
            (rtcPollTransactionActive && rtcTimeoutCount >= kRtcTimeoutBackoffThreshold) ? "backoff_rtc_poll" : "fallback_mock_time"
        );
#endif
    }
}

esp_err_t rtcReadRegisters(uint8_t startRegister, uint8_t* data, uint8_t length, const char* stage)
{
    setRtcDiagStage(stage, startRegister, length);
    const uint8_t reg = startRegister;
    const esp_err_t result = sharedI2cWriteRead(kRtcI2cAddress, &reg, 1, data, length, rtcPollTransactionActive ? kRtcI2cTimeoutMs : 1000);
    updateRtcI2cResult(result);
    return result;
}

void setRtcFallbackTime()
{
    latestRtcData.valid = false;
    latestRtcData.hour = 10;
    latestRtcData.minute = 18;
    latestRtcData.second = 0;
    latestRtcData.day = 26;
    latestRtcData.month = 6;
    latestRtcData.year = 2026;
    rtcFallbackUsed = true;
}

void initRtc()
{
    rtcDiagReadOk = false;
    rtcDiagLastI2cResult = ESP_OK;
    rtcTimeoutCount = 0;
    rtcNextPollAllowedMs = 0;
    rtcPollTransactionActive = false;
    setRtcDiagStage("init_start", 0, 0);
    latestRtcData.ready = false;
    setRtcFallbackTime();

    if (!initSharedI2cBusOnce()) {
        updateRtcI2cResult(ESP_ERR_INVALID_STATE);
#if ENABLE_RTC_DIAG_LOG
        Serial.printf(
            "RTC_DIAG: INIT=%d INIT_OK=0 POLL=%d READ_OK=0 RTC_BACKEND=ARDUINO_WIRE_NG RTC_SHARED_I2C=1 RTC_INSTALL_DRIVER=0 RTC_DIRECT_ESP_IDF=0 RTC_I2C_RESULT=%d RTC_ERR_NAME=%s RTC_TIMEOUT_COUNT=%lu RTC_STAGE=%s RTC_REG=0x%02X RTC_LEN=%u RTC_POLL_INTERVAL_MS=%lu YEAR=%d MONTH=%d DAY=%d HOUR=%d MINUTE=%d SECOND=%d VALID=0 FALLBACK_USED=1 SHARED_I2C_READY=%d RTC_SHARED_I2C_WITH_GPS=%d\n",
            DIAG_INIT_RTC,
            DIAG_POLL_RTC,
            static_cast<int>(rtcDiagLastI2cResult),
            i2cErrName(rtcDiagLastI2cResult),
            static_cast<unsigned long>(rtcTimeoutCount),
            rtcDiagStage,
            rtcDiagRegister,
            rtcDiagLength,
            static_cast<unsigned long>(kRtcPollIntervalMs),
            latestRtcData.year,
            latestRtcData.month,
            latestRtcData.day,
            latestRtcData.hour,
            latestRtcData.minute,
            latestRtcData.second,
            sharedI2cReady ? 1 : 0,
            (IIC_SDA == kLc76gI2cSda && IIC_SCL == kLc76gI2cScl) ? 1 : 0
        );
#endif
        return;
    }

    uint8_t control1 = 0;
    if (rtcReadRegisters(kRtcControl1Register, &control1, 1, "init_probe") != ESP_OK) {
#if ENABLE_RTC_UI_LOG
        Serial.println("RTC UI: PCF85063 probe failed; using mock time.");
#endif
#if ENABLE_RTC_DIAG_LOG
        Serial.printf(
            "RTC_DIAG: INIT=%d INIT_OK=0 POLL=%d READ_OK=0 RTC_BACKEND=ARDUINO_WIRE_NG RTC_SHARED_I2C=1 RTC_INSTALL_DRIVER=0 RTC_DIRECT_ESP_IDF=0 RTC_I2C_RESULT=%d RTC_ERR_NAME=%s RTC_TIMEOUT_COUNT=%lu RTC_STAGE=%s RTC_REG=0x%02X RTC_LEN=%u RTC_POLL_INTERVAL_MS=%lu YEAR=%d MONTH=%d DAY=%d HOUR=%d MINUTE=%d SECOND=%d VALID=0 FALLBACK_USED=1 SHARED_I2C_READY=%d RTC_SHARED_I2C_WITH_GPS=%d\n",
            DIAG_INIT_RTC,
            DIAG_POLL_RTC,
            static_cast<int>(rtcDiagLastI2cResult),
            i2cErrName(rtcDiagLastI2cResult),
            static_cast<unsigned long>(rtcTimeoutCount),
            rtcDiagStage,
            rtcDiagRegister,
            rtcDiagLength,
            static_cast<unsigned long>(kRtcPollIntervalMs),
            latestRtcData.year,
            latestRtcData.month,
            latestRtcData.day,
            latestRtcData.hour,
            latestRtcData.minute,
            latestRtcData.second,
            sharedI2cReady ? 1 : 0,
            (IIC_SDA == kLc76gI2cSda && IIC_SCL == kLc76gI2cScl) ? 1 : 0
        );
#endif
        return;
    }

    latestRtcData.ready = true;
    lastRtcPollMs = millis() - kRtcPollIntervalMs;
#if ENABLE_RTC_UI_LOG
    Serial.println("RTC UI: PCF85063 shared I2C ready.");
#endif
#if ENABLE_RTC_DIAG_LOG
    Serial.printf(
        "RTC_DIAG: INIT=%d INIT_OK=1 POLL=%d READ_OK=0 RTC_BACKEND=ARDUINO_WIRE_NG RTC_SHARED_I2C=1 RTC_INSTALL_DRIVER=0 RTC_DIRECT_ESP_IDF=0 RTC_I2C_RESULT=%d RTC_ERR_NAME=%s RTC_TIMEOUT_COUNT=%lu RTC_STAGE=%s RTC_REG=0x%02X RTC_LEN=%u RTC_POLL_INTERVAL_MS=%lu YEAR=%d MONTH=%d DAY=%d HOUR=%d MINUTE=%d SECOND=%d VALID=0 FALLBACK_USED=1 SHARED_I2C_READY=%d RTC_SHARED_I2C_WITH_GPS=%d\n",
        DIAG_INIT_RTC,
        DIAG_POLL_RTC,
        static_cast<int>(rtcDiagLastI2cResult),
        i2cErrName(rtcDiagLastI2cResult),
        static_cast<unsigned long>(rtcTimeoutCount),
        rtcDiagStage,
        rtcDiagRegister,
        rtcDiagLength,
        static_cast<unsigned long>(kRtcPollIntervalMs),
        latestRtcData.year,
        latestRtcData.month,
        latestRtcData.day,
        latestRtcData.hour,
        latestRtcData.minute,
        latestRtcData.second,
        sharedI2cReady ? 1 : 0,
        (IIC_SDA == kLc76gI2cSda && IIC_SCL == kLc76gI2cScl) ? 1 : 0
    );
#endif
}

void applyRtcToUi()
{
    if (currentRenderedPage == UI_SHOW_PAGE_HOME) {
        ui_home_status_update(createHomeStatusUiState());
    } else if (currentRenderedPage == UI_SHOW_PAGE_SETTINGS) {
        ui_settings_update(createHomeStatusUiState());
    }
}

void updateRtc()
{
    if (!latestRtcData.ready) {
        return;
    }

    const uint32_t now = millis();
    if ((now - lastRtcPollMs) < kRtcPollIntervalMs) {
        return;
    }
    if (now < rtcNextPollAllowedMs) {
        return;
    }
    lastRtcPollMs = now;

    uint8_t buffer[kRtcTimeRegisterCount] = {};
    rtcDiagReadOk = false;
    rtcDiagLastI2cResult = ESP_OK;
    rtcPollTransactionActive = true;
    const esp_err_t result = rtcReadRegisters(kRtcSecondsRegister, buffer, kRtcTimeRegisterCount, "poll_read_time");
    rtcPollTransactionActive = false;
    if (result != ESP_OK) {
        setRtcFallbackTime();
        return;
    }

    const bool wasValid = latestRtcData.valid;
    const bool clockIntegrityGuaranteed = (buffer[0] & 0x80) == 0;
    const int second = bcdToDec(buffer[0] & 0x7F);
    const int minute = bcdToDec(buffer[1] & 0x7F);
    const int hour = bcdToDec(buffer[2] & 0x3F);
    const int day = bcdToDec(buffer[3] & 0x3F);
    const int month = bcdToDec(buffer[5] & 0x1F);
    const int year = 2000 + bcdToDec(buffer[6]);
    const bool valid = isRtcFieldsValid(year, month, day, hour, minute, second, clockIntegrityGuaranteed);
    rtcDiagReadOk = valid;
    latestRtcData.valid = valid;
    rtcFallbackUsed = !valid;

    if (!valid) {
#if ENABLE_RTC_UI_LOG
        Serial.println("RTC UI: invalid time using mock time");
#endif
        setRtcFallbackTime();
        if (wasValid) {
            applyRtcToUi();
        }
        return;
    }

    const bool timeChanged = (latestRtcData.hour != hour) || (latestRtcData.minute != minute);

    latestRtcData.hour = hour;
    latestRtcData.minute = minute;
    latestRtcData.second = second;
    latestRtcData.day = day;
    latestRtcData.month = month;
    latestRtcData.year = year;
    rtcFallbackUsed = false;
    if (rtcTimeoutCount > 0) {
        rtcTimeoutCount = 0;
        rtcNextPollAllowedMs = 0;
    }

#if ENABLE_RTC_UI_LOG
    Serial.printf("RTC UI: OK TIME=%02d:%02d:%02d\n", latestRtcData.hour, latestRtcData.minute, latestRtcData.second);
#endif

    if (timeChanged || !wasValid) {
        applyRtcToUi();
    }
}

esp_err_t gpsI2cWrite(uint8_t deviceAddress, const uint8_t* data, size_t dataLength)
{
    return sharedI2cWrite(deviceAddress, data, dataLength);
}

esp_err_t gpsI2cRead(uint8_t deviceAddress, uint8_t* data, size_t dataLength)
{
    return sharedI2cRead(deviceAddress, data, dataLength);
}

esp_err_t gpsI2cReadNmeaBlockOnce(uint8_t deviceAddress, uint8_t* data, size_t dataLength)
{
    if (!sharedI2cReady) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == nullptr || dataLength == 0 || dataLength > kGpsMaxDynamicReadLength) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t readCount = 0;
    gpsTransportLastNmeaLength = dataLength;
    gpsTransportTransactionStartMs = millis();
    const esp_err_t result = i2cRead(
        Wire.getBusNum(),
        deviceAddress,
        data,
        dataLength,
        1000,
        &readCount
    );
    gpsTransportTransactionEndMs = millis();

    if (result == ESP_OK && readCount == dataLength) {
        gpsTransportSuccessCount++;
        return ESP_OK;
    }
    if (result == ESP_ERR_INVALID_STATE) {
        gpsTransportNackCount++;
    }
    if (result == ESP_OK) {
        return ESP_ERR_INVALID_SIZE;
    }
    return result;
}

bool initGpsI2cBackend()
{
    gpsI2cParamConfigResult = sharedI2cParamConfigResult;
    gpsI2cDriverInstallResult = sharedI2cDriverInstallResult;
    gpsI2cDriverAlreadyInstalled = sharedI2cDriverAlreadyInstalled;
    gpsI2cBackendReusedExistingDriver = sharedI2cReady;

    if (!sharedI2cReady && !initSharedI2cBusOnce()) {
        gpsI2cParamConfigResult = sharedI2cParamConfigResult;
        gpsI2cDriverInstallResult = sharedI2cDriverInstallResult;
        gpsI2cDriverAlreadyInstalled = sharedI2cDriverAlreadyInstalled;
        gpsI2cBackendReusedExistingDriver = false;
        gpsReadyReason = "shared_i2c_not_ready";
#if ENABLE_GPS_SUMMARY_LOG
        Serial.printf(
            "GPS_I2C_INIT: GPS_READY=0 GPS_READY_REASON=%s SHARED_I2C_READY=%d PARAM_CONFIG=%d DRIVER_INSTALL=%d DRIVER_ALREADY_INSTALLED=%d MUTEX_CREATED=%d SKIPPED_GPS_DRIVER_INSTALL=1\n",
            gpsReadyReason,
            sharedI2cReady ? 1 : 0,
            static_cast<int>(sharedI2cParamConfigResult),
            static_cast<int>(sharedI2cDriverInstallResult),
            sharedI2cDriverAlreadyInstalled ? 1 : 0,
            sharedI2cMutexCreated ? 1 : 0
        );
#endif
        return false;
    }

    gpsI2cParamConfigResult = sharedI2cParamConfigResult;
    gpsI2cDriverInstallResult = sharedI2cDriverInstallResult;
    gpsI2cDriverAlreadyInstalled = sharedI2cDriverAlreadyInstalled;
    gpsI2cBackendReusedExistingDriver = true;
    gpsReadyReason = sharedI2cDriverAlreadyInstalled ? "shared_driver_already_installed" : "shared_bus_ready";
#if ENABLE_GPS_SUMMARY_LOG
    Serial.printf(
        "GPS_I2C_INIT: GPS_READY=1 GPS_READY_REASON=%s SHARED_I2C_READY=%d GPS_REUSE_SHARED_I2C=1 SKIPPED_GPS_DRIVER_INSTALL=1 PARAM_CONFIG=%d DRIVER_INSTALL=%d DRIVER_ALREADY_INSTALLED=%d MUTEX_CREATED=%d TOUCH_INIT=%d\n",
        gpsReadyReason,
        sharedI2cReady ? 1 : 0,
        static_cast<int>(sharedI2cParamConfigResult),
        static_cast<int>(sharedI2cDriverInstallResult),
        sharedI2cDriverAlreadyInstalled ? 1 : 0,
        sharedI2cMutexCreated ? 1 : 0,
        touchReady ? 1 : 0
    );
#endif
    return true;
}

uint32_t parseLc76gLength()
{
    return gpsI2cParseLittleEndianLength(gpsLengthBytes);
}

void printGpsLengthRaw(uint32_t parsedLength)
{
#if ENABLE_GPS_VERBOSE_LOG
    Serial.printf(
        "GPS_LEN_RAW: b0=0x%02X b1=0x%02X b2=0x%02X b3=0x%02X parsed_len=%lu test_compatible_len=%lu\n",
        gpsLengthBytes[0],
        gpsLengthBytes[1],
        gpsLengthBytes[2],
        gpsLengthBytes[3],
        static_cast<unsigned long>(parsedLength),
        static_cast<unsigned long>(parsedLength)
    );
#else
    (void)parsedLength;
#endif
}

void discardGpsBlockLength(const char* reason, uint32_t parsedLength)
{
    gpsDiagnosticsReadFailures++;
    gpsDiagnosticsLastBlockLength = parsedLength;
#if ENABLE_GPS_VERBOSE_LOG
    if (strcmp(reason, "block_too_large") == 0) {
        Serial.printf(
            "GPS_UI: LC76G NMEA block too large: raw bytes=0x%02X 0x%02X 0x%02X 0x%02X parsed_len=%lu max_allowed=%lu action=discard_and_retry\n",
            gpsLengthBytes[0],
            gpsLengthBytes[1],
            gpsLengthBytes[2],
            gpsLengthBytes[3],
            static_cast<unsigned long>(parsedLength),
            static_cast<unsigned long>(kGpsMaxNmeaBlockLength)
        );
    } else {
        Serial.printf(
            "GPS_UI: LC76G NMEA block invalid: reason=%s raw bytes=0x%02X 0x%02X 0x%02X 0x%02X parsed_len=%lu max_allowed=%lu action=discard_and_retry\n",
            reason,
            gpsLengthBytes[0],
            gpsLengthBytes[1],
            gpsLengthBytes[2],
            gpsLengthBytes[3],
            static_cast<unsigned long>(parsedLength),
            static_cast<unsigned long>(kGpsMaxNmeaBlockLength)
        );
    }
#else
    (void)reason;
#endif
    gpsDataLength = 0;
    memset(gpsLengthBytes, 0, sizeof(gpsLengthBytes));
}

void warnLargeGpsBlockAllowed(uint32_t parsedLength)
{
#if ENABLE_GPS_VERBOSE_LOG
    Serial.printf(
        "GPS_UI: LC76G NMEA block large but allowed: parsed_len=%lu buffer_size=%lu action=read_and_parse\n",
        static_cast<unsigned long>(parsedLength),
        static_cast<unsigned long>(kGpsMaxNmeaBlockLength)
    );
#else
    (void)parsedLength;
#endif
}

bool shouldPrintGpsBacklogLog()
{
    const uint32_t now = millis();
    if (lastGpsBacklogLogMs != 0 && (now - lastGpsBacklogLogMs) < kGpsDiagnosticsIntervalMs) {
        return false;
    }
    lastGpsBacklogLogMs = now;
    return true;
}

bool sendLc76gNmeaReadRequest()
{
    uint8_t data2[] = { 0x00, 0x20, 0x51, 0xAA };
    uint8_t dataToSend[sizeof(data2) + sizeof(gpsLengthBytes)];
    memcpy(dataToSend, data2, sizeof(data2));
    memcpy(dataToSend + sizeof(data2), gpsLengthBytes, sizeof(gpsLengthBytes));
    delay(kGpsI2cDelayMs);

    gpsDiagnosticsNmeaWriteResult = gpsI2cWrite(kLc76gDeviceAddress, dataToSend, sizeof(dataToSend));
    if (gpsDiagnosticsNmeaWriteResult != ESP_OK) {
        gpsDiagnosticsReadFailures++;
        return false;
    }

    return true;
}

void feedGpsNmeaBlock(uint8_t* nmeaBytes, uint32_t dataLength)
{
    gpsDiagnosticsBytesThisSecond += dataLength;
    gpsDiagnosticsSawBytesThisSecond = dataLength > 0;
    for (uint32_t i = 0; i < dataLength; i++) {
        if (gpsParser.feed(static_cast<char>(nmeaBytes[i]))) {
            gpsDiagnosticsSentencesThisSecond++;
#if GPS_DEBUG_RAW_NMEA && ENABLE_GPS_VERBOSE_LOG
            Serial.print("GPS NMEA: ");
            Serial.println(gpsParser.lastSentence());
#endif
        }
    }
}

bool readAndParseGpsBlock(uint32_t parsedLength, bool isLargeBlock)
{
    uint8_t* dynamicReadData = static_cast<uint8_t*>(malloc(parsedLength + 1));
    if (dynamicReadData == nullptr) {
        gpsDiagnosticsReadFailures++;
        if (isLargeBlock) {
            gpsDiagnosticsLargeFailuresThisSecond++;
        }
#if ENABLE_GPS_VERBOSE_LOG
        Serial.printf(
            "GPS_UI: LC76G NMEA buffer allocation failed bytes=%lu\n",
            static_cast<unsigned long>(parsedLength)
        );
#endif
        return false;
    }

    if (!sendLc76gNmeaReadRequest()) {
        if (isLargeBlock) {
            gpsDiagnosticsLargeFailuresThisSecond++;
        }
        free(dynamicReadData);
        return false;
    }

    delay(kGpsI2cDelayMs);

    gpsDiagnosticsNmeaReadResult = gpsI2cReadNmeaBlockOnce(
        kLc76gDeviceReadAddress,
        dynamicReadData,
        parsedLength
    );
    if (gpsDiagnosticsNmeaReadResult != ESP_OK) {
        gpsDiagnosticsReadFailures++;
        if (isLargeBlock) {
            gpsDiagnosticsLargeFailuresThisSecond++;
#if ENABLE_GPS_VERBOSE_LOG
            Serial.printf(
                "GPS_UI: LC76G large block read failed: bytes=%lu result=%d action=retry\n",
                static_cast<unsigned long>(parsedLength),
                static_cast<int>(gpsDiagnosticsNmeaReadResult)
            );
#endif
        } else {
#if ENABLE_GPS_VERBOSE_LOG
            Serial.printf("GPS_UI: LC76G NMEA read failed: %d\n", static_cast<int>(gpsDiagnosticsNmeaReadResult));
#endif
        }
        free(dynamicReadData);
        return false;
    }

    dynamicReadData[parsedLength] = '\0';
    if (isLargeBlock) {
        gpsDiagnosticsLargeBytesThisSecond += parsedLength;
        gpsDiagnosticsLargeEventsThisSecond++;
#if ENABLE_GPS_VERBOSE_LOG
        Serial.printf(
            "GPS_UI: LC76G large block read ok: bytes=%lu prefix=%c%c action=parse_and_clear_backlog\n",
            static_cast<unsigned long>(parsedLength),
            parsedLength > 0 ? static_cast<char>(dynamicReadData[0]) : '-',
            parsedLength > 1 ? static_cast<char>(dynamicReadData[1]) : '-'
        );
#endif
    }

    feedGpsNmeaBlock(dynamicReadData, parsedLength);
    free(dynamicReadData);
    applyGpsToUi();
    return true;
}

void initGps()
{
    gpsDataReset(&latestGpsData);
    gpsReady = initGpsI2cBackend();
    lastGpsPollMs = millis();
    lastGpsDiagnosticsMs = millis();
#if ENABLE_GPS_SUMMARY_LOG
    Serial.printf(
        "GPS_UI: GPS_IMPL=GPS_DATA_TEST_COMPAT I2C_BACKEND=ARDUINO_WIRE_NG INIT=%d READY=%d GPS_READY_REASON=%s SHARED_I2C_READY=%d GPS_REUSE_SHARED_I2C=%d SKIPPED_GPS_DRIVER_INSTALL=1 SDA=%d SCL=%d FREQ=%lu I2C_PORT=%d DEVICE_ADDRESS_W=0x%02X DEVICE_ADDRESS_R=0x%02X UART=NONE RX=N/A TX=N/A BAUD=N/A RAW_NMEA=%d TOUCH_INIT=%d PMU_INIT=%d RTC_INIT=%d PARAM_CONFIG=%d DRIVER_INSTALL=%d DRIVER_ALREADY_INSTALLED=%d MUTEX_CREATED=%d\n",
        gpsReady ? 1 : 0,
        gpsReady ? 1 : 0,
        gpsReadyReason,
        sharedI2cReady ? 1 : 0,
        gpsI2cBackendReusedExistingDriver ? 1 : 0,
        kLc76gI2cSda,
        kLc76gI2cScl,
        static_cast<unsigned long>(kLc76gI2cFreqHz),
        static_cast<int>(Wire.getBusNum()),
        kLc76gDeviceAddress,
        kLc76gDeviceReadAddress,
        GPS_DEBUG_RAW_NMEA,
        DIAG_INIT_TOUCH,
        DIAG_INIT_PMU,
        DIAG_INIT_RTC,
        static_cast<int>(gpsI2cParamConfigResult),
        static_cast<int>(gpsI2cDriverInstallResult),
        gpsI2cDriverAlreadyInstalled ? 1 : 0,
        sharedI2cMutexCreated ? 1 : 0
    );
#endif

    if (!gpsReady) {
#if ENABLE_GPS_SUMMARY_LOG
        Serial.printf(
            "GPS_UI: Arduino Wire NG I2C backend init failed; GPS_READY=0 GPS_READY_REASON=%s SHARED_I2C_READY=%d PARAM_CONFIG=%d DRIVER_INSTALL=%d DRIVER_ALREADY_INSTALLED=%d MUTEX_CREATED=%d SKIPPED_GPS_DRIVER_INSTALL=1\n",
            gpsReadyReason,
            sharedI2cReady ? 1 : 0,
            static_cast<int>(gpsI2cParamConfigResult),
            static_cast<int>(gpsI2cDriverInstallResult),
            gpsI2cDriverAlreadyInstalled ? 1 : 0,
            sharedI2cMutexCreated ? 1 : 0
        );
#endif
    }
}

bool initReadLength()
{
    uint8_t requestLength[] = { 0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00 };
    gpsDataLength = 0;
    memset(gpsLengthBytes, 0, sizeof(gpsLengthBytes));

    gpsDiagnosticsLengthWriteResult = gpsI2cWrite(kLc76gDeviceAddress, requestLength, sizeof(requestLength));
    if (gpsDiagnosticsLengthWriteResult != ESP_OK) {
        gpsDiagnosticsReadFailures++;
        gpsDiagnosticsLastBlockLength = 0;
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(kGpsI2cDelayMs));

    gpsDiagnosticsLengthReadResult = gpsI2cRead(kLc76gDeviceReadAddress, gpsLengthBytes, sizeof(gpsLengthBytes));
    if (gpsDiagnosticsLengthReadResult != ESP_OK) {
        gpsDiagnosticsReadFailures++;
        gpsDiagnosticsLastBlockLength = 0;
        memset(gpsLengthBytes, 0, sizeof(gpsLengthBytes));
        return false;
    }

    return true;
}

bool readLc76gNmeaBlock()
{
    gpsDiagnosticsLengthWriteResult = ESP_OK;
    gpsDiagnosticsLengthReadResult = ESP_OK;
    gpsDiagnosticsNmeaWriteResult = ESP_OK;
    gpsDiagnosticsNmeaReadResult = ESP_OK;

    if (!initReadLength()) {
        return false;
    }

    gpsDataLength = parseLc76gLength();
    gpsDiagnosticsLastBlockLength = gpsDataLength;
    printGpsLengthRaw(gpsDataLength);

    if (gpsDataLength == 0) {
        discardGpsBlockLength("zero_length", gpsDataLength);
        return false;
    }

    if (gpsDataLength > kGpsMaxDynamicReadLength) {
        gpsDiagnosticsReadFailures++;
        gpsDiagnosticsLargeFailuresThisSecond++;
        if (shouldPrintGpsBacklogLog()) {
#if ENABLE_GPS_VERBOSE_LOG
            Serial.printf(
                "GPS_UI: LC76G block exceeds dynamic max: parsed_len=%lu dynamic_max=%lu action=retry\n",
                static_cast<unsigned long>(gpsDataLength),
                static_cast<unsigned long>(kGpsMaxDynamicReadLength)
            );
#endif
        }
        gpsDataLength = 0;
        memset(gpsLengthBytes, 0, sizeof(gpsLengthBytes));
        return false;
    }

    if (gpsDataLength > kGpsMaxNmeaBlockLength) {
#if ENABLE_GPS_VERBOSE_LOG
        Serial.printf(
            "GPS_UI: LC76G large block dynamic read: parsed_len=%lu dynamic_max=%lu action=read_and_parse\n",
            static_cast<unsigned long>(gpsDataLength),
            static_cast<unsigned long>(kGpsMaxDynamicReadLength)
        );
#endif
        return readAndParseGpsBlock(gpsDataLength, true);
    }

    if (gpsDataLength > kGpsLargeNmeaBlockWarningLength) {
        warnLargeGpsBlockAllowed(gpsDataLength);
    }

    return readAndParseGpsBlock(gpsDataLength, false);
}

void applyGpsToUi()
{
    latestGpsData = gpsParser.data();
    if (!latestGpsData.hasFix) {
        latestGpsData.speedKmh = 0.0f;
    } else if (isfinite(latestGpsData.speedKmh)) {
        gpsValidSpeedSamplePending = true;
    }

    if (currentRenderedPage == UI_SHOW_PAGE_HOME) {
        ui_home_status_update(createHomeStatusUiState());
    } else if (currentRenderedPage == UI_SHOW_PAGE_SETTINGS) {
        ui_settings_update(createHomeStatusUiState());
    } else if (currentRenderedPage == UI_SHOW_PAGE_WORKOUT) {
        renderPage(UI_SHOW_PAGE_WORKOUT, true);
    }
}

void updateGpsDiagnostics()
{
    const uint32_t now = millis();
    if ((now - lastGpsDiagnosticsMs) < kGpsDiagnosticsIntervalMs) {
        return;
    }
    lastGpsDiagnosticsMs = now;
    gpsSummaryBytesLastWindow = gpsDiagnosticsBytesThisSecond;

#if ENABLE_GPS_SUMMARY_LOG
    Serial.printf(
        "GPS_UI: GPS_IMPL=GPS_DATA_TEST_COMPAT I2C_BACKEND=ARDUINO_WIRE_NG INIT=%d READY=%d GPS_READY_REASON=%s SHARED_I2C_READY=%d GPS_REUSE_SHARED_I2C=%d SKIPPED_GPS_DRIVER_INSTALL=1 UART=NONE SDA=%d SCL=%d FREQ=%lu DEVICE_ADDRESS_W=0x%02X DEVICE_ADDRESS_R=0x%02X STEP=BLOCK_READ LEN_WR=%d LEN_RD=%d NMEA_WR=%d NMEA_RD=%d LAST_LEN=%lu BYTES_1S=%lu ANY_1S=%d SENT_1S=%lu LARGE_BYTES_1S=%lu LARGE_EVENTS_1S=%lu LARGE_FAILS_1S=%lu FAILS=%lu PARAM_CONFIG=%d DRIVER_INSTALL=%d DRIVER_ALREADY_INSTALLED=%d MUTEX_CREATED=%d FIX=%d SAT=%d SPEED=%.1f LAT=%.6f LON=%.6f\n",
        DIAG_ENABLE_GPS,
        gpsReady ? 1 : 0,
        gpsReadyReason,
        sharedI2cReady ? 1 : 0,
        gpsI2cBackendReusedExistingDriver ? 1 : 0,
        kLc76gI2cSda,
        kLc76gI2cScl,
        static_cast<unsigned long>(kLc76gI2cFreqHz),
        kLc76gDeviceAddress,
        kLc76gDeviceReadAddress,
        static_cast<int>(gpsDiagnosticsLengthWriteResult),
        static_cast<int>(gpsDiagnosticsLengthReadResult),
        static_cast<int>(gpsDiagnosticsNmeaWriteResult),
        static_cast<int>(gpsDiagnosticsNmeaReadResult),
        static_cast<unsigned long>(gpsDiagnosticsLastBlockLength),
        static_cast<unsigned long>(gpsDiagnosticsBytesThisSecond),
        gpsDiagnosticsSawBytesThisSecond ? 1 : 0,
        static_cast<unsigned long>(gpsDiagnosticsSentencesThisSecond),
        static_cast<unsigned long>(gpsDiagnosticsLargeBytesThisSecond),
        static_cast<unsigned long>(gpsDiagnosticsLargeEventsThisSecond),
        static_cast<unsigned long>(gpsDiagnosticsLargeFailuresThisSecond),
        static_cast<unsigned long>(gpsDiagnosticsReadFailures),
        static_cast<int>(gpsI2cParamConfigResult),
        static_cast<int>(gpsI2cDriverInstallResult),
        gpsI2cDriverAlreadyInstalled ? 1 : 0,
        sharedI2cMutexCreated ? 1 : 0,
        latestGpsData.hasFix ? 1 : 0,
        latestGpsData.satellites,
        latestGpsData.speedKmh,
        latestGpsData.latitude,
        latestGpsData.longitude
    );
#endif

    gpsDiagnosticsBytesThisSecond = 0;
    gpsDiagnosticsSentencesThisSecond = 0;
    gpsDiagnosticsLargeBytesThisSecond = 0;
    gpsDiagnosticsLargeEventsThisSecond = 0;
    gpsDiagnosticsLargeFailuresThisSecond = 0;
    gpsDiagnosticsSawBytesThisSecond = false;

    if ((now - lastGpsTransportLogMs) >= kGpsTransportLogIntervalMs) {
        lastGpsTransportLogMs = now;
        ESP_LOGI(
            "GPS_TRANSPORT",
            "start_ms=%lu end_ms=%lu nmea_len=%lu success=%lu nack=%lu",
            static_cast<unsigned long>(gpsTransportTransactionStartMs),
            static_cast<unsigned long>(gpsTransportTransactionEndMs),
            static_cast<unsigned long>(gpsTransportLastNmeaLength),
            static_cast<unsigned long>(gpsTransportSuccessCount),
            static_cast<unsigned long>(gpsTransportNackCount)
        );
    }
}

void updateGps()
{
    if (!gpsReady) {
        return;
    }

    const uint32_t now = millis();
    if ((now - lastGpsPollMs) >= kGpsPollIntervalMs) {
        readLc76gNmeaBlock();
        lastGpsPollMs = millis();
    }
}

double degreesToRadians(double degrees)
{
    return degrees * kPi / 180.0;
}

bool isValidGpsPoint(const GpsData& gpsData)
{
    return gpsData.hasFix &&
        gpsData.latitude >= -90.0 &&
        gpsData.latitude <= 90.0 &&
        gpsData.longitude >= -180.0 &&
        gpsData.longitude <= 180.0;
}

float haversineDistanceM(double lat1, double lon1, double lat2, double lon2)
{
    const double dLat = degreesToRadians(lat2 - lat1);
    const double dLon = degreesToRadians(lon2 - lon1);
    const double rLat1 = degreesToRadians(lat1);
    const double rLat2 = degreesToRadians(lat2);

    const double sinHalfLat = sin(dLat / 2.0);
    const double sinHalfLon = sin(dLon / 2.0);
    double a =
        (sinHalfLat * sinHalfLat) +
        (cos(rLat1) * cos(rLat2) * sinHalfLon * sinHalfLon);
    if (a < 0.0) {
        a = 0.0;
    } else if (a > 1.0) {
        a = 1.0;
    }
    const double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    const double distanceM = kEarthRadiusM * c;

    if (distanceM < 0.0 || distanceM > 1000000.0) {
        return 0.0f;
    }
    return static_cast<float>(distanceM);
}

void updateWorkoutDistance(const GpsData& gpsData)
{
    if (!workoutRunning) {
        return;
    }

    if (!isValidGpsPoint(gpsData)) {
        workoutHasLastPoint = false;
        return;
    }

    if (!workoutHasLastPoint) {
        workoutLastLatitude = gpsData.latitude;
        workoutLastLongitude = gpsData.longitude;
        workoutHasLastPoint = true;
        return;
    }

    const float distanceM = haversineDistanceM(
        workoutLastLatitude,
        workoutLastLongitude,
        gpsData.latitude,
        gpsData.longitude
    );

    workoutLastLatitude = gpsData.latitude;
    workoutLastLongitude = gpsData.longitude;

    if (gpsData.speedKmh < kMinGpsSpeedKmhForDistance) {
        return;
    }
    if (distanceM < kMinGpsDistanceM || distanceM > kMaxGpsDistanceM) {
        return;
    }

    workoutDistanceKm += distanceM / 1000.0f;
}

void updateWorkoutRuntime()
{
    const uint32_t now = millis();
    if (workoutLastTickMs == 0) {
        workoutLastTickMs = now;
    }

    const uint32_t deltaMs = now - workoutLastTickMs;
    workoutLastTickMs = now;

    if (workoutRunning) {
        workoutImuSpeedUpdate(now);
        const bool receivedValidGpsSpeed = gpsValidSpeedSamplePending;
        const WorkoutSpeedResult speedResult = workoutSpeedResolver.update(
            now,
            receivedValidGpsSpeed,
            latestGpsData.speedKmh,
            workoutImuSpeedGetKmh(),
            workoutImuReady
        );
        gpsValidSpeedSamplePending = false;
        if (speedResult.switchedToImu) {
            workoutImuSpeedSetBaseline(speedResult.imuInitialSpeedKmh, now);
            workoutHasLastPoint = false;
            Serial.printf("EVENT: workout_speed_source=IMU_EST initial=%.1f\n", speedResult.imuInitialSpeedKmh);
        } else if (speedResult.switchedToGps) {
            workoutImuSpeedSetBaseline(latestGpsData.speedKmh, now);
            workoutHasLastPoint = false;
            Serial.println("EVENT: workout_speed_source=GPS");
        }
        effectiveWorkoutSpeedKmh = speedResult.effectiveSpeedKmh;
        effectiveWorkoutSpeedSource = speedResult.source;
        workoutElapsedMs += deltaMs;
        workoutCaloriesUpdate(effectiveWorkoutSpeedKmh, deltaMs);
        if (effectiveWorkoutSpeedSource == WORKOUT_SPEED_IMU_EST) {
            workoutDistanceKm += (effectiveWorkoutSpeedKmh / 3.6f) * (static_cast<float>(deltaMs) / 1000.0f) / 1000.0f;
        } else if (receivedValidGpsSpeed) {
            updateWorkoutDistance(latestGpsData);
        }
#if ENABLE_IMU_SPEED_DEBUG_LOG
        if ((now - lastImuSpeedDebugMs) >= 1000) {
            lastImuSpeedDebugMs = now;
            Serial.printf("STATUS: speed_source=%s effective=%.1f imu=%.1f axis=%u\n",
                effectiveWorkoutSpeedSource == WORKOUT_SPEED_IMU_EST ? "IMU_EST" : "GPS",
                effectiveWorkoutSpeedKmh, workoutImuSpeedGetKmh(), static_cast<unsigned>(workoutImuSpeedGetAxis()));
        }
#endif
    } else {
        gpsValidSpeedSamplePending = false;
        effectiveWorkoutSpeedKmh = 0.0f;
    }

    const uint32_t elapsedSecond = workoutElapsedMs / 1000;
    if (currentRenderedPage == UI_SHOW_PAGE_WORKOUT && elapsedSecond != workoutLastRenderedSecond) {
        workoutLastRenderedSecond = elapsedSecond;
        renderPage(UI_SHOW_PAGE_WORKOUT, true);
    }

    displayStateUpdateWorkout(
        workoutElapsedMs / 1000,
        effectiveWorkoutSpeedKmh,
        workoutCaloriesGetEstimated(),
        workoutDistanceKm * 1000.0f
    );
}

UiState createCurrentUiState()
{
    UiState state = uiCreateMockState();
    state.gpsFixed = latestGpsData.hasFix;
    state.satellites = latestGpsData.satellites;
    state.speedKmh = effectiveWorkoutSpeedKmh;
    state.speedSource = !workoutRunning ? SPEED_SOURCE_NONE :
        (effectiveWorkoutSpeedSource == WORKOUT_SPEED_IMU_EST ? SPEED_SOURCE_IMU_EST : SPEED_SOURCE_GPS);
    state.distanceKm = workoutDistanceKm;
    state.elapsedSeconds = workoutElapsedMs / 1000;
    state.elapsedMinutes = state.elapsedSeconds / 60;
    state.recording = workoutRunning;

    if (latestPmuData.ready) {
        state.pmuReady = true;
        state.batteryPercent = latestPmuData.batteryPercent;
        state.batteryVoltage = latestPmuData.batteryVoltage;
        state.systemVoltage = latestPmuData.systemVoltage;
        state.pmuTemperature = latestPmuData.temperature;
        state.charging = latestPmuData.charging;
        state.vbusIn = latestPmuData.vbusIn;
        state.vbusGood = latestPmuData.vbusGood;
    }

    state.hour = latestRtcData.hour;
    state.minute = latestRtcData.minute;

    static const char* const monthNames[] = {
        "---", "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };
    static char homeDateText[12] = "--- --";
    if (latestRtcData.month >= 1 && latestRtcData.month <= 12 &&
        latestRtcData.day >= 1 && latestRtcData.day <= 31) {
        snprintf(
            homeDateText,
            sizeof(homeDateText),
            "%s %02d",
            monthNames[latestRtcData.month],
            latestRtcData.day
        );
    } else {
        snprintf(homeDateText, sizeof(homeDateText), "--- --");
    }
    state.dateText = homeDateText;
    state.modeText = (latestRtcData.ready && latestRtcData.valid) ? "LOCAL TIME" : "RTC --";

    return state;
}

UiState createHomeStatusUiState()
{
    UiState state = createCurrentUiState();
#if ENABLE_DISPLAY_BLE_SERVER
    state.bleEnabled = displayBleServerReady;
    state.bleConnected = displayBleIsConnected();
#else
    state.bleEnabled = false;
    state.bleConnected = false;
#endif
    return state;
}

void renderPage(uint8_t page, bool force)
{
    if (currentRenderedPage == page && !force) {
        return;
    }
    if (currentRenderedPage == page && force && page == UI_SHOW_PAGE_WORKOUT) {
        ui_workout_gps_update(createCurrentUiState());
        return;
    }

    const bool usesLiveSystemStatus = page == UI_SHOW_PAGE_HOME || page == UI_SHOW_PAGE_SETTINGS;
    UiState state = usesLiveSystemStatus ? createHomeStatusUiState() : createCurrentUiState();

    lv_obj_clean(lv_scr_act());
    currentRenderedPage = page;
    displayProductPageSet(
        page == UI_SHOW_PAGE_WORKOUT
            ? DisplayProductPage::WorkoutGps
            : page == UI_SHOW_PAGE_IG
                ? DisplayProductPage::IgControl
                : DisplayProductPage::Other
    );

    switch (page) {
    case UI_SHOW_PAGE_WORKOUT:
        ui_workout_gps_create(lv_scr_act(), state);
        break;
    case UI_SHOW_PAGE_IG:
        ui_ig_control_create(lv_scr_act(), state);
        break;
    case UI_SHOW_PAGE_HOME_MODE:
        ui_home_mode_create(lv_scr_act(), state);
        break;
    case UI_SHOW_PAGE_SETTINGS:
        ui_settings_create(
            lv_scr_act(),
            state,
            UiSettingsScreenBrightnessControl {
                getScreenBrightnessPercent,
                setScreenBrightnessPercent
            }
        );
        break;
    case UI_SHOW_PAGE_HOME:
    default:
        ui_home_status_create(lv_scr_act(), state);
        break;
    }

    if (page == UI_SHOW_PAGE_HOME) {
        installHomeNavHitAreas();
    } else {
        installBackHitArea();
        if (page == UI_SHOW_PAGE_WORKOUT) {
            installWorkoutControlHitArea();
        }
    }
    enableGestureBubble(lv_scr_act());
}

uint8_t autoPageToStaticPage(uint8_t index)
{
    switch (index % kAutoPageCount) {
    case 1:
        return UI_SHOW_PAGE_WORKOUT;
    case 2:
        return UI_SHOW_PAGE_IG;
    case 3:
        return UI_SHOW_PAGE_HOME_MODE;
    case 4:
        return UI_SHOW_PAGE_SETTINGS;
    case 0:
    default:
        return UI_SHOW_PAGE_HOME;
    }
}

uint8_t staticPageToAutoIndex(uint8_t page)
{
    switch (page) {
    case UI_SHOW_PAGE_WORKOUT:
        return 1;
    case UI_SHOW_PAGE_IG:
        return 2;
    case UI_SHOW_PAGE_HOME_MODE:
        return 3;
    case UI_SHOW_PAGE_SETTINGS:
        return 4;
    case UI_SHOW_PAGE_HOME:
    default:
        return 0;
    }
}

void showPage(uint8_t targetPage, const char* reason)
{
    if (targetPage == currentRenderedPage) {
        return;
    }

    Serial.printf(
        "EVENT: page_change reason=%s current=%u target=%u\n",
        reason,
        static_cast<unsigned>(currentRenderedPage),
        static_cast<unsigned>(targetPage)
    );
    renderPage(targetPage);
}

void onHomeNavClicked(lv_event_t* event)
{
    const uintptr_t rawTarget = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    const uint8_t targetPage = static_cast<uint8_t>(rawTarget);

    lv_indev_t* indev = lv_indev_get_act();
    if (indev != nullptr) {
        lv_indev_wait_release(indev);
    }
    showPage(targetPage, "home-button");
}

void createHomeNavHitArea(
    int16_t x,
    int16_t y,
    const char* labelText,
    uint8_t targetPage)
{
    lv_obj_t* hitArea = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(hitArea);
    lv_obj_set_pos(hitArea, x, y);
    lv_obj_set_size(hitArea, kHomeNavCardWidth, kHomeNavCardHeight);
    lv_obj_set_style_radius(hitArea, 24, 0);
    lv_obj_set_style_bg_color(hitArea, lv_color_hex(0x11151A), 0);
    lv_obj_set_style_bg_opa(hitArea, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hitArea, 1, 0);
    lv_obj_set_style_border_color(hitArea, lv_color_hex(0x26313A), 0);
    lv_obj_set_style_border_opa(hitArea, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(hitArea, lv_color_hex(0x1B222A), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(hitArea, lv_color_hex(0x4CC9F0), LV_STATE_PRESSED);
    lv_obj_clear_flag(hitArea, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hitArea, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(hitArea, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t* label = lv_label_create(hitArea);
    lv_label_set_text(label, labelText);
    lv_obj_set_width(label, kHomeNavCardWidth);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, ui_theme::fontStatus(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF5F7FA), 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_center(label);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_add_event_cb(
        hitArea,
        onHomeNavClicked,
        LV_EVENT_CLICKED,
        reinterpret_cast<void*>(static_cast<uintptr_t>(targetPage))
    );
}

void installHomeNavHitAreas()
{
    createHomeNavHitArea(kHomeNavLeftX, kHomeNavFirstRowY, "WORKOUT", UI_SHOW_PAGE_WORKOUT);
    createHomeNavHitArea(kHomeNavRightX, kHomeNavFirstRowY, "IG\nCONTROL", UI_SHOW_PAGE_IG);
    createHomeNavHitArea(kHomeNavLeftX, kHomeNavSecondRowY, "HOME\nMODE", UI_SHOW_PAGE_HOME_MODE);
    createHomeNavHitArea(kHomeNavRightX, kHomeNavSecondRowY, "SETTINGS", UI_SHOW_PAGE_SETTINGS);
}

void onBackClicked(lv_event_t* event)
{
    (void)event;

    lv_indev_t* indev = lv_indev_get_act();
    if (indev != nullptr) {
        lv_indev_wait_release(indev);
    }
    showPage(UI_SHOW_PAGE_HOME, "back");
}

void installBackHitArea()
{
    lv_obj_t* hitArea = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(hitArea);
    lv_obj_set_pos(hitArea, kBackHitX, kBackHitY);
    lv_obj_set_size(hitArea, kBackHitSize, kBackHitSize);
    lv_obj_set_style_bg_opa(hitArea, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(hitArea, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(hitArea, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (currentRenderedPage == UI_SHOW_PAGE_SETTINGS) {
        ui_settings_attach_back_button_visual(hitArea);
    }
    lv_obj_add_event_cb(hitArea, onBackClicked, LV_EVENT_CLICKED, nullptr);
}

void onWorkoutControlClicked(lv_event_t* event)
{
    (void)event;

    lv_indev_t* indev = lv_indev_get_act();
    if (indev != nullptr) {
        lv_indev_wait_release(indev);
    }

    const WorkoutCaloriesStatus previousStatus = workoutCaloriesGetStatus();
    workoutRunning = !workoutRunning;
    if (workoutRunning) {
        workoutCaloriesStart();
        if (previousStatus == WorkoutCaloriesStatus::Idle) {
            workoutSpeedResolver.start(millis());
            workoutImuSpeedStart(latestGpsData.hasFix ? latestGpsData.speedKmh : 0.0f, millis());
        } else {
            workoutSpeedResolver.resume(millis());
            workoutImuSpeedResume(millis());
        }
    } else {
        workoutCaloriesPause();
        workoutSpeedResolver.pause(millis());
        workoutImuSpeedPause();
    }
    workoutLastTickMs = millis();
    workoutLastRenderedSecond = 0xFFFFFFFF;
    workoutHasLastPoint = false;

    Serial.printf("EVENT: workout=%s\n", workoutRunning ? "running" : "paused");
    renderPage(UI_SHOW_PAGE_WORKOUT, true);
}

void installWorkoutControlHitArea()
{
    lv_obj_t* hitArea = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(hitArea);
    lv_obj_set_pos(hitArea, kWorkoutControlHitX, kWorkoutControlHitY);
    lv_obj_set_size(hitArea, kWorkoutControlHitSize, kWorkoutControlHitSize);
    lv_obj_set_style_bg_opa(hitArea, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(hitArea, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(hitArea, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(hitArea, onWorkoutControlClicked, LV_EVENT_CLICKED, nullptr);
}

void enableGestureBubble(lv_obj_t* obj)
{
    if (obj == nullptr) {
        return;
    }

    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);

    const uint32_t childCount = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < childCount; i++) {
        enableGestureBubble(lv_obj_get_child(obj, i));
    }
}

void onPageGesture(lv_event_t* event)
{
    (void)event;

    lv_indev_t* indev = lv_indev_get_act();
    if (indev == nullptr) {
        return;
    }

    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    uint8_t targetPage = currentRenderedPage;

    if (dir == LV_DIR_LEFT) {
        targetPage = autoPageToStaticPage((staticPageToAutoIndex(currentRenderedPage) + 1) % kAutoPageCount);
    } else if (dir == LV_DIR_RIGHT) {
        targetPage = UI_SHOW_PAGE_HOME;
    } else {
        return;
    }

    lv_indev_wait_release(indev);
    showPage(targetPage, (dir == LV_DIR_LEFT) ? "gesture-left" : "gesture-right");
}

void printI2cDiagConfig()
{
#if ENABLE_GPS_VERBOSE_LOG || ENABLE_TOUCH_DIAG_LOG || ENABLE_PMU_DIAG_LOG || ENABLE_RTC_DIAG_LOG
    Serial.println("I2C_DIAG_CONFIG:");
    Serial.printf("GPS=%d\n", DIAG_ENABLE_GPS);
    Serial.printf("TOUCH_INIT=%d TOUCH_POLL=%d\n", DIAG_INIT_TOUCH, DIAG_POLL_TOUCH);
    Serial.printf("PMU_INIT=%d PMU_POLL=%d\n", DIAG_INIT_PMU, DIAG_POLL_PMU);
    Serial.printf("RTC_INIT=%d RTC_POLL=%d\n", DIAG_INIT_RTC, DIAG_POLL_RTC);
    Serial.printf("I2C_INIT_ORDER_GPS_FIRST=%d\n", DIAG_I2C_INIT_ORDER_GPS_FIRST);
    Serial.println("GPS_IMPL=GPS_DATA_TEST_COMPAT");
    Serial.println("I2C_BACKEND=ARDUINO_WIRE_NG");
    Serial.println("TOUCH_BACKEND=ARDUINO_WIRE_NG");
    Serial.println("PMU_BACKEND=ARDUINO_WIRE_NG");
    Serial.println("PMU_CALLBACK_ONLY=1");
    Serial.println("RTC_BACKEND=ARDUINO_WIRE_NG");
    Serial.println("RTC_DIRECT_ESP_IDF=0");
    Serial.println("SHARED_I2C_BUS=ARDUINO_WIRE_NG");
    Serial.printf("SDA=%d\n", kLc76gI2cSda);
    Serial.printf("SCL=%d\n", kLc76gI2cScl);
    Serial.printf("FREQ=%lu\n", static_cast<unsigned long>(kLc76gI2cFreqHz));
    Serial.printf("DEVICE_ADDRESS_W=0x%02X\n", kLc76gDeviceAddress);
    Serial.printf("DEVICE_ADDRESS_R=0x%02X\n", kLc76gDeviceReadAddress);
#endif
}

void printDiagDependencyWarnings()
{
#if DIAG_POLL_TOUCH && !DIAG_INIT_TOUCH
    Serial.println("ERR: TOUCH_POLL requested but TOUCH_INIT=0, skip touch polling");
#endif
#if DIAG_POLL_PMU && !DIAG_INIT_PMU
    Serial.println("ERR: PMU_POLL requested but PMU_INIT=0, skip PMU polling");
#endif
#if DIAG_POLL_RTC && !DIAG_INIT_RTC
    Serial.println("ERR: RTC_POLL requested but RTC_INIT=0, skip RTC polling");
#endif
}

void updateI2cModuleDiagnostics()
{
    const uint32_t now = millis();
    if ((now - lastI2cModuleDiagnosticsMs) < kGpsDiagnosticsIntervalMs) {
        return;
    }
    lastI2cModuleDiagnosticsMs = now;

#if ENABLE_TOUCH_DIAG_LOG && (DIAG_INIT_TOUCH || DIAG_POLL_TOUCH)
    Serial.printf(
        "TOUCH_DIAG: INIT=%d INIT_OK=%d TOUCH_INIT_OK=%d POLL=%d TOUCH_POLL_ENABLED=%d READ_OK=%d TOUCH_READ_OK=%d TOUCHED=%d X=%d Y=%d TOUCH_BACKEND=ARDUINO_WIRE_NG TOUCH_SHARED_I2C=1 TOUCH_INSTALL_DRIVER=0 TOUCH_CALLBACK_ONLY=1 TOUCH_I2C_RESULT=%d TOUCH_ERR_NAME=%s TOUCH_TIMEOUT_COUNT=%lu TOUCH_STAGE=%s TOUCH_REG=0x%04X TOUCH_LEN=%lu TOUCH_POLL_INTERVAL_MS=%lu SHARED_I2C_READY=%d TOUCH_SHARED_I2C_WITH_GPS=%d\n",
        DIAG_INIT_TOUCH,
        touchReady ? 1 : 0,
        touchReady ? 1 : 0,
        (DIAG_INIT_TOUCH && DIAG_POLL_TOUCH) ? 1 : 0,
        (DIAG_INIT_TOUCH && DIAG_POLL_TOUCH) ? 1 : 0,
        touchDiagReadOk ? 1 : 0,
        touchDiagReadOk ? 1 : 0,
        touchDiagTouched ? 1 : 0,
        touchDiagX,
        touchDiagY,
        static_cast<int>(touchDiagLastI2cResult),
        i2cErrName(touchDiagLastI2cResult),
        static_cast<unsigned long>(touchTimeoutCount),
        touchDiagStage,
        touchDiagRegister,
        static_cast<unsigned long>(touchDiagLength),
        static_cast<unsigned long>(kTouchPollIntervalMs),
        sharedI2cReady ? 1 : 0,
        (IIC_SDA == kLc76gI2cSda && IIC_SCL == kLc76gI2cScl) ? 1 : 0
    );
#endif

#if ENABLE_PMU_DIAG_LOG && (DIAG_INIT_PMU || DIAG_POLL_PMU)
    Serial.printf(
        "PMU_DIAG: INIT=%d INIT_OK=%d POLL=%d READ_OK=%d PMU_BACKEND=ARDUINO_WIRE_NG PMU_SHARED_I2C=1 PMU_INSTALL_DRIVER=0 PMU_CALLBACK_ONLY=1 PMU_I2C_RESULT=%d PMU_ERR_NAME=%s PMU_TIMEOUT_COUNT=%lu PMU_STAGE=%s PMU_REG=0x%02X PMU_LEN=%u PMU_POLL_INTERVAL_MS=%lu BAT=%d USB=%d VBUS=%d VBUS_GOOD=%d SYS=%.2f TEMP=%.1f SHARED_I2C_READY=%d PMU_SHARED_I2C_WITH_GPS=%d\n",
        DIAG_INIT_PMU,
        latestPmuData.ready ? 1 : 0,
        (DIAG_INIT_PMU && DIAG_POLL_PMU) ? 1 : 0,
        pmuDiagReadOk ? 1 : 0,
        static_cast<int>(pmuDiagLastI2cResult),
        i2cErrName(pmuDiagLastI2cResult),
        static_cast<unsigned long>(pmuTimeoutCount),
        pmuDiagStage,
        pmuDiagRegister,
        pmuDiagLength,
        static_cast<unsigned long>(kPmuPollIntervalMs),
        latestPmuData.batteryPercent,
        latestPmuData.vbusIn ? 1 : 0,
        latestPmuData.vbusIn ? 1 : 0,
        latestPmuData.vbusGood ? 1 : 0,
        latestPmuData.systemVoltage,
        latestPmuData.temperature,
        sharedI2cReady ? 1 : 0,
        (IIC_SDA == kLc76gI2cSda && IIC_SCL == kLc76gI2cScl) ? 1 : 0
    );
#endif

#if ENABLE_RTC_DIAG_LOG && (DIAG_INIT_RTC || DIAG_POLL_RTC)
    Serial.printf(
        "RTC_DIAG: INIT=%d INIT_OK=%d POLL=%d READ_OK=%d RTC_BACKEND=ARDUINO_WIRE_NG RTC_SHARED_I2C=1 RTC_INSTALL_DRIVER=0 RTC_CALLBACK_ONLY=0 RTC_DIRECT_ESP_IDF=0 RTC_I2C_RESULT=%d RTC_ERR_NAME=%s RTC_TIMEOUT_COUNT=%lu RTC_STAGE=%s RTC_REG=0x%02X RTC_LEN=%u RTC_POLL_INTERVAL_MS=%lu YEAR=%d MONTH=%d DAY=%d HOUR=%d MINUTE=%d SECOND=%d VALID=%d FALLBACK_USED=%d SHARED_I2C_READY=%d RTC_SHARED_I2C_WITH_GPS=%d\n",
        DIAG_INIT_RTC,
        latestRtcData.ready ? 1 : 0,
        (DIAG_INIT_RTC && DIAG_POLL_RTC) ? 1 : 0,
        rtcDiagReadOk ? 1 : 0,
        static_cast<int>(rtcDiagLastI2cResult),
        i2cErrName(rtcDiagLastI2cResult),
        static_cast<unsigned long>(rtcTimeoutCount),
        rtcDiagStage,
        rtcDiagRegister,
        rtcDiagLength,
        static_cast<unsigned long>(kRtcPollIntervalMs),
        latestRtcData.year,
        latestRtcData.month,
        latestRtcData.day,
        latestRtcData.hour,
        latestRtcData.minute,
        latestRtcData.second,
        latestRtcData.valid ? 1 : 0,
        rtcFallbackUsed ? 1 : 0,
        sharedI2cReady ? 1 : 0,
        (IIC_SDA == kLc76gI2cSda && IIC_SCL == kLc76gI2cScl) ? 1 : 0
    );
#endif
}

void createUi()
{
#if DIAG_ENABLE_GPS
    renderPage(UI_SHOW_PAGE_WORKOUT);
#else
#if UI_STATIC_MOCK_PAGE == UI_SHOW_PAGE_TOUCH
    renderPage(UI_SHOW_PAGE_HOME);
#elif UI_STATIC_MOCK_PAGE == UI_SHOW_PAGE_AUTO
    currentAutoPage = 0;
    lastAutoPageMs = millis();
    renderPage(autoPageToStaticPage(currentAutoPage));
#elif UI_STATIC_MOCK_PAGE == UI_SHOW_PAGE_SETTINGS
    renderPage(UI_SHOW_PAGE_SETTINGS);
#elif UI_STATIC_MOCK_PAGE == UI_SHOW_PAGE_HOME_MODE
    renderPage(UI_SHOW_PAGE_HOME_MODE);
#elif UI_STATIC_MOCK_PAGE == UI_SHOW_PAGE_IG
    renderPage(UI_SHOW_PAGE_IG);
#elif UI_STATIC_MOCK_PAGE == UI_SHOW_PAGE_WORKOUT
    renderPage(UI_SHOW_PAGE_WORKOUT);
#else
    renderPage(UI_SHOW_PAGE_HOME);
#endif
#endif
}

void updateAutoPage()
{
#if !DIAG_ENABLE_GPS && UI_STATIC_MOCK_PAGE == UI_SHOW_PAGE_AUTO
    const uint32_t now = millis();
    if ((now - lastAutoPageMs) >= kAutoPageIntervalMs) {
        currentAutoPage = (currentAutoPage + 1) % kAutoPageCount;
        lastAutoPageMs = now;
        renderPage(autoPageToStaticPage(currentAutoPage));
    }
#endif
}

void gpsLoop(uint32_t now)
{
    (void)now;
#if DIAG_ENABLE_GPS
    updateGps();
    updateGpsDiagnostics();
#endif
}

void pmuLoop(uint32_t now)
{
    (void)now;
#if DIAG_INIT_PMU && DIAG_POLL_PMU
    updatePmu();
#endif
}

void rtcLoop(uint32_t now)
{
    (void)now;
#if DIAG_INIT_RTC && DIAG_POLL_RTC
    updateRtc();
#endif
}

void diagnosticsLoop(uint32_t now)
{
    (void)now;
    updateI2cModuleDiagnostics();
}

void uiLoop(uint32_t now)
{
    static uint32_t lastHomeStatusRefreshMs = 0;
    static uint32_t lastSettingsStatusRefreshMs = 0;

    updateWorkoutRuntime();
    updateAutoPage();
    ui_ig_control_loop(now);

    if (currentRenderedPage == UI_SHOW_PAGE_HOME &&
        (lastHomeStatusRefreshMs == 0 || (now - lastHomeStatusRefreshMs) >= 750)) {
        lastHomeStatusRefreshMs = now;
        ui_home_status_update(createHomeStatusUiState());
    }

    if (currentRenderedPage == UI_SHOW_PAGE_SETTINGS &&
        (lastSettingsStatusRefreshMs == 0 || (now - lastSettingsStatusRefreshMs) >= 750)) {
        lastSettingsStatusRefreshMs = now;
        ui_settings_update(createHomeStatusUiState());
    }
}

void optionalModulesLoop(uint32_t now)
{
#if ENABLE_DISPLAY_BLE_SERVER
    static uint32_t lastDisplayBleLoopLogMs = 0;
    if (lastDisplayBleLoopLogMs == 0 || (now - lastDisplayBleLoopLogMs) >= 5000) {
        lastDisplayBleLoopLogMs = now;
#if ENABLE_DISPLAY_BLE_LOG
        Serial.println("EVENT: display_ble_loop active");
#endif
    }
    displayBleLoop();
#else
    (void)now;
#endif

#if ENABLE_DISPLAY_STATE_SERIAL_CONTROL
    displayStateSerialControlLoop();
#endif
}

void lvglLoop(uint32_t now)
{
    (void)now;
    lv_timer_handler();
}

void printSystemSummaryIfDue(uint32_t now)
{
#if ENABLE_SYSTEM_SUMMARY_LOG
    if (lastSystemSummaryMs == 0) {
        lastSystemSummaryMs = now;
        return;
    }
    if ((now - lastSystemSummaryMs) < SYSTEM_SUMMARY_INTERVAL_MS) {
        return;
    }
    lastSystemSummaryMs = now;

    const DisplayState displayState = displayStateGetSnapshot();
#if ENABLE_DISPLAY_BLE_SERVER
    const bool bleConnected = displayBleIsConnected();
#else
    const bool bleConnected = false;
#endif

    Serial.printf(
        "STATUS: gps_fix=%d sat=%u speed=%.1f bytes=%lu pmu_bat=%d usb=%d rtc_valid=%d touch=%d state_proj=%d bright=%u mask=0x%04X ble=%d\n",
        latestGpsData.hasFix ? 1 : 0,
        static_cast<unsigned>(latestGpsData.satellites),
        latestGpsData.hasFix ? latestGpsData.speedKmh : 0.0f,
        static_cast<unsigned long>(gpsSummaryBytesLastWindow),
        latestPmuData.ready ? latestPmuData.batteryPercent : -1,
        latestPmuData.vbusIn ? 1 : 0,
        (latestRtcData.ready && latestRtcData.valid) ? 1 : 0,
        touchDiagTouched ? 1 : 0,
        displayState.projectionOn ? 1 : 0,
        static_cast<unsigned>(displayState.brightnessPercent),
        static_cast<unsigned>(displayState.displayMask),
        bleConnected ? 1 : 0
    );
#else
    (void)now;
#endif
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(10);

#if ENABLE_SETUP_STEP_LOG
    Serial.println("BOOT: ui_static_mock start");
#endif
    logSetupStep(1, "serial ready");
    systemResourceLog("BOOT");

    displayStateBegin();
    workoutCaloriesBegin();
    logSetupStep(2, "display state ready");
#if ENABLE_DISPLAY_STATE_LOG
    displayStatePrintSnapshot();
#endif

#if ENABLE_DISPLAY_STATE_SERIAL_CONTROL
    displayStateSerialControlBegin();
    logSetupStep(3, "serial control ready");
#else
    logSetupStep(3, "serial control disabled");
#endif
    printI2cDiagConfig();
    printDiagDependencyWarnings();

    const bool sharedI2cOk = initSharedI2cBusOnce();
    logSetupStepResult(4, "shared i2c", sharedI2cOk);
    systemResourceLog("AFTER_SHARED_I2C");

    initDisplay();
    systemResourceLog("AFTER_DISPLAY");
#if ENABLE_RESOURCE_BASELINE_AUDIT
    const SystemResourceSnapshot lvglResourceBefore = systemResourceCapture();
#endif
    initLvgl();
#if ENABLE_RESOURCE_BASELINE_AUDIT
    const SystemResourceSnapshot lvglResourceAfter = systemResourceCapture();
    const size_t lvglBufferPixels = ui_theme::kScreenWidth * ui_theme::kScreenHeight / 4;
    const size_t lvglBufferBytes = lvglBufferPixels * sizeof(lv_color_t);
    Serial.printf(
        "LVGL_RESOURCE: buffers=1 mode=single pixels=%lu bytes_per_pixel=%u single_buffer_bytes=%lu total_bytes=%lu allocation_caps=MALLOC_CAP_DMA\n",
        static_cast<unsigned long>(lvglBufferPixels),
        static_cast<unsigned>(sizeof(lv_color_t)),
        static_cast<unsigned long>(lvglBufferBytes),
        static_cast<unsigned long>(lvglBufferBytes)
    );
    systemResourceLogBuffer("LVGL_BUF1", lvBuf1, lvglBufferBytes);
    systemResourceLogSnapshot("LVGL_BEFORE", lvglResourceBefore);
    systemResourceLogSnapshot("LVGL_AFTER", lvglResourceAfter);
    systemResourceLogDelta("LVGL", lvglResourceBefore, lvglResourceAfter);
#endif
    systemResourceLog("AFTER_LVGL");
    logSetupStep(5, "display/lvgl ready");

#if ENABLE_SETUP_STEP_LOG
#if DIAG_I2C_INIT_ORDER_GPS_FIRST
    Serial.println("EVENT: configured_init_order=gps_first");
#else
    Serial.println("EVENT: configured_init_order=touch_first");
#endif
#endif
#if DIAG_ENABLE_GPS
#if ENABLE_RESOURCE_BASELINE_AUDIT
    const SystemResourceSnapshot gpsResourceBefore = systemResourceCapture();
#endif
    initGps();
#if ENABLE_RESOURCE_BASELINE_AUDIT
    const SystemResourceSnapshot gpsResourceAfter = systemResourceCapture();
    systemResourceLogSnapshot("GPS_BEFORE", gpsResourceBefore);
    systemResourceLogSnapshot("GPS_AFTER", gpsResourceAfter);
    systemResourceLogDelta("GPS", gpsResourceBefore, gpsResourceAfter);
#endif
    logSetupStepResult(6, "gps", gpsReady);
#else
    gpsReady = false;
    logSetupStep(6, "gps disabled");
#endif
#if DIAG_INIT_TOUCH
#if ENABLE_RESOURCE_BASELINE_AUDIT
    const SystemResourceSnapshot touchResourceBefore = systemResourceCapture();
#endif
    initTouch();
#if ENABLE_RESOURCE_BASELINE_AUDIT
    const SystemResourceSnapshot touchResourceAfter = systemResourceCapture();
    systemResourceLogSnapshot("TOUCH_BEFORE", touchResourceBefore);
    systemResourceLogSnapshot("TOUCH_AFTER", touchResourceAfter);
    systemResourceLogDelta("TOUCH", touchResourceBefore, touchResourceAfter);
#endif
    logSetupStepResult(7, "touch", touchReady);
#else
    logSetupStep(7, "touch disabled");
#endif
#if DIAG_INIT_PMU
#if ENABLE_RESOURCE_BASELINE_AUDIT
    const SystemResourceSnapshot pmuResourceBefore = systemResourceCapture();
#endif
    initPmu();
#if ENABLE_RESOURCE_BASELINE_AUDIT
    const SystemResourceSnapshot pmuResourceAfter = systemResourceCapture();
    systemResourceLogSnapshot("PMU_BEFORE", pmuResourceBefore);
    systemResourceLogSnapshot("PMU_AFTER", pmuResourceAfter);
    systemResourceLogDelta("PMU", pmuResourceBefore, pmuResourceAfter);
#endif
    logSetupStepResult(8, "pmu", latestPmuData.ready);
#else
    logSetupStep(8, "pmu disabled");
#endif
#if DIAG_INIT_RTC
#if ENABLE_RESOURCE_BASELINE_AUDIT
    const SystemResourceSnapshot rtcResourceBefore = systemResourceCapture();
#endif
    initRtc();
#if ENABLE_RESOURCE_BASELINE_AUDIT
    const SystemResourceSnapshot rtcResourceAfter = systemResourceCapture();
    systemResourceLogSnapshot("RTC_BEFORE", rtcResourceBefore);
    systemResourceLogSnapshot("RTC_AFTER", rtcResourceAfter);
    systemResourceLogDelta("RTC", rtcResourceBefore, rtcResourceAfter);
#endif
    logSetupStepResult(9, "rtc", latestRtcData.ready);
#else
    logSetupStep(9, "rtc disabled");
#endif
    workoutImuReady = workoutImuSpeedBegin(workoutImuI2cCallback, workoutImuHalCallback);
    Serial.printf("SETUP: qmi8658 workout speed %s address=0x6B\n", workoutImuReady ? "ready" : "not ready");
    systemResourceLog("AFTER_I2C_DEVICES");
    createUi();
    logSetupStep(10, "ui ready");
    systemResourceLog("AFTER_UI");

#if ENABLE_DISPLAY_BLE_SERVER
    logSetupStep(11, "display ble begin");
#if ENABLE_RESOURCE_BASELINE_AUDIT
    const SystemResourceSnapshot bleResourceBefore = systemResourceCapture();
#endif
    displayBleServerReady = displayBleBegin();
#if ENABLE_RESOURCE_BASELINE_AUDIT
    const SystemResourceSnapshot bleResourceAfter = systemResourceCapture();
    systemResourceLogSnapshot("BLE_BEFORE", bleResourceBefore);
    systemResourceLogSnapshot("BLE_AFTER", bleResourceAfter);
    systemResourceLogDelta("BLE", bleResourceBefore, bleResourceAfter);
#endif
    logSetupStepResult(11, "display ble", displayBleServerReady);
#else
    logSetupStep(11, "optional modules ready ble=0");
#endif
    systemResourceLog("AFTER_OPTIONAL_MODULES");

#if ENABLE_SETUP_STEP_LOG
    Serial.println("BOOT: ui_static_mock ready");
#endif
    systemResourceLog("SETUP_COMPLETE");
}

void loop()
{
    const uint32_t now = millis();

    powerManagerLoop(now);
    if (!systemPowerAllowsBusinessWork()) {
        vTaskDelay(pdMS_TO_TICKS(50));
        return;
    }

    gpsLoop(now);
    pmuLoop(now);
    rtcLoop(now);
    diagnosticsLoop(now);
    uiLoop(now);
    optionalModulesLoop(now);
    lvglLoop(now);
    printSystemSummaryIfDue(now);
#if ENABLE_PERIODIC_RESOURCE_LOG
    static uint32_t lastPeriodicResourceLogMs = 0;
    if ((now - lastPeriodicResourceLogMs) >= 10000) {
        lastPeriodicResourceLogMs = now;
        systemResourceLog("PERIODIC");
    }
#endif
    delay(5);
}
