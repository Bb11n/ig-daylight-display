#include "system_resource_monitor.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_log.h>

namespace {

constexpr char kResourceTag[] = "PHASE2_RESOURCE";

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

}  // namespace

SystemResourceSnapshot systemResourceCapture()
{
    SystemResourceSnapshot snapshot = {};
    snapshot.internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    snapshot.internalLargestBlock = heap_caps_get_largest_free_block(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    snapshot.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    return snapshot;
}

void systemResourceLogSnapshot(const char* stage, const SystemResourceSnapshot& snapshot)
{
    ESP_LOGI(
        kResourceTag,
        "RESOURCE_POINT: stage=%s internal_free=%u internal_largest_block=%u psram_free=%u",
        stage != nullptr ? stage : "UNKNOWN",
        static_cast<unsigned>(snapshot.internalFree),
        static_cast<unsigned>(snapshot.internalLargestBlock),
        static_cast<unsigned>(snapshot.psramFree));
}

void systemResourceLogDelta(
    const char* module,
    const SystemResourceSnapshot& before,
    const SystemResourceSnapshot& after)
{
    const long internalUsage = static_cast<long>(before.internalFree) - static_cast<long>(after.internalFree);
    const long psramUsage = static_cast<long>(before.psramFree) - static_cast<long>(after.psramFree);
    ESP_LOGI(
        kResourceTag,
        "RESOURCE_DELTA: module=%s internal_before=%u internal_after=%u "
        "estimated_internal_usage=%ld internal_largest_before=%u internal_largest_after=%u "
        "psram_before=%u psram_after=%u estimated_psram_usage=%ld",
        module != nullptr ? module : "UNKNOWN",
        static_cast<unsigned>(before.internalFree),
        static_cast<unsigned>(after.internalFree),
        internalUsage,
        static_cast<unsigned>(before.internalLargestBlock),
        static_cast<unsigned>(after.internalLargestBlock),
        static_cast<unsigned>(before.psramFree),
        static_cast<unsigned>(after.psramFree),
        psramUsage);
}

void systemResourceLogBuffer(const char* name, const void* buffer, size_t requestedBytes)
{
    const size_t allocatedBytes = buffer != nullptr
        ? heap_caps_get_allocated_size(const_cast<void*>(buffer))
        : 0;
    ESP_LOGI(
        kResourceTag,
        "RESOURCE_BUFFER: name=%s ptr=%p requested_bytes=%u allocated_bytes=%u region=%s",
        name != nullptr ? name : "UNKNOWN",
        buffer,
        static_cast<unsigned>(requestedBytes),
        static_cast<unsigned>(allocatedBytes),
        memoryRegion(buffer));
}

void systemResourceLog(const char* stage)
{
    const size_t largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internalLargest = heap_caps_get_largest_free_block(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(
        kResourceTag,
        "MEMORY: stage=%s free_heap=%u min_free_heap=%u largest_free_block=%u "
        "internal_free=%u internal_largest_block=%u psram_total=%u psram_free=%u",
        stage != nullptr ? stage : "UNKNOWN",
        static_cast<unsigned>(ESP.getFreeHeap()),
        static_cast<unsigned>(ESP.getMinFreeHeap()),
        static_cast<unsigned>(largestFreeBlock),
        static_cast<unsigned>(internalFree),
        static_cast<unsigned>(internalLargest),
        static_cast<unsigned>(psramTotal),
        static_cast<unsigned>(psramFree));
}

void display_runtime_log_memory(const char* stage)
{
    const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internalMinimum = heap_caps_get_minimum_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internalLargest = heap_caps_get_largest_free_block(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t dmaFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    const size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(
        kResourceTag,
        "VOICE_BLE_MEMORY: stage=%s internal_free=%u internal_minimum=%u "
        "largest_internal_block=%u DMA_free=%u PSRAM_free=%u",
        stage != nullptr ? stage : "UNKNOWN",
        static_cast<unsigned>(internalFree),
        static_cast<unsigned>(internalMinimum),
        static_cast<unsigned>(internalLargest),
        static_cast<unsigned>(dmaFree),
        static_cast<unsigned>(psramFree));
}
