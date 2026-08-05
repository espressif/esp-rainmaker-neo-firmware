/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_timesync_internal.h
 * @brief Internal declarations for the timesync common API
 */

#ifndef __TIMESYNC_INTERNAL_H__
#define __TIMESYNC_INTERNAL_H__

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "osal_timesync.h"
#include "osal_timesync_ref_time.h"

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// Internal constants
#define OSAL_TIMESYNC_DEFAULT_TZ         CONFIG_OSAL_TIMESYNC_DEFAULT_TZ

// Storage keys for persistent data
#define OSAL_TIMESYNC_NVS_NAMESPACE      "timesync"
#define OSAL_TIMESYNC_TZ_POSIX_KEY       "tz_posix"
#define OSAL_TIMESYNC_TZ_KEY             "tz"

// Standard library includes
#include <stdbool.h>

// Internal function declarations

typedef int (*osal_timesync_setenv_func)(const char *name, const char *value, int rewrite);
/**
 * @brief Set environment variable
 * @param[in] name Environment variable name
 * @param[in] value Environment variable value
 * @param[in] rewrite Whether to overwrite existing value
 * @return 0 on success, negative on error
 */
extern osal_timesync_setenv_func osal_timesync_setenv;

/**
 * @brief Initialize timezone storage and initial timezone
 *
 * @return 0 on success, negative on error
 */
int osal_timesync_timezone_init(void);

/**
 * @brief Initialize event loop registration information
 * @param[in] event_loop_registration_info Event loop registration information
 * @return 0 on success, negative on error
 */
int osal_timesync_event_loop_init(osal_timesync_event_loop_registration_info_t *event_loop_registration_info);

/**
 * @brief Storage operations using nvs-common interface
 */
int osal_timesync_storage_get_string(const char *key, char **value);
int osal_timesync_storage_set_string(const char *key, const char *value);

/**
 * @brief Get timezone POSIX string from timezone database
 *
 * @param[in] name Timezone name (e.g., "America/Los_Angeles")
 * @return POSIX timezone string or NULL if not found
 */
const char *osal_timesync_tz_db_get_posix_str(const char *name);

/**
 * @brief Check if time is valid (after reference time)
 *
 * @return true if time is valid, false otherwise
 */
bool osal_timesync_time_is_valid(void);

#ifdef __cplusplus
}
#endif

#endif /* __TIMESYNC_INTERNAL_H__ */
