#include "system_resource_monitor.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>

#include "debug_log_config.h"

namespace {

const char* memoryRegion(const void* pointer)
{
    if (pointer == nullptr) {
        return "unallocated";
    }
    if (esp_ptr_external_ram(pointer)) {
        return "psram";
    }
    if (esp_ptr_internal(pointer)) {
        return "internal";
    }
    return "other";
}

} // namespace

SystemResourceSnapshot systemResourceCapture()
{
    SystemResourceSnapshot snapshot = {};
    snapshot.internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    snapshot.internalLargestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#if defined(MALLOC_CAP_SPIRAM)
    snapshot.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif
    return snapshot;
}

void systemResourceLogSnapshot(const char* stage, const SystemResourceSnapshot& snapshot)
{
#if ENABLE_SYSTEM_RESOURCE_LOG
    Serial.printf(
        "RESOURCE_POINT: stage=%s internal_free=%lu internal_largest_block=%lu psram_free=%lu\n",
        stage != nullptr ? stage : "UNKNOWN",
        static_cast<unsigned long>(snapshot.internalFree),
        static_cast<unsigned long>(snapshot.internalLargestBlock),
        static_cast<unsigned long>(snapshot.psramFree)
    );
#else
    (void)stage;
    (void)snapshot;
#endif
}

void systemResourceLogDelta(
    const char* module,
    const SystemResourceSnapshot& before,
    const SystemResourceSnapshot& after)
{
#if ENABLE_SYSTEM_RESOURCE_LOG
    const long estimatedInternalUsage = static_cast<long>(before.internalFree) - static_cast<long>(after.internalFree);
    const long estimatedPsramUsage = static_cast<long>(before.psramFree) - static_cast<long>(after.psramFree);
    Serial.printf(
        "RESOURCE_DELTA: module=%s internal_before=%lu internal_after=%lu estimated_internal_usage=%ld internal_largest_before=%lu internal_largest_after=%lu psram_before=%lu psram_after=%lu estimated_psram_usage=%ld\n",
        module != nullptr ? module : "UNKNOWN",
        static_cast<unsigned long>(before.internalFree),
        static_cast<unsigned long>(after.internalFree),
        estimatedInternalUsage,
        static_cast<unsigned long>(before.internalLargestBlock),
        static_cast<unsigned long>(after.internalLargestBlock),
        static_cast<unsigned long>(before.psramFree),
        static_cast<unsigned long>(after.psramFree),
        estimatedPsramUsage
    );
#else
    (void)module;
    (void)before;
    (void)after;
#endif
}

void systemResourceLogBuffer(const char* name, const void* buffer, size_t requestedBytes)
{
#if ENABLE_SYSTEM_RESOURCE_LOG
    const size_t allocatedBytes = buffer != nullptr
        ? heap_caps_get_allocated_size(const_cast<void*>(buffer))
        : 0;
    Serial.printf(
        "RESOURCE_BUFFER: name=%s ptr=%p requested_bytes=%lu allocated_bytes=%lu region=%s\n",
        name != nullptr ? name : "UNKNOWN",
        buffer,
        static_cast<unsigned long>(requestedBytes),
        static_cast<unsigned long>(allocatedBytes),
        memoryRegion(buffer)
    );
#else
    (void)name;
    (void)buffer;
    (void)requestedBytes;
#endif
}

void systemResourceLog(const char* stage)
{
#if ENABLE_SYSTEM_RESOURCE_LOG
    const char* safeStage = stage != nullptr ? stage : "UNKNOWN";
    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t minFreeHeap = ESP.getMinFreeHeap();
    const size_t largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const size_t internalLargestFreeBlock = heap_caps_get_largest_free_block(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    size_t psramTotal = 0;
    size_t psramFree = 0;
#if defined(MALLOC_CAP_SPIRAM)
    psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif

    Serial.printf(
        "MEMORY: stage=%s free_heap=%lu min_free_heap=%lu largest_free_block=%lu internal_free=%lu internal_largest_block=%lu psram_total=%lu psram_free=%lu\n",
        safeStage,
        static_cast<unsigned long>(freeHeap),
        static_cast<unsigned long>(minFreeHeap),
        static_cast<unsigned long>(largestFreeBlock),
        static_cast<unsigned long>(internalFree),
        static_cast<unsigned long>(internalLargestFreeBlock),
        static_cast<unsigned long>(psramTotal),
        static_cast<unsigned long>(psramFree)
    );
#else
    (void)stage;
#endif
}
