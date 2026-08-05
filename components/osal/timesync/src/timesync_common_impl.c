/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file timesync_common_impl.c
 * @brief Implementation of the timesync common API
 */

/** Includes ***************************************/

#include "osal_timesync_internal.h"

// Standard library includes
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>

// Platform-agnostic includes using platform-common and nvs-common
#include "osal_time.h"
#include "osal_log.h"
#include "osal_task.h"
#include "osal_ticks.h"
#include "osal_semaphore.h"
#include "osal_mem_alloc.h"
#include "osal_event_loop.h"
#include "osal_storage.h"

/** Variables ***************************************/

static const char *TAG = "osal_time_common";

static osal_timesync_event_loop_registration_info_t xEventLoopRegistrationInfo;

/* Private function declarations ***************************************/

/**
 * @brief post a timezone change event.
 * @param[in] tz the new timezone string. NULL if not available.
 * @param[in] tz_posix the new POSIX timezone string.
 */
static void __post_timezone_change(const char *tz, const char *tz_posix);

/** Private function definitions ***************************************/

static void __post_timezone_change(const char *tz, const char *tz_posix)
{
    if (tz != NULL) {
        osal_event_post(xEventLoopRegistrationInfo.event_base, xEventLoopRegistrationInfo.event_ids.tz_changed, (char *) tz, strlen(tz) + 1, OSAL_MAX_DELAY);
    }
    if (tz_posix != NULL) {
        osal_event_post(xEventLoopRegistrationInfo.event_base, xEventLoopRegistrationInfo.event_ids.tz_posix_changed, (char *) tz_posix, strlen(tz_posix) + 1, OSAL_MAX_DELAY);
    }
}

/** Function definitions ***************************************/

int osal_timesync_event_loop_init(osal_timesync_event_loop_registration_info_t *event_loop_registration_info)
{
    if (event_loop_registration_info == NULL) {
        return -1;
    }
    xEventLoopRegistrationInfo = *event_loop_registration_info;
    return 0;
}

int osal_timesync_timezone_init(void)
{
    // Initialize NVS first
    osal_storage_init(NULL);

    char *tz_posix = osal_timesync_get_timezone_posix();
    if (tz_posix) {
        int ret = osal_timesync_set_timezone_posix(tz_posix);
        free(tz_posix);
        return ret;
    }
#ifdef OSAL_TIMESYNC_DEFAULT_TZ
    else if (strlen(OSAL_TIMESYNC_DEFAULT_TZ) > 0) {
        return osal_timesync_set_timezone(OSAL_TIMESYNC_DEFAULT_TZ);
    }
#endif
    return -1;
}

int osal_timesync_storage_get_string(const char *key, char **value)
{
    if (!key || !value) {
        OSAL_LOGE(TAG, "Invalid key or value pointer");
        return -1;
    }
    *value = NULL;
    osal_storage_handle_t handle;
    // use default partition
    osal_err_t err = osal_storage_open(NULL, OSAL_TIMESYNC_NVS_NAMESPACE, OSAL_STORAGE_OPEN_READONLY, &handle);
    if (err != OSAL_ERR_OK) {
        return -1;
    }

    size_t len = 0;
    // First get the length
    err = osal_storage_get(handle, key, NULL, &len, OSAL_STORAGE_TYPE_BINARY);
    if (err == OSAL_ERR_OK && len > 0) {
        *value = OSAL_CALLOC_EXTRAM(1, len + 1); /* +1 for NULL termination */
        if (*value) {
            osal_storage_get(handle, key, *value, &len, OSAL_STORAGE_TYPE_BINARY);
        }
    }
    osal_storage_close(handle);
    return (*value) ? 0 : -1;
}

int osal_timesync_storage_set_string(const char *key, const char *value)
{
    if (!key || !value) {
        OSAL_LOGE(TAG, "Invalid key or value");
        return -1;
    }

    osal_storage_handle_t handle;
    // use default partition
    osal_err_t err = osal_storage_open(NULL, OSAL_TIMESYNC_NVS_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &handle);
    if (err != OSAL_ERR_OK) {
        return -1;
    }

    err = osal_storage_set(handle, key, value, strlen(value), OSAL_STORAGE_TYPE_BINARY);
    if (err == OSAL_ERR_OK) {
        osal_storage_commit(handle);
    }
    osal_storage_close(handle);
    return (err == OSAL_ERR_OK) ? 0 : -1;
}

