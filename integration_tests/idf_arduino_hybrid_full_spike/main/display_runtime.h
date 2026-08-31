#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

bool display_runtime_init();
bool display_runtime_start_ble();
void display_runtime_loop();
void display_runtime_log_memory(const char* stage);

esp_err_t display_runtime_shared_i2c_write(
    uint8_t address,
    const uint8_t* data,
    size_t length,
    uint32_t timeout_ms
);
esp_err_t display_runtime_shared_i2c_read(
    uint8_t address,
    uint8_t* data,
    size_t length,
    uint32_t timeout_ms
);
