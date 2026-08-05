/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

/**
 * @file app_main.c
 * @brief Unified (ESP-IDF + POSIX) device simulator.
 *
 * A serial/remote-driven RainMaker Neo node used by the integration-test harness. It
 * brings the SDK up and hands control to the RainMaker Neo host control task; commands then
 * arrive over the external-I/O transport rather than from application code, so
 * there is no device/param/driver here.
 *
 * The example implements only @c app_run() (and, for POSIX, an @c app_teardown()
 * override). The platform entry point and signal/pause boilerplate live in the
 * @c app_entry common component.
 */

/* App entry (owns app_main/main; provides app_run + APP_RETURN_ON_ERR + OSAL_LOGx) */
#include "app_entry.h"

/* Remote control */
#include "esp_rmaker_host_ctrl.h"

#if defined(ESP_PLATFORM)
/* ESP-only network bring-up. On POSIX the remote node needs no local network stack. */
#include "osal_storage.h"
#include "app_network_neo.h"
#endif /* ESP_PLATFORM */

/* Tag for logging */
static const char *TAG = "device-sim";

/* Portable startup. See the examples/common/app_entry component for how the
 * ESP-IDF and POSIX entry points invoke this. */
osal_err_t app_run(void)
{
    OSAL_LOGI(TAG, "Starting device-sim");

#if defined(ESP_PLATFORM)
    /* Bring up NVS and the network, then run the RMNG provisioning sequence:
     * 1. esp_rmaker_pre_prov_init()  - init SDK components required before provisioning
     * 2. app_network_start_neo()    - start the network and await provisioning completion
     * 3. esp_rmaker_pre_prov_deinit()- deinit those pre-provisioning components
     * POSIX has no local network stack, so the remote node skips all of this. */
    osal_storage_init(NULL);
    app_network_init();
    APP_RETURN_ON_ERR(esp_rmaker_pre_prov_init(), "Failed to initialise pre-provisioning components");
    APP_RETURN_ON_ERR(app_network_start_neo(), "Failed to start the network and await provisioning");
    APP_RETURN_ON_ERR(esp_rmaker_pre_prov_deinit(), "Failed to deinitialise pre-provisioning components");
#endif /* ESP_PLATFORM */

    /* Initialise and start the remote control. */
    esp_rmaker_config_t config = {
        .enable_time_sync = true,
    };
    APP_RETURN_ON_ERR(esp_rmaker_host_ctrl_init(&config), "Failed to initialise remote control");
    APP_RETURN_ON_ERR(esp_rmaker_host_ctrl_start(), "Failed to start remote control");

    OSAL_LOGI(TAG, "device-sim ready.");
    return OSAL_ERR_OK;
}

#if !defined(ESP_PLATFORM)
void app_teardown(void)
{
    if (esp_rmaker_host_ctrl_stop() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to stop remote control");
    }
    if (esp_rmaker_host_ctrl_deinit() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to deinitialise remote control");
    }
    OSAL_LOGI(TAG, "Exiting device-sim");
}
#endif /* !ESP_PLATFORM */
