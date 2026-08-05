/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_time.h
 * @brief Platform common time provider.
 */

#ifndef __OSAL_TIME_H__
#define __OSAL_TIME_H__

#include <time.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Function providing the current time in seconds, in the style of time().
 *
 * @param[out] time If non-NULL, the current time is also stored here.
 * @return The current time, in seconds since the Unix epoch.
 */
typedef time_t (*osal_time_func)(time_t *time);

/**
 * @brief Function providing the current time in milliseconds.
 *
 * @param[out] time_ms If non-NULL, the current time is also stored here.
 * @return The current time, in milliseconds since the Unix epoch.
 */
typedef uint64_t (*osal_time_ms_func)(uint64_t *time_ms);

/** Get the current time. See ::osal_time_func for the calling contract. */
extern osal_time_func osal_get_time;

/** Get the current time in milliseconds. See ::osal_time_ms_func for the calling contract. */
extern osal_time_ms_func osal_get_time_ms;

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_TIME_H__ */