bool osal_timesync_time_is_valid(void)
{
    /* Use the real clock; osal_get_time is not used here because it may be overridden by the virtual scheduler */
    time_t now = time(NULL);
    return osal_timesync_epoch_ms_is_valid(now * 1000);
}

bool osal_timesync_epoch_ms_is_valid(int64_t epoch_ms)
{
    /* Same reference-time floor as osal_timesync_time_is_valid(), but applied to a
     * caller-supplied instant rather than the system clock. Non-positive
     * values fold to <= 0, which is below the floor, so they return false. */
    return (epoch_ms / 1000) > (int64_t)OSAL_TIMESYNC_REF_TIME;
}

int osal_timesync_print_current_time(void)
{
    char local_time[128];
    if (osal_timesync_get_local_time_str(local_time, sizeof(local_time)) == 0) {
        if (!osal_timesync_time_is_valid()) {
            OSAL_LOGI(TAG, "Time not synchronised yet.");
        }
        OSAL_LOGI(TAG, "The current time is: %s.", local_time);
        return 0;
    }
    return -1;
}

int osal_timesync_set_time(int64_t epoch_ms)
{
    /* Reject anything that would leave the clock invalid:
     * stepping the clock to garbage helps nothing. */
    if (!osal_timesync_epoch_ms_is_valid(epoch_ms)) {
        return -1;
    }
    struct timeval tv = {
        .tv_sec = (time_t)(epoch_ms / 1000),
        .tv_usec = (suseconds_t)((epoch_ms % 1000) * 1000),
    };
    if (settimeofday(&tv, NULL) != 0) {
        OSAL_LOGW(TAG, "Failed to set system time (errno %d)", errno);
        return -1;
    }
    osal_timesync_print_current_time();
    return 0;
}

static int __set_timezone_posix_no_post(const char *tz_posix)
{
    if (!tz_posix) {
        return -1;
    }

    int err = osal_timesync_storage_set_string(OSAL_TIMESYNC_TZ_POSIX_KEY, tz_posix);
    if (err == 0) {
        osal_timesync_setenv("TZ", tz_posix, 1);
        tzset();
        osal_timesync_print_current_time();
    }

    return err;
}

int osal_timesync_set_timezone_posix(const char *tz_posix)
{
    int err = __set_timezone_posix_no_post(tz_posix);
    if (err != 0) {
        return err;
    }
    __post_timezone_change(NULL, tz_posix);
    return 0;
}

int osal_timesync_set_timezone(const char *tz)
{
    if (!tz) {
        return -1;
    }

    const char *tz_posix = osal_timesync_tz_db_get_posix_str(tz);
    if (!tz_posix) {
        OSAL_LOGE(TAG, "Unable to set timezone '%s' - not found in database.", tz);
        return -1;
    }

    int err = __set_timezone_posix_no_post(tz_posix);
    if (err == 0) {
        err = osal_timesync_storage_set_string(OSAL_TIMESYNC_TZ_KEY, tz);
        __post_timezone_change(tz, tz_posix);
    }
    return err;
}

char *osal_timesync_get_timezone_posix(void)
{
    char *value = NULL;
    osal_timesync_storage_get_string(OSAL_TIMESYNC_TZ_POSIX_KEY, &value);
    return value;
}

char *osal_timesync_get_timezone(void)
{
    char *value = NULL;
    osal_timesync_storage_get_string(OSAL_TIMESYNC_TZ_KEY, &value);
    return value;
}

int osal_timesync_get_local_time_str(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) {
        return -1;
    }

    struct tm timeinfo;
    char strftime_buf[64];
    time_t now = osal_get_time(NULL);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c %z[%Z]", &timeinfo);

    size_t print_size = snprintf(buf, buf_len, "%s, DST: %s",
                                 strftime_buf, timeinfo.tm_isdst ? "Yes" : "No");
    if (print_size >= buf_len) {
        OSAL_LOGE(TAG, "Buffer size %d insufficient for localtime string. Required size: %d",
                  (int)buf_len, (int)print_size);
        return -1;
    }
    return 0;
}
