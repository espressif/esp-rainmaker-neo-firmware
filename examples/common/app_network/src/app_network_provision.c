/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_network_provision.c
 * @brief ESP-IDF network bring-up: wraps Wi-Fi/Thread provisioning and the RMNG
 *        pre-provisioning flow into a single app_network_provision() call.
 */

/* Declarations */
#include "app_network_neo.h"

/* RMNG includes */
#include "esp_rmaker_flow.h"
#include "esp_rmaker_error_types.h"

/* ESP-IDF includes */
#include "esp_event.h"
#include "network_provisioning/manager.h"

/* Platform includes */
#include "osal_log.h"

static const char *TAG = "app_network";

/* Optional example-supplied provisioning-lifecycle callbacks (see app_network_neo.h). */
static app_network_prov_hooks_t s_prov_hooks;

void app_network_set_prov_hooks(const app_network_prov_hooks_t *hooks)
{
    if (hooks) {
        s_prov_hooks = *hooks;
    } else {
        s_prov_hooks = (app_network_prov_hooks_t) {
            0
        };
    }
}

/* Bridges the ESP-only NETWORK_PROV_START event to the portable on_session_start
 * hook, so the example never has to register for the event itself. */
static void prov_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void) arg;
    (void) data;
    if (base == NETWORK_PROV_EVENT && id == NETWORK_PROV_START && s_prov_hooks.on_session_start) {
        s_prov_hooks.on_session_start();
    }
}

osal_err_t app_network_provision(uint16_t device_type, uint8_t device_subtype)
{
    /* 1. Initialise the SDK components required before provisioning. */
    if (esp_rmaker_pre_prov_init() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialise pre-provisioning components");
        return OSAL_ERR_FAIL;
    }

    /* 2. Set device-specific manufacturer data (optional, advertised over BLE).
     *    Skipped when the caller passes NEO_MFG_DATA_DEVICE_TYPE_NONE. */
    osal_err_t err = OSAL_ERR_OK;
    if (device_type != NEO_MFG_DATA_DEVICE_TYPE_NONE) {
        err = app_network_set_custom_mfg_data_neo(device_type, device_subtype);
        if (err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to set custom manufacturer data");
            return err;
        }
    }

    /* Provisioning-lifecycle feedback (opt-in): announce bring-up and listen for
     * the session-start event before the (blocking) network start. */
    if (s_prov_hooks.on_begin) {
        s_prov_hooks.on_begin();
    }
    esp_event_handler_register(NETWORK_PROV_EVENT, NETWORK_PROV_START, &prov_event_handler, NULL);

    /* 3. Start the network and await provisioning completion. */
    err = app_network_start_neo();

    esp_event_handler_unregister(NETWORK_PROV_EVENT, NETWORK_PROV_START, &prov_event_handler);
    if (s_prov_hooks.on_end) {
        s_prov_hooks.on_end();
    }

    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to start the network");
        return err;
    }

    /* 4. Deinitialise the SDK components used only for provisioning. */
    if (esp_rmaker_pre_prov_deinit() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to deinitialise pre-provisioning components");
        return OSAL_ERR_FAIL;
    }

    return OSAL_ERR_OK;
}
