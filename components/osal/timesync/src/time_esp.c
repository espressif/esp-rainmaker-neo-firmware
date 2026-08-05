/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file time_esp.c
 * @brief ESP-IDF implementation of the timesync common API
 */

#include "osal_timesync_internal.h"

#include <esp_idf_version.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#include <esp_sntp.h>
#else
#include <lwip/apps/sntp.h>
#endif

#include <string.h>
#include <inttypes.h>

#include "osal_log.h"
#include "osal_task.h"

static const char *TAG = "osal_time_esp";
static const char *SNTP_SERVERS[] = {
    "time.google.com",
    "time.cloudflare.com",
    "pool.ntp.org",
    "time.windows.com",
};
#define SNTP_SERVER_COUNT (sizeof(SNTP_SERVERS) / sizeof(SNTP_SERVERS[0]))

// Global state
static bool init_done = false;

static void osal_timesync_sync_cb(struct timeval *tv)
{
    OSAL_LOGI(TAG, "SNTP Synchronised.");
    osal_timesync_print_current_time();
}

// Private API implementations
static int esp_setenv(const char *name, const char *value, int rewrite)
{
    return setenv(name, value, rewrite);
}
osal_timesync_setenv_func osal_timesync_setenv = esp_setenv;

// Public API implementations
int osal_timesync_init(osal_timesync_config_t *config)
{
    if (init_done) {
        OSAL_LOGI(TAG, "SNTP already initialized.");
        return 0;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    if (esp_sntp_enabled()) {
#else
    if (sntp_enabled()) {
#endif
        OSAL_LOGI(TAG, "SNTP already initialized.");
        init_done = true;
        return 0;
    }

    OSAL_LOGI(TAG, "Initializing SNTP.");

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    if (config && config->server_name) {
        esp_sntp_setservername(0, config->server_name);
    } else {
        for (size_t i = 0; i < SNTP_SERVER_COUNT; i++) {
            esp_sntp_setservername(i, SNTP_SERVERS[i]);
        }
    }
    esp_sntp_init();
#else
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    if (config && config->server_name) {
        sntp_setservername(0, config->server_name);
    } else {
        for (size_t i = 0; i < SNTP_SERVER_COUNT; i++) {
            sntp_setservername(i, SNTP_SERVERS[i]);
        }
    }
    sntp_init();
#endif

    if (config && config->sync_time_cb) {
        sntp_set_time_sync_notification_cb(config->sync_time_cb);
    } else {
        sntp_set_time_sync_notification_cb(osal_timesync_sync_cb);
    }

    if (config && osal_timesync_event_loop_init(&config->event_loop_registration_info) != 0) {
        OSAL_LOGE(TAG, "Failed to initialize event loop posting information");
        return -1;
    }

    if (osal_timesync_timezone_init() != 0) {
        OSAL_LOGE(TAG, "Failed to enable timezone");
        return -1;
    }

    init_done = true;
    return 0;
}

int osal_timesync_deinit(void)
{
    if (!init_done) {
        return 0;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    esp_sntp_stop();
#else
    sntp_stop();
#endif
    init_done = false;
    return 0;
}

bool osal_timesync_is_initialized(void)
{
    return init_done;
}

bool osal_timesync_is_synced(void)
{
    return osal_timesync_time_is_valid();
}

int osal_timesync_wait_for_sync(uint32_t timeout_ms)
{
    if (!init_done) {
        OSAL_LOGW(TAG, "Time sync not initialized");
        return -1;
    }

    OSAL_LOGW(TAG, "Waiting for time to be synchronized. This may take time.");

    uint32_t elapsed_ms = 0;
    const uint32_t check_interval_ms = 2000; // 2 seconds

    while (timeout_ms == 0 || elapsed_ms < timeout_ms) {
        if (osal_timesync_is_synced()) {
            osal_timesync_print_current_time();
            return 0;
        }

        OSAL_LOGD(TAG, "Time not synchronized yet. Retrying...");
        osal_task_delay(osal_ticks_from_ms(check_interval_ms));
        elapsed_ms += check_interval_ms;
    }

    OSAL_LOGE(TAG, "Time not synchronized within timeout: %" PRIu32 " ms", timeout_ms);
    return -1;
}
