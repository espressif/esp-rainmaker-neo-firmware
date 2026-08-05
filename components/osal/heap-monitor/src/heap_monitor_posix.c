/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file heap_monitor_posix.c
 * @brief Heap monitor common implementation for POSIX.
 * @note This implementation is not available for POSIX, so only stub functions are provided.
 */

#include "osal_heap_monitor.h"

void osal_heap_monitor_common_print_status(const char *tag)
{
    return;
}

bool osal_heap_monitor_common_get_status(osal_heap_monitor_common_status_t *status)
{
    if (!status) {
        return false;
    }
    /* There is no heap instrumentation on the host. Zero the struct so a caller that
     * ignores the return value cannot read uninitialised memory, but report failure:
     * returning success for an all-zero reading makes "no data" indistinguishable from
     * a genuinely empty heap, and the host-control heap command would answer OK with
     * five zeroes. */
    status->total_size = 0;
    status->allocated_size = 0;
    status->free_size = 0;
    status->largest_block_size = 0;
    status->lowest_free_size = 0;
    return false;
}
