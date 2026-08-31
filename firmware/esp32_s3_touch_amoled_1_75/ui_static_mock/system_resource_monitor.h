#pragma once

#include <stddef.h>

struct SystemResourceSnapshot {
    size_t internalFree;
    size_t internalLargestBlock;
    size_t psramFree;
};

SystemResourceSnapshot systemResourceCapture();
void systemResourceLog(const char* stage);
void systemResourceLogSnapshot(const char* stage, const SystemResourceSnapshot& snapshot);
void systemResourceLogDelta(
    const char* module,
    const SystemResourceSnapshot& before,
    const SystemResourceSnapshot& after
);
void systemResourceLogBuffer(const char* name, const void* buffer, size_t requestedBytes);
