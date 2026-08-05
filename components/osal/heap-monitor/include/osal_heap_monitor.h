/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_heap_monitor.h
 * @brief Heap monitor common header file.
 */

#ifndef __HEAP_MONITOR_COMMON_H__
#define __HEAP_MONITOR_COMMON_H__

#include <stddef.h>
#include <stdbool.h>

/** Snapshot of heap usage */
typedef struct {
    size_t total_size; /**< Total size of the heap in bytes. */
    size_t allocated_size; /**< Allocated size of the heap in bytes. */
    size_t free_size; /**< Free size of the heap in bytes. */
    size_t largest_block_size; /**< Largest block size of the heap in bytes. */
    size_t lowest_free_size; /**< Lowest free size of the heap in bytes. */
} osal_heap_monitor_common_status_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Print the current heap status.
 * @param[in] tag The tag to print the heap status for.
 */
void osal_heap_monitor_common_print_status(const char *tag);

/**
 * @brief Get the current heap status.
 * @note Not available on POSIX: there is no heap instrumentation on the host, so this
 * zeroes @p status and returns false there. Callers must check the return value rather
 * than read the struct unconditionally.
 * @param[out] status Pointer to a osal_heap_monitor_common_status_t structure to store the current heap status.
 * @return True if the heap status was successfully retrieved, false otherwise.
 */
bool osal_heap_monitor_common_get_status(osal_heap_monitor_common_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* __HEAP_MONITOR_COMMON_H__ */
