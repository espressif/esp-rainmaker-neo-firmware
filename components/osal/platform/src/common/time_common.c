/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file time_common.c
 * @brief Default time provider implementation
 */

#include "osal_time.h"
#include <time.h>
#include <sys/time.h>
#include <stdint.h>

// Map to the standard time function
osal_time_func osal_get_time = time;

// Map to standard gettimeofday function
static uint64_t __get_time_ms(uint64_t *time_ms)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t t = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    if (time_ms) {
        *time_ms = t;
    }
    return t;
}
osal_time_ms_func osal_get_time_ms = __get_time_ms;
