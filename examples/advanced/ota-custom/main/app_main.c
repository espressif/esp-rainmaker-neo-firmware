/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/* ota-custom: fetches OTA jobs and drives a status LED through the OTA
 * lifecycle. Beyond the default firmware update it demonstrates two OTA
 * extension points via a sample "mock" implementation (see ota_custom_mock.c):
 * a custom-filetype handler and a custom (non-OTA) job callback. */

#include <inttypes.h>

#include "app_entry.h"

#include <osal_storage.h>
#include <app_network_neo.h>
#include <app_event_loop.h>
#include <app_led.h>

#include <esp_rmaker_core.h>
#include <esp_rmaker_system_ctrl.h>
#include <esp_rmaker_ota.h>

#include "ota_custom_mock.h"

/* Configuration includes */
#include "sdkconfig.h"

static const char *TAG = "app_main";

/* OTA status LED colours. */
static const struct {
    uint16_t idle;
    uint16_t in_progress;
    uint16_t success;
    uint16_t failed;
} __ota_led_hue = {
    .idle = 190,         /* light blue */
    .in_progress = 20,   /* orange */
    .success = 120,      /* green */
    .failed = 0,         /* red */
};

/* Boot LED: setup colour (white), on. */
static const app_led_state_t __ota_led_boot_state = {
    .power = true,
    .brightness = 30,
    .color_hs = {.hue = 0, .saturation = 0},
    .cct = 0,
    .mode = APP_LED_MODE_HSV,
};

static void __ota_led_apply_hue(uint16_t hue)
{
    (void) app_led_set_mode(APP_LED_MODE_HSV);
    (void) app_led_set_hue(hue);
    (void) app_led_set_saturation(100);
}

/* OTA event handler: logs each OTA lifecycle event and colours the status LED. */
static void __ota_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base != RMAKER_OTA_EVENT) {
        return;
    }

    const esp_rmaker_ota_status_details_t *status_details = (const esp_rmaker_ota_status_details_t *)event_data;
    uint32_t downloaded_bytes = 0;
    uint32_t total_bytes = 0;
    uint32_t percentage = 0;
    char *add_str = "unknown";
    char *add_str2 = "unknown";
    esp_rmaker_ota_error_reason_t error = OTA_ERROR_NONE;
    switch ((esp_rmaker_ota_event_t) event_id) {
    case RMAKER_OTA_EVENT_STARTING:
        if (status_details != NULL && status_details->type == ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_STARTING) {
            if (status_details->details.starting.fw_version != NULL) {
                add_str = (char *)status_details->details.starting.fw_version;
            }
            if (status_details->details.starting.filetype != NULL) {
                add_str2 = (char *)status_details->details.starting.filetype;
            } else {
                add_str2 = "default (firmware update)";
            }
        }
        OSAL_LOGI(TAG, "OTA starting: job filetype - %s, job firmware version - %s", add_str2, add_str);
        __ota_led_apply_hue(__ota_led_hue.in_progress);
        break;
    case RMAKER_OTA_EVENT_FETCH_REQUEST_IGNORED:
        OSAL_LOGW(TAG, "OTA fetch request ignored. Triggering a delayed fetch request in 10 seconds...");
        esp_rmaker_ota_fetch_with_delay(10);
        break;
    case RMAKER_OTA_EVENT_IN_PROGRESS:
        if (status_details != NULL && status_details->type == ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_IN_PROGRESS) {
            downloaded_bytes = status_details->details.in_progress.downloaded_bytes;
            total_bytes = status_details->details.in_progress.total_bytes;
            percentage = (total_bytes > 0) ? (downloaded_bytes * 100) / total_bytes : 0;
        }
        OSAL_LOGI(TAG, "OTA in progress: %" PRIu32 " / %" PRIu32 " bytes (%" PRIu32 "%%)", downloaded_bytes, total_bytes, percentage);
        __ota_led_apply_hue(__ota_led_hue.in_progress);
        break;
    case RMAKER_OTA_EVENT_SUCCESSFUL:
        if (status_details != NULL && status_details->type == ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_SUCCEEDED) {
            if (status_details->details.succeeded.fw_version != NULL) {
                add_str = (char *)status_details->details.succeeded.fw_version;
            }
            if (status_details->details.succeeded.filetype != NULL) {
                add_str2 = (char *)status_details->details.succeeded.filetype;
            } else {
                add_str2 = "default (firmware update)";
            }
        }
        OSAL_LOGI(TAG, "OTA successful: job filetype - %s, job firmware version - %s", add_str2, add_str);
        __ota_led_apply_hue(__ota_led_hue.success);
        break;
    case RMAKER_OTA_EVENT_FAILED:
        if (status_details != NULL && status_details->type == ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_FAILED) {
            add_str = (char *)status_details->details.failed.reason;
        }
        OSAL_LOGE(TAG, "OTA failed: %s", add_str);
        __ota_led_apply_hue(__ota_led_hue.failed);
        break;
    case RMAKER_OTA_EVENT_REJECTED:
        if (status_details != NULL && status_details->type == ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_REJECTED) {
            add_str = (char *)status_details->details.rejected.reason;
        }
        OSAL_LOGW(TAG, "OTA rejected: %s", add_str);
        __ota_led_apply_hue(__ota_led_hue.failed);
        break;
    case RMAKER_OTA_EVENT_ERROR_OCCURRED:
        if (event_data != NULL) {
            error = *((esp_rmaker_ota_error_reason_t *)event_data);
        }
        OSAL_LOGE(TAG, "OTA error occurred: %s", esp_rmaker_ota_error_reason_to_string(error));
        __ota_led_apply_hue(__ota_led_hue.failed);
        break;
    default:
        OSAL_LOGI(TAG, "Unhandled OTA event: %" PRId32, event_id);
        break;
    }
}

