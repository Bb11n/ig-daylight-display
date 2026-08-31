if(NOT DEFINED IG_ARDUINO_LIB_ROOT OR IG_ARDUINO_LIB_ROOT STREQUAL "")
    if(DEFINED ENV{IG_ARDUINO_LIB_ROOT})
        file(TO_CMAKE_PATH "$ENV{IG_ARDUINO_LIB_ROOT}" IG_ARDUINO_LIB_ROOT)
    elseif(DEFINED ENV{USERPROFILE})
        file(TO_CMAKE_PATH "$ENV{USERPROFILE}/Documents/Arduino/libraries" IG_ARDUINO_LIB_ROOT)
    endif()
endif()

set(SENSOR_ROOT "${IG_ARDUINO_LIB_ROOT}/SensorLib")
set(SENSOR_PROPERTIES "${SENSOR_ROOT}/library.properties")
if(NOT EXISTS "${SENSOR_ROOT}/src/TouchDrvCSTXXX.hpp" OR NOT EXISTS "${SENSOR_PROPERTIES}")
    message(FATAL_ERROR "SensorLib 0.3.1 not found below IG_ARDUINO_LIB_ROOT=${IG_ARDUINO_LIB_ROOT}")
endif()
file(READ "${SENSOR_PROPERTIES}" SENSOR_METADATA)
if(NOT SENSOR_METADATA MATCHES "version=0\\.3\\.1")
    message(FATAL_ERROR "SensorLib version mismatch; Phase 2 requires exactly 0.3.1")
endif()

file(GLOB_RECURSE SENSOR_SOURCES
    "${SENSOR_ROOT}/src/*.c"
    "${SENSOR_ROOT}/src/*.cpp"
)
idf_component_register(
    SRCS ${SENSOR_SOURCES}
    INCLUDE_DIRS
        "${SENSOR_ROOT}/src"
        "${SENSOR_ROOT}/src/REG"
        "${SENSOR_ROOT}/src/touch"
        "${SENSOR_ROOT}/src/platform"
        "${SENSOR_ROOT}/src/bosch"
        "${SENSOR_ROOT}/src/bosch/firmware"
        "${SENSOR_ROOT}/src/bosch/BMM150"
        "${SENSOR_ROOT}/src/bosch/common"
    REQUIRES espressif__arduino-esp32 esp_timer esp_driver_gpio driver
)
