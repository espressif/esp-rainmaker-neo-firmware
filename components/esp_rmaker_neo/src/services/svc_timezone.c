/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file svc_timezone.c
 * @brief Timezone service
 */

/* Includes *******************************************************/

/* Declarations */
#include "esp_rmaker_standard_services.h"
#include "services/standard_creation.h"

/* Standard C headers */
#include <string.h>
#include <inttypes.h>

/* Platform common includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Timesync includes */
#include "osal_timesync.h"

/* RMNG includes */
#include "event_loop.h"

/* Variables *******************************************************/

/**
 * @brief Tag for the timezone service
 */
static const char *TAG = "rmng_svc_timezone";

/**
 * @brief Timezone service callbacks
 */
static esp_rmaker_timezone_service_callbacks_t __timezone_service_callbacks = {
    .timezone_local_change_cb = NULL,
    .timezone_posix_local_change_cb = NULL,
};

/* Private function declarations *******************************************************/

/**
 * @brief Timezone change event handler
 * @param[in] event_handler_arg Event handler argument
 * @param[in] event_base Event base
 * @param[in] event_id Event ID
 * @param[in] event_data Event data
 */
static void __on_timezone_change_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data);

/* Private function definitions *******************************************************/

static void __on_timezone_change_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    const char *data = (const char *)event_data;
    OSAL_LOGD(TAG, "Timezone change event received: %" PRId32 " | %s", event_id, data);
    if (event_id == RMAKER_EVENT_TZ_CHANGED) {
        if (__timezone_service_callbacks.timezone_local_change_cb) {
            __timezone_service_callbacks.timezone_local_change_cb(data);
        }
    }
    if (event_id == RMAKER_EVENT_TZ_POSIX_CHANGED) {
        if (__timezone_service_callbacks.timezone_posix_local_change_cb) {
            __timezone_service_callbacks.timezone_posix_local_change_cb(data);
        }
    }
}

/* Public functions *******************************************************/

esp_rmaker_error_t esp_rmaker_timezone_service_enable(void)
{
    /* Get timezone strings from timesync */
    char *timezone = osal_timesync_get_timezone();
    char *timezone_posix = osal_timesync_get_timezone_posix();
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    if (!timezone || !timezone_posix) {
        OSAL_LOGE(TAG, "Failed to get timezone strings");
        goto esp_rmaker_timezone_service_enable_end;
    }

    /* Create timezone service */
    err = esp_rmaker_timezone_service_add_to_node(timezone, timezone_posix, &__timezone_service_callbacks);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to create timezone service");
        goto esp_rmaker_timezone_service_enable_end;
    }

    /* Register timezone change event handler */
    err = event_loop_register_timezone_change_handler(__on_timezone_change_event_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to register timezone change event handler");
        goto esp_rmaker_timezone_service_enable_end;
    }

esp_rmaker_timezone_service_enable_end:
    if (timezone) {
        free(timezone);
    }
    if (timezone_posix) {
        free(timezone_posix);
    }
    return err;
}

esp_rmaker_error_t esp_rmaker_timezone_service_disable(void)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    err = esp_rmaker_timezone_service_remove_from_node();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to remove timezone service from node");
        return err;
    }
    event_loop_unregister_timezone_change_handler(__on_timezone_change_event_handler);
    return ESP_RMAKER_OK;
}
