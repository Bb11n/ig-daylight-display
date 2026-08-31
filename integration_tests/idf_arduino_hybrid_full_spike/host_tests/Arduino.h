#pragma once

#include <stdint.h>

using portMUX_TYPE = int;

#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))

struct TestSerial {
    template <typename... Args>
    void printf(const char*, Args...)
    {
    }
};

extern TestSerial Serial;
