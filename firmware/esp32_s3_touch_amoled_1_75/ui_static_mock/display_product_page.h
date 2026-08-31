#pragma once

#include <stdint.h>

enum class DisplayProductPage : uint8_t {
    Other = 0,
    WorkoutGps = 1,
    IgControl = 2,
};

void displayProductPageSet(DisplayProductPage page);
DisplayProductPage displayProductPageGet();
