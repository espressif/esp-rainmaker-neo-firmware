/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file heap_monitor_esp.c
 * @brief Heap monitor common implementation for ESP-IDF.
 */

#include <stdint.h>
#include <inttypes.h>

#include "sdkconfig.h"

#include "osal_heap_monitor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "osal_heap_mon";

static void __print_heap_trace_summary(const char *tag, const char *cap_name, uint32_t cap)
{
    /* Get heap trace summary */
    multi_heap_info_t info;
    heap_caps_get_info(&info, cap);
    size_t total_size = info.total_free_bytes + info.total_allocated_bytes;
    /* Print heap trace summary */
    ESP_LOGI(TAG, "== Start of Heap trace summary [%s](%s) ==", tag, cap_name);
    // print all relevant information
    ESP_LOGI(TAG, "->  total size of heap: %" PRIu32 " byte(s)", (uint32_t) total_size);
    ESP_LOGI(TAG, "--------------------------------");
    ESP_LOGI(TAG, "->      allocated size: %" PRIu32 " byte(s) (%.2f%%)", (uint32_t) info.total_allocated_bytes, (double)info.total_allocated_bytes / total_size * 100);
    ESP_LOGI(TAG, "-> remaining free size: %" PRIu32 " byte(s) (%.2f%%)", (uint32_t) info.total_free_bytes, (double)info.total_free_bytes / total_size * 100);
    ESP_LOGI(TAG, "--------------------------------");
    ESP_LOGI(TAG, "->  largest block size: %" PRIu32 " byte(s)", (uint32_t) info.largest_free_block);
    ESP_LOGI(TAG, "->    lowest free size: %" PRIu32 " byte(s)", (uint32_t) info.minimum_free_bytes);

    ESP_LOGI(TAG, "== End of heap trace summary [%s](%s) ==", tag, cap_name);
}

void osal_heap_monitor_common_print_status(const char *tag)
{
    /* Get heap trace summary */
    __print_heap_trace_summary(tag, "internal", MALLOC_CAP_INTERNAL);
#if defined(CONFIG_SPIRAM)
    __print_heap_trace_summary(tag, "SPIRAM", MALLOC_CAP_SPIRAM);
#endif
}

/* Accumulate one region's totals. MALLOC_CAP_INTERNAL and MALLOC_CAP_SPIRAM
 * address disjoint memory, so summing across them gives a true device-wide
 * total without double-counting (which MALLOC_CAP_DEFAULT would risk when
 * SPIRAM is folded into the default heap). largest_block is reduced with
 * max() - a single allocation can't span regions, so the meaningful figure
 * is the biggest contiguous block available in any one region. */
static void __accumulate_heap_status(osal_heap_monitor_common_status_t *status, uint32_t cap)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, cap);
    status->total_size += info.total_free_bytes + info.total_allocated_bytes;
    status->allocated_size += info.total_allocated_bytes;
    status->free_size += info.total_free_bytes;
    if (info.largest_free_block > status->largest_block_size) {
        status->largest_block_size = info.largest_free_block;
    }
    status->lowest_free_size += info.minimum_free_bytes;
}

bool osal_heap_monitor_common_get_status(osal_heap_monitor_common_status_t *status)
{
    /* Get heap trace summary */
    status->total_size = 0;
    status->allocated_size = 0;
    status->free_size = 0;
    status->largest_block_size = 0;
    status->lowest_free_size = 0;
    /* Sum internal + SPIRAM (disjoint regions) for a device-wide figure.
     * Internal only when SPIRAM is absent. */
    __accumulate_heap_status(status, MALLOC_CAP_INTERNAL);
#if defined(CONFIG_SPIRAM)
    __accumulate_heap_status(status, MALLOC_CAP_SPIRAM);
#endif
    return true;
}
