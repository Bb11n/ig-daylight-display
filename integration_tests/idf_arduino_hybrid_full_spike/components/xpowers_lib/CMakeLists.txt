if(NOT DEFINED IG_ARDUINO_LIB_ROOT OR IG_ARDUINO_LIB_ROOT STREQUAL "")
    if(DEFINED ENV{IG_ARDUINO_LIB_ROOT})
        file(TO_CMAKE_PATH "$ENV{IG_ARDUINO_LIB_ROOT}" IG_ARDUINO_LIB_ROOT)
    elseif(DEFINED ENV{USERPROFILE})
        file(TO_CMAKE_PATH "$ENV{USERPROFILE}/Documents/Arduino/libraries" IG_ARDUINO_LIB_ROOT)
    endif()
endif()

set(XPOWERS_ROOT "${IG_ARDUINO_LIB_ROOT}/XPowersLib")
set(XPOWERS_PROPERTIES "${XPOWERS_ROOT}/library.properties")
if(NOT EXISTS "${XPOWERS_ROOT}/src/XPowersLib.h" OR NOT EXISTS "${XPOWERS_PROPERTIES}")
    message(FATAL_ERROR "XPowersLib 0.2.6 not found below IG_ARDUINO_LIB_ROOT=${IG_ARDUINO_LIB_ROOT}")
endif()
file(READ "${XPOWERS_PROPERTIES}" XPOWERS_METADATA)
if(NOT XPOWERS_METADATA MATCHES "version=0\\.2\\.6")
    message(FATAL_ERROR "XPowersLib version mismatch; Phase 2 requires exactly 0.2.6")
endif()

file(GLOB_RECURSE XPOWERS_SOURCES "${XPOWERS_ROOT}/src/*.cpp")
idf_component_register(
    SRCS ${XPOWERS_SOURCES}
    INCLUDE_DIRS
        "${XPOWERS_ROOT}/src"
        "${XPOWERS_ROOT}/src/REG"
    REQUIRES espressif__arduino-esp32
)
