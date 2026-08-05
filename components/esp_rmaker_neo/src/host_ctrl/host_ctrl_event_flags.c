/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file host_ctrl_event_flags.c
 * @brief Event flags for the host control.
 */

#include "esp_rmaker_error_types.h"
#include "osal_event_group.h"
#include "osal_semaphore.h"
#include "osal_log.h"
#include "event_flags.h"

#include <inttypes.h>

static const char *TAG = "rmng_hc_event_flags";
static osal_event_group_handle_t s_event_group = NULL;
static osal_semaphore_handle_t s_event_group_mutex = NULL;

static void __event_group_lock(void)
{
    osal_semaphore_take(s_event_group_mutex, OSAL_MAX_DELAY);
}

static void __event_group_unlock(void)
{
    osal_semaphore_give(s_event_group_mutex);
}

esp_rmaker_error_t esp_rmaker_event_flags_init(void)
{
    s_event_group_mutex = osal_semaphore_create_mutex();
    if (s_event_group_mutex == NULL) {
        return ESP_RMAKER_NO_MEM;
    }
    s_event_group = osal_event_group_create();
    if (s_event_group == NULL) {
        return ESP_RMAKER_NO_MEM;
    }
    esp_rmaker_event_flags_clear_all();
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_event_flags_deinit(void)
{
    osal_event_group_delete(s_event_group);
    osal_semaphore_delete(s_event_group_mutex);
    return ESP_RMAKER_OK;
}

void esp_rmaker_event_flags_set(osal_event_group_bits_t flags)
{
    OSAL_LOGD(TAG, "Setting event flags: %" PRIx32, (uint32_t) flags);
    __event_group_lock();
    osal_event_group_set_bits(s_event_group, flags);
    __event_group_unlock();
}

void esp_rmaker_event_flags_clear(osal_event_group_bits_t flags)
{
    OSAL_LOGD(TAG, "Clearing event flags: %" PRIx32, (uint32_t) flags);
    __event_group_lock();
    osal_event_group_clear_bits(s_event_group, flags);
    __event_group_unlock();
}

esp_rmaker_error_t esp_rmaker_event_flags_wait(osal_event_group_bits_t flags, uint32_t timeout_ms)
{
    osal_event_group_bits_t bits = osal_event_group_wait_bits(s_event_group, flags, false, true, osal_ticks_from_ms(timeout_ms));
    if ((bits & flags) != flags) {
        // assume that the flags were not set because of a timeout
        return ESP_RMAKER_TIMEOUT;
    }

    /* Clear all applicable flags */
    osal_event_group_bits_t ignore_flags = ESP_RMAKER_EVENT_FLAGS_ONLINE;
    osal_event_group_bits_t clear_bits = flags & ~ignore_flags;
    esp_rmaker_event_flags_clear(clear_bits);
    return ESP_RMAKER_OK;
}
