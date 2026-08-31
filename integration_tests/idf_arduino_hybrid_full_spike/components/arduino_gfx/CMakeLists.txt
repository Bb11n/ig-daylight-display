if(NOT DEFINED IG_ARDUINO_LIB_ROOT OR IG_ARDUINO_LIB_ROOT STREQUAL "")
    if(DEFINED ENV{IG_ARDUINO_LIB_ROOT})
        file(TO_CMAKE_PATH "$ENV{IG_ARDUINO_LIB_ROOT}" IG_ARDUINO_LIB_ROOT)
    elseif(DEFINED ENV{USERPROFILE})
        file(TO_CMAKE_PATH "$ENV{USERPROFILE}/Documents/Arduino/libraries" IG_ARDUINO_LIB_ROOT)
    endif()
endif()

set(GFX_ROOT "${IG_ARDUINO_LIB_ROOT}/GFX_Library_for_Arduino")
set(GFX_PROPERTIES "${GFX_ROOT}/library.properties")
if(NOT EXISTS "${GFX_ROOT}/src/Arduino_GFX_Library.h" OR NOT EXISTS "${GFX_PROPERTIES}")
    message(FATAL_ERROR "Arduino_GFX 1.6.6 not found below IG_ARDUINO_LIB_ROOT=${IG_ARDUINO_LIB_ROOT}")
endif()
file(READ "${GFX_PROPERTIES}" GFX_METADATA)
if(NOT GFX_METADATA MATCHES "version=1\\.6\\.6")
    message(FATAL_ERROR "Arduino_GFX version mismatch; Phase 2 requires exactly 1.6.6")
endif()

file(GLOB_RECURSE GFX_SOURCES "${GFX_ROOT}/src/*.cpp")
idf_component_register(
    SRCS ${GFX_SOURCES}
    INCLUDE_DIRS "${GFX_ROOT}/src"
    REQUIRES espressif__arduino-esp32 driver esp_lcd
)
