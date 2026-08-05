/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file time_posix.c
 * @brief POSIX implementation of the timesync common API
 */

#include "osal_timesync_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#include "osal_log.h"
#include "osal_storage.h"

static const char *TAG = "osal_time_posix";

// Global state
static struct {
    bool initialized;
    bool synced;
    osal_timesync_config_t config;
} g_posix_state = {0};

// Private API implementations
osal_timesync_setenv_func osal_timesync_setenv = setenv;

// Public API implementations
int osal_timesync_init(osal_timesync_config_t *config)
{
    if (g_posix_state.initialized) {
        return 0; // Already initialized
    }

    // Store configuration
    if (config) {
        g_posix_state.config = *config;

        if (osal_timesync_event_loop_init(&config->event_loop_registration_info) != 0) {
            OSAL_LOGE(TAG, "Failed to initialize event loop posting information");
            return -1;
        }
    } else {
        memset(&g_posix_state.config, 0, sizeof(g_posix_state.config));
    }

    // POSIX systems typically already have synchronized time
    // We just need to check if the system time is reasonable
    g_posix_state.synced = osal_timesync_time_is_valid();
    g_posix_state.initialized = true;

    if (osal_timesync_timezone_init() != 0) {
        OSAL_LOGE(TAG, "Failed to initialize timezone");
        return -1;
    }

    OSAL_LOGI(TAG, "POSIX timesync initialized, system time is %s",
              g_posix_state.synced ? "valid" : "invalid");

    return 0;
}

int osal_timesync_deinit(void)
{
    if (!g_posix_state.initialized) {
        return 0;
    }
    g_posix_state.initialized = false;
    return 0;
}

bool osal_timesync_is_initialized(void)
{
    return g_posix_state.initialized;
}

bool osal_timesync_is_synced(void)
{
    // Pure wall-clock validity check, matching the ESP impl. NOT gated on
    // this SDK having initialized timesync: the clock may be valid via an
    // external SNTP owner (enable_time_sync=false), and the decoupled-flow
    // consumers (schedule arming, the timeseries guard, getTimeSync) all
    // treat "synced" as "the wall clock is valid".
    return osal_timesync_time_is_valid();
}

int osal_timesync_wait_for_sync(uint32_t timeout_ms)
{
    if (!g_posix_state.initialized) {
        return -1;
    }

    // For POSIX, we just check if time is valid immediately
    // The OS is responsible for time synchronization
    if (osal_timesync_is_synced()) {
        osal_timesync_print_current_time();
        return 0;
    }

    // If time is not valid, we can't really do much on POSIX
    // The system administrator should ensure NTP is configured
    OSAL_LOGW(TAG, "System time appears invalid. Please check NTP configuration.");
    return -1;
}

// Cleanup function (not in public API but useful)
void osal_timesync_cleanup(void)
{
    if (!g_posix_state.initialized) {
        return;
    }

    // Simple cleanup - just reset state
    memset(&g_posix_state, 0, sizeof(g_posix_state));
    OSAL_LOGI(TAG, "POSIX timesync cleanup completed");
}
