get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(PRODUCT_UI_DIR "${REPOSITORY_ROOT}/firmware/esp32_s3_touch_amoled_1_75/ui_static_mock")

set(PRODUCT_SOURCES
    "${PRODUCT_UI_DIR}/display_ble_server.cpp"
    "${PRODUCT_UI_DIR}/display_product_page.cpp"
    "${PRODUCT_UI_DIR}/display_protocol_v2.cpp"
    "${PRODUCT_UI_DIR}/display_state.cpp"
    "${PRODUCT_UI_DIR}/display_state_serial_control.cpp"
    "${PRODUCT_UI_DIR}/gps_parser.cpp"
    "${PRODUCT_UI_DIR}/gps_i2c_transport.cpp"
    "${PRODUCT_UI_DIR}/system_power_state.cpp"
    "${PRODUCT_UI_DIR}/ui_battery_ring.cpp"
    "${PRODUCT_UI_DIR}/ui_home_mode.cpp"
    "${PRODUCT_UI_DIR}/ui_home_status.cpp"
    "${PRODUCT_UI_DIR}/ui_ig_control.cpp"
    "${PRODUCT_UI_DIR}/ui_settings.cpp"
    "${PRODUCT_UI_DIR}/ui_workout_gps.cpp"
    "${PRODUCT_UI_DIR}/workout_calories.cpp"
    "${PRODUCT_UI_DIR}/workout_imu_estimator.cpp"
    "${PRODUCT_UI_DIR}/workout_imu_speed.cpp"
    "${PRODUCT_UI_DIR}/workout_speed_resolver.cpp"
)

idf_component_register(
    SRCS
        "main.cpp"
        "display_runtime.cpp"
        "i2c_telemetry.cpp"
        "spike_resource_monitor.cpp"
        "voice_audio_adapter.cpp"
        "voice_command.cpp"
        "voice_command_adapter.cpp"
        "voice_feedback_runtime.cpp"
        "voice_feedback_wav.cpp"
        "voice_sr_runtime.cpp"
        ${PRODUCT_SOURCES}
    INCLUDE_DIRS
        "."
        "${PRODUCT_UI_DIR}"
    REQUIRES
        espressif__arduino-esp32
        arduino_gfx
        sensor_lib
        xpowers_lib
        lvgl
        esp-nimble-cpp
        esp-sr
        esp_codec_dev
        esp_driver_i2s
        fatfs
        esp_driver_sdmmc
)

# Keep GCC 14 diagnostics visible without changing validated Arduino libraries.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format-truncation
    -Wno-error=overloaded-virtual
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    IG_SPIKE_STAGE=${IG_SPIKE_STAGE}
    IG_I2C_VARIANT=${IG_I2C_VARIANT}
    ENABLE_GPS_RUNTIME=${IG_ENABLE_GPS_RUNTIME}
    ENABLE_RESOURCE_BASELINE_AUDIT=1
    ENABLE_PERIODIC_RESOURCE_LOG=1
    IG_VOICE_FEEDBACK_GATE_A_TEST=${IG_VOICE_FEEDBACK_GATE_A_TEST}
)

target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=i2cWrite"
    "-Wl,--wrap=i2cRead"
    "-Wl,--wrap=i2cWriteReadNonStop"
)

if(IG_SPIKE_STAGE EQUAL 1)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        UI_STATIC_MOCK_PAGE=1
        ENABLE_DISPLAY_BLE_SERVER=0
        DIAG_ENABLE_GPS=0
        DIAG_INIT_TOUCH=0
        DIAG_POLL_TOUCH=0
        DIAG_INIT_PMU=0
        DIAG_POLL_PMU=0
        DIAG_INIT_RTC=0
        DIAG_POLL_RTC=0
    )
elseif(IG_SPIKE_STAGE EQUAL 2)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        UI_STATIC_MOCK_PAGE=100
        ENABLE_DISPLAY_BLE_SERVER=0
        DIAG_ENABLE_GPS=0
        DIAG_INIT_TOUCH=0
        DIAG_POLL_TOUCH=0
        DIAG_INIT_PMU=0
        DIAG_POLL_PMU=0
        DIAG_INIT_RTC=0
        DIAG_POLL_RTC=0
    )
elseif(IG_SPIKE_STAGE EQUAL 3)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        UI_STATIC_MOCK_PAGE=100
        ENABLE_DISPLAY_BLE_SERVER=0
        DIAG_ENABLE_GPS=0
        DIAG_INIT_TOUCH=1
        DIAG_POLL_TOUCH=1
        DIAG_INIT_PMU=0
        DIAG_POLL_PMU=0
        DIAG_INIT_RTC=0
        DIAG_POLL_RTC=0
    )
elseif(IG_SPIKE_STAGE EQUAL 4)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        UI_STATIC_MOCK_PAGE=100
        ENABLE_DISPLAY_BLE_SERVER=0
    )
else()
    if(IG_I2C_VARIANT EQUAL 1)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            UI_STATIC_MOCK_PAGE=100 ENABLE_DISPLAY_BLE_SERVER=0
            DIAG_ENABLE_GPS=0 DIAG_INIT_TOUCH=1 DIAG_POLL_TOUCH=1
            DIAG_INIT_PMU=0 DIAG_POLL_PMU=0 DIAG_INIT_RTC=0 DIAG_POLL_RTC=0)
    elseif(IG_I2C_VARIANT EQUAL 2)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            UI_STATIC_MOCK_PAGE=100 ENABLE_DISPLAY_BLE_SERVER=0
            DIAG_ENABLE_GPS=0 DIAG_INIT_TOUCH=1 DIAG_POLL_TOUCH=1
            DIAG_INIT_PMU=1 DIAG_POLL_PMU=1 DIAG_INIT_RTC=0 DIAG_POLL_RTC=0)
    elseif(IG_I2C_VARIANT EQUAL 3)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            UI_STATIC_MOCK_PAGE=100 ENABLE_DISPLAY_BLE_SERVER=0
            DIAG_ENABLE_GPS=0 DIAG_INIT_TOUCH=1 DIAG_POLL_TOUCH=1
            DIAG_INIT_PMU=1 DIAG_POLL_PMU=1 DIAG_INIT_RTC=1 DIAG_POLL_RTC=1)
    else()
        target_compile_definitions(${COMPONENT_LIB} PRIVATE
            UI_STATIC_MOCK_PAGE=100 ENABLE_DISPLAY_BLE_SERVER=0)
    endif()
endif()
