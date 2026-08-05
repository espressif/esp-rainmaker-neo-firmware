/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_timesync.h
 * @brief Common interface for time synchronization.
 */

#ifndef __OSAL_TIMESYNC_H__
#define __OSAL_TIMESYNC_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <sys/time.h>

#include "osal_event_loop.h"

/**
 * @brief Event loop registration information
 */
typedef struct {
    osal_event_base_t event_base;
    struct {
        int32_t tz_posix_changed;
        int32_t tz_changed;
    } event_ids;
} osal_timesync_event_loop_registration_info_t;

/**
 * @brief Time synchronization configuration
 */
typedef struct {
    /** SNTP/NTP server name. If not specified, default server is used. */
    char *server_name;
    /** Optional callback to invoke whenever time is synchronized.
     * This will be called periodically as per the sync polling interval.
     * If kept NULL, the default callback will be invoked, which will just print the
     * current local time.
     */
    void (*sync_time_cb)(struct timeval *tv);

    /** Event loop registration information */
    osal_timesync_event_loop_registration_info_t event_loop_registration_info;
} osal_timesync_config_t;

/**
 * @brief Timezone change callback
 *
 * @param[in] tz new timezone string. NULL if not available.
 * @param[in] tz_posix new POSIX timezone string
 */
typedef void (*osal_timesync_timezone_change_cb)(const char *tz, const char *tz_posix);

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize time synchronization
 *
 * This API initializes time synchronization (SNTP for ESP, NTP for POSIX).
 *
 * @param[in] config Configuration to be used for time synchronization.
 *                   The default configuration is used if NULL is passed.
 *
 * @return 0 on success
 * @return negative error code on failure
 */
int osal_timesync_init(osal_timesync_config_t *config);

/**
 * @brief Deinitialize time synchronization
 *
 * This API deinitializes time synchronization.
 *
 * @return 0 on success
 * @return negative error code on failure
 */
int osal_timesync_deinit(void);

/**
 * @brief Check if time synchronization is initialized
 *
 * @return true if time synchronization is initialized
 * @return false if time synchronization is not initialized
 */
bool osal_timesync_is_initialized(void);

/**
 * @brief Check if current time is updated
 *
 * This API checks if the current system time is more recent than the reference time,
 * which is set at CMake compile time.
 *
 * @return true if time is updated
 * @return false if time is not updated
 */
bool osal_timesync_is_synced(void);

/**
 * @brief Check whether an epoch-ms value denotes a valid (plausible) wall clock
 *
 * Applies the same reference-time floor. Use to validate a coarse time before applying it,
 * or to validate a stored timestamp before trusting it - independent of the current system clock.
 *
 * @param[in] epoch_ms Candidate time, in milliseconds since the Unix epoch (UTC).
 *
 * @return true if epoch_ms is after the reference time
 * @return false otherwise (including non-positive values)
 */
bool osal_timesync_epoch_ms_is_valid(int64_t epoch_ms);

/**
 * @brief Wait for time synchronization
 *
 * This API waits for the system time to be synchronized.
 * This is a blocking call.
 *
 * @param[in] timeout_ms Timeout in milliseconds to wait for time synchronization.
 *                       0 means wait indefinitely.
 *
 * @return 0 on success
 * @return negative error code on timeout/failure
 */
int osal_timesync_wait_for_sync(uint32_t timeout_ms);

/**
 * @brief Set the system time
 *
 * Sets the system wall-clock time from an epoch value, e.g. one received
 * from the cloud. Intended as a coarse, best-effort time source whose
 * accuracy is bounded by delivery latency; SNTP (where enabled) remains
 * authoritative and steps the clock on its next sync.
 *
 * @param[in] epoch_ms Time to set, in milliseconds since the Unix epoch (UTC).
 *
 * @return 0 on success
 * @return negative error code on failure (e.g. invalid value, or
 *         insufficient privileges on POSIX hosts)
 */
int osal_timesync_set_time(int64_t epoch_ms);

/**
 * @brief Set POSIX timezone
 *
 * Set the timezone (TZ environment variable) as per the POSIX format
 * specified in the [GNU libc documentation](https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html).
 * Eg. For China: "CST-8"
 *     For US Pacific Time (including daylight saving information): "PST8PDT,M3.2.0,M11.1.0"
 *
 * @param[in] tz_posix NULL terminated TZ POSIX string
 *
 * @return 0 on success
 * @return negative error code on failure
 */
int osal_timesync_set_timezone_posix(const char *tz_posix);

/**
 * @brief Set timezone location string
 *
 * Set the timezone as a user friendly location string.
 * Check [here](https://rainmaker.espressif.com/docs/time-service.html) for a list of valid values.
 *
 * Eg. For China: "Asia/Shanghai"
 *     For US Pacific Time: "America/Los_Angeles"
 *
 * @note Setting timezone using this API internally also sets the POSIX timezone string.
 *
 * @param[in] tz NULL terminated Timezone location string
 *
 * @return 0 on success
 * @return negative error code on failure
 */
int osal_timesync_set_timezone(const char *tz);

/**
 * @brief Get the current POSIX timezone
 *
 * This fetches the current timezone in POSIX format.
 *
 * @return Pointer to a NULL terminated POSIX timezone string on success.
 *         Freeing this is the responsibility of the caller.
 * @return NULL on failure.
 */
char *osal_timesync_get_timezone_posix(void);

/**
 * @brief Get the current timezone
 *
 * This fetches the current timezone location string.
 *
 * @return Pointer to a NULL terminated timezone string on success.
 *         Freeing this is the responsibility of the caller.
 * @return NULL on failure.
 */
char *osal_timesync_get_timezone(void);

/**
 * @brief Get printable local time string
 *
 * Get a printable local time string, with information of timezone and Daylight Saving.
 * Eg. "Tue Sep  1 09:04:38 2020 -0400[EDT], DST: Yes"
 * "Tue Sep  1 21:04:04 2020 +0800[CST], DST: No"
 *
 * @param[out] buf Pointer to a pre-allocated buffer into which the time string will
 *                 be populated.
 * @param[in] buf_len Length of the above buffer.
 *
 * @return 0 on success
 * @return negative error code on failure
 */
int osal_timesync_get_local_time_str(char *buf, size_t buf_len);

/**
 * @brief Print current time.
 *
 * @return 0 on success, negative on error
 */
int osal_timesync_print_current_time(void);

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_TIMESYNC_H__ */
