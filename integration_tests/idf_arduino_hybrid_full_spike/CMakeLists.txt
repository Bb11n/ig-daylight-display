cmake_minimum_required(VERSION 3.16)

if(DEFINED ENV{IG_ARDUINO_LIB_ROOT})
    set(IG_ARDUINO_LIB_ROOT "$ENV{IG_ARDUINO_LIB_ROOT}" CACHE PATH "Verified Arduino library root")
elseif(DEFINED ENV{USERPROFILE})
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}/Documents/Arduino/libraries" IG_DEFAULT_ARDUINO_LIB_ROOT)
    set(IG_ARDUINO_LIB_ROOT "${IG_DEFAULT_ARDUINO_LIB_ROOT}" CACHE PATH "Verified Arduino library root")
else()
    message(FATAL_ERROR "Set IG_ARDUINO_LIB_ROOT to the verified Arduino libraries directory")
endif()

set(IG_SPIKE_STAGE "5" CACHE STRING "Integration stage: 1=display, 2=pages, 3=touch, 4=I2C devices, 5=BLE")
if(NOT IG_SPIKE_STAGE MATCHES "^[1-5]$")
    message(FATAL_ERROR "IG_SPIKE_STAGE must be an integer from 1 through 5")
endif()

set(IG_I2C_VARIANT "0" CACHE STRING "I2C diagnostic variant: 0=full, 1=touch, 2=touch+PMU, 3=touch+PMU+RTC, 4=all")
if(NOT IG_I2C_VARIANT MATCHES "^[0-4]$")
    message(FATAL_ERROR "IG_I2C_VARIANT must be an integer from 0 through 4")
endif()

set(IG_ENABLE_GPS_RUNTIME "1" CACHE STRING "Enable product GPS initialization and polling: 0=off, 1=on")
if(NOT IG_ENABLE_GPS_RUNTIME MATCHES "^[01]$")
    message(FATAL_ERROR "IG_ENABLE_GPS_RUNTIME must be 0 or 1")
endif()

set(IG_VOICE_FEEDBACK_GATE_A_TEST "0" CACHE STRING "Explicit one-shot speaker test: 0=off, 1=on")
if(NOT IG_VOICE_FEEDBACK_GATE_A_TEST MATCHES "^[01]$")
    message(FATAL_ERROR "IG_VOICE_FEEDBACK_GATE_A_TEST must be 0 or 1")
endif()

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components/arduino_gfx"
    "${CMAKE_CURRENT_LIST_DIR}/components/sensor_lib"
    "${CMAKE_CURRENT_LIST_DIR}/components/xpowers_lib"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Required by esp-nimble-cpp when Arduino is consumed as an ESP-IDF component.
add_compile_definitions(ARDUINO_ARCH_ESP32=1 BOARD_HAS_PSRAM=1)

project(idf_arduino_hybrid_full_spike)