/* Abstracted main function.
 * For how ESP-IDF and POSIX entry points use this, see the examples/common/app_entry component. */
osal_err_t app_run(void)
{
    /* Initialise the RMNG serial console. */
    esp_rmaker_console_init();

    /* Initialise and turn on the status LED (setup / white). */
    if (app_led_init(&__ota_led_boot_state) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to initialise the LED; check the configuration");
        return OSAL_ERR_FAIL;
    }

    /* Initialize NVS and the network stack. */
    osal_storage_init(NULL);
    app_network_init();

    /* Register the network reset function so the console reset-network command can trigger it. */
    esp_rmaker_system_ctrl_register_network_reset_fn(app_network_reset_credentials);

    /* RMNG provisioning runs before node init. This example advertises no
     * manufacturer-data device type (NONE).
     * See the examples/common/app_network component for more details. */
    app_network_provision(NEO_MFG_DATA_DEVICE_TYPE_NONE, NEO_MFG_DATA_DEVICE_SUBTYPE_NONE);

    /* Register the default RMNG event handler (logs RMNG/OTA/network events).
     * See the examples/common/app_event_loop component for more details. */
    app_event_loop_register_default_handler();

    /* Initialize the RMNG agent. */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = true,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "OTA Custom", "ota-custom");
    if (!node) {
        OSAL_LOGE(TAG, "Could not initialise node. Aborting!!!");
        return OSAL_ERR_FAIL;
    }

    /* Enable optional services. */
#if CONFIG_ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE
    /* On-Network Challenge Response: node-group association via an HTTP server on the node. */
    esp_rmaker_chal_resp_service_enable();
#else
    /* Local Control: control the device without internet via an HTTP server on the node. */
    esp_rmaker_local_ctrl_service_enable();
#endif

    /* Enable OTA before starting the agent. Register the OTA event handler, then
     * plumb in the custom-filetype handler (and, when enabled, the custom job
     * callback) demonstrated in ota_custom_mock.c. */
    osal_event_handler_register(RMAKER_OTA_EVENT, RMAKER_OTA_EVENT_BASE_ANY, __ota_event_handler, NULL);

    esp_rmaker_ota_config_t ota_config = {
        .custom_filetype_handler_lookup = custom_mock_lookup,
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
        .custom_job_cb = custom_mock_custom_job_cb, /* Optional: custom job callback. */
#endif
        .ota_cb = NULL,             /* Use the default OTA callback. */
        .ota_diag = NULL,           /* No OTA diagnostics callback. */
        .priv = NULL,
    };
    esp_rmaker_ota_enable(&ota_config);

    /* Start the RMNG agent. */
    esp_rmaker_start();

    /* Signal setup complete. */
    __ota_led_apply_hue(__ota_led_hue.idle);
    OSAL_LOGI(TAG, "ota-custom ready.");
    return OSAL_ERR_OK;
}
