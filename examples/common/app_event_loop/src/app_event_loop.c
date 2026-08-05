/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_event_loop.c
 * @brief Implementation of the app event loop.
 */

/* Includes **********************************************************************/

/* Declarations */
#include "app_event_loop.h"

/* Standard includes */
#include <stdint.h>
#include <inttypes.h>

/* Event loop includes */
#include "esp_rmaker_event_loop.h"
#include "esp_rmaker_ota_event_loop.h"
#include "esp_rmaker_ota_error_reasons.h"

/* Platform common includes */
#include "osal_log.h"

/* MQTT common includes */
#include "osal_mqtt_prototypes.h"

/* System control includes */
#include "esp_rmaker_system_ctrl.h"

/* Services includes */
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE
#include "esp_rmaker_standard_services.h"
#endif

/* Constants **********************************************************************/

/**
 * @brief Delay in seconds before rebooting the device after OTA update.
 */
#define OTA_REBOOT_DELAY_SECONDS 3

/**
 * @brief Tag for the app event loop.
 */
static const char *TAG = "app_event_loop";

/* Private function declarations ****************************************************/

/**
 * @brief Default event handler for RMAKER_EVENT and RMAKER_COMMON_EVENT events.
 *
 * @param[in] event_handler_arg The argument to pass to the event handler.
 * @param[in] event_base The event base to send the event to.
 * @param[in] event_id The event id to send the event to.
 * @param[in] event_data The data to send with the event.
 */
static void esp_rmaker_app_default_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data);

/**
 * @brief MQTT on complete event handler.
 *
 * @param[in] event_name The name of the event.
 * @param[in] event_data Event data, which is a pointer to a osal_mqtt_event_loop_data_on_complete_t.
 */
static void __mqtt_on_complete_event_handler(const char *event_name, void *event_data);

/**
 * @brief OTA event handler.
 *
 * @param[in] event_handler_arg Event handler argument.
 * @param[in] event_base Event base.
 * @param[in] event_id Event ID.
 * @param[in] event_data Event data.
 */
static void __ota_event_handler(void *event_handler_arg, osal_event_base_t event_base,
                                int32_t event_id, void *event_data);

/* Private function definitions ****************************************************/

static void esp_rmaker_app_default_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    /* Handle RMAKER_EVENT events. You can handle less cases here if you only want to handle a few events. */
    if (event_base == RMAKER_EVENT) {
        switch ((esp_rmaker_event_t) event_id) {

        /** Core events ****************************************************************/
        case RMAKER_EVENT_INIT_DONE:
            // RainMaker SDK initialized.
            OSAL_LOGI(TAG, "RMNG SDK initialized");
            break;
        case RMAKER_EVENT_CORE_STARTED:
            // RainMaker SDK started.
            OSAL_LOGI(TAG, "RMNG SDK started");
            break;

        /** Local control events ******************************************************/
        case RMAKER_EVENT_LOCAL_CTRL_STARTED:
            // Local control started.
            OSAL_LOGI(TAG, "Local control started");
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE
            OSAL_LOGI(TAG, "Enabling challenge response");
            esp_rmaker_chal_resp_service_enable();
#endif /* CONFIG_ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE */
            break;
        case RMAKER_EVENT_LOCAL_CTRL_STOPPED:
            // Local control stopped.
            OSAL_LOGI(TAG, "Local control stopped");
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE
            OSAL_LOGI(TAG, "Disabling challenge response");
            esp_rmaker_chal_resp_service_disable();
#endif /* CONFIG_ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE */
            break;

        default:
            // Unknown event.
            OSAL_LOGW(TAG, "Unknown event: %" PRId32, event_id);
            break;
        }
    }

    /* Handle RMAKER_COMMON_EVENT events. You can handle less cases here if you only want to handle a few events. */
    else if (event_base == RMAKER_COMMON_EVENT) {
        switch ((esp_rmaker_common_event_t) event_id) {

        /** MQTT events ****************************************************************/
        case RMAKER_MQTT_EVENT_CONNECTED:
            // MQTT connected.
            OSAL_LOGI(TAG, "MQTT connected");
            break;
        case RMAKER_MQTT_EVENT_DISCONNECTED:
            // MQTT disconnected.
            OSAL_LOGI(TAG, "MQTT disconnected");
            break;
        case RMAKER_MQTT_EVENT_PUBLISHED:
            // MQTT message published successfully.
            __mqtt_on_complete_event_handler("Publish", event_data);
            break;
        case RMAKER_MQTT_EVENT_SUBSCRIBED:
            // MQTT message subscribed successfully.
            __mqtt_on_complete_event_handler("Subscribe", event_data);
            break;
        case RMAKER_MQTT_EVENT_UNSUBSCRIBED:
            // MQTT message unsubscribed successfully.
            __mqtt_on_complete_event_handler("Unsubscribe", event_data);
            break;

        /** Timezone events ************************************************************/
        case RMAKER_EVENT_TZ_POSIX_CHANGED:
            // POSIX timezone changed.
            OSAL_LOGI(TAG, "POSIX timezone changed to: %s", (char *)event_data);
            break;
        case RMAKER_EVENT_TZ_CHANGED:
            // Timezone changed.
            OSAL_LOGI(TAG, "Timezone changed to: %s", (char *)event_data);
            break;

        /** System events **************************************************************/
        case RMAKER_EVENT_REBOOT:
            // System rebooted.
            OSAL_LOGI(TAG, "System will reboot in %" PRIu8 " seconds", *(uint8_t *)event_data);
            break;
        case RMAKER_EVENT_NETWORK_RESET:
            // System network reset.
            OSAL_LOGI(TAG, "System will reset network");
            break;
        case RMAKER_EVENT_FACTORY_RESET:
            // System factory reset.
            OSAL_LOGI(TAG, "System will reset factory");
            break;

        default:
            // Unknown event.
            OSAL_LOGW(TAG, "Unknown event: %" PRId32, event_id);
            break;
        }
    }

    /* Ignore other event bases */
}

static void __mqtt_on_complete_event_handler(const char *event_name, void *event_data)
{
    osal_mqtt_event_loop_data_on_complete_t *mqtt_data = (osal_mqtt_event_loop_data_on_complete_t *)event_data;
    const char *status_str = mqtt_data->status == OSAL_ERR_OK ? "SUCCESS" : "FAILED";
    OSAL_LOGD(TAG,
              "MQTT on complete event '%s':"
              "\n\tMessage ID: %" PRId32
              "\n\tStatus    : %s"
              "\n\tChannel   : %" PRIu32 ".%" PRIu32,
              event_name,
              mqtt_data->message_id,
              status_str,
              mqtt_data->channel.main,
              mqtt_data->channel.sub);
}

static void __ota_event_handler(void *event_handler_arg, osal_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base != RMAKER_OTA_EVENT) {
        return;
    }
    const esp_rmaker_ota_status_details_t *status_details = (const esp_rmaker_ota_status_details_t *)event_data;
    esp_rmaker_ota_error_reason_t error = OTA_ERROR_NONE;
    uint32_t downloaded_bytes = 0;
    uint32_t total_bytes = 0;
    uint32_t percentage = 0;
    char *filetype = "default (firmware update)";
    char *fw_version = "unknown";
    char *reason = "unknown";

    switch ((esp_rmaker_ota_event_t) event_id) {
    case RMAKER_OTA_EVENT_STARTING:
        if (status_details != NULL && status_details->type == ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_STARTING) {
            const esp_rmaker_ota_status_details_t *status_details = (const esp_rmaker_ota_status_details_t *)event_data;
            if (status_details->details.starting.fw_version != NULL) {
                fw_version = (char *)status_details->details.starting.fw_version;
            }
            if (status_details->details.starting.filetype != NULL) {
                filetype = (char *)status_details->details.starting.filetype;
            }
        }
        OSAL_LOGI(TAG,
                  "\n========================================\n"
                  "\tOTA EVENT: STARTING\n"
                  "\tOTA update is starting...\n"
                  "\tJob filetype: %s\n"
                  "\tJob firmware version: %s\n"
                  "========================================",
                  filetype,
                  fw_version
                 );
        break;

    case RMAKER_OTA_EVENT_FETCH_REQUEST_IGNORED:
        OSAL_LOGW(TAG,
                  "\n========================================\n"
                  "\tOTA EVENT: FETCH_REQUEST_IGNORED\n"
                  "\tThe OTA state machine is not in an appropriate state to handle a fetch request via esp_rmaker_ota_fetch().\n"
                  "\te.g., the OTA state machine is currently processing a job.\n"
                  "\tYou can use this event to trigger a delayed fetch request.\n"
                  "========================================");
        break;

    case RMAKER_OTA_EVENT_IN_PROGRESS:
        if (status_details != NULL && status_details->type == ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_IN_PROGRESS) {
            downloaded_bytes = status_details->details.in_progress.downloaded_bytes;
            total_bytes = status_details->details.in_progress.total_bytes;
            percentage = (downloaded_bytes * 100) / total_bytes;
        }
        OSAL_LOGI(TAG,
                  "\n========================================\n"
                  "\tOTA EVENT: IN_PROGRESS - Download progressing...\n"
                  "\tDownloaded: %" PRIu32 " / %" PRIu32 " bytes (%" PRIu32 "%%)\n"
                  "========================================",
                  downloaded_bytes,
                  total_bytes,
                  percentage
                 );
        break;

    case RMAKER_OTA_EVENT_SUCCESSFUL:
        if (status_details != NULL && status_details->type == ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_SUCCEEDED) {
            if (status_details->details.succeeded.fw_version != NULL) {
                fw_version = (char *)status_details->details.succeeded.fw_version;
            }
            if (status_details->details.succeeded.filetype != NULL) {
                filetype = (char *)status_details->details.succeeded.filetype;
            }
        }
        OSAL_LOGI(TAG,
                  "\n========================================\n"
                  "\tOTA EVENT: SUCCESSFUL\n"
                  "\tOTA update completed successfully!\n"
                  "\tJob filetype: %s\n"
                  "\tFirmware version: %s\n"
                  "========================================",
                  filetype,
                  fw_version
                 );
        break;

    case RMAKER_OTA_EVENT_FAILED:
        if (status_details != NULL && status_details->type == ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_FAILED) {
            reason = (char *)status_details->details.failed.reason;
        }
        OSAL_LOGE(TAG,
                  "\n========================================\n"
                  "\tOTA EVENT: FAILED\n"
                  "\tReason: %s\n"
                  "========================================",
                  reason
                 );
        break;

    case RMAKER_OTA_EVENT_REJECTED:
        if (status_details != NULL && status_details->type == ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_REJECTED) {
            reason = (char *)status_details->details.rejected.reason;
        }
        OSAL_LOGW(TAG,
                  "\n========================================\n"
                  "\tOTA EVENT: REJECTED\n"
                  "\tReason: %s\n"
                  "========================================",
                  reason
                 );
        break;

    case RMAKER_OTA_EVENT_REQ_FOR_REBOOT:
        OSAL_LOGI(TAG,
                  "\n========================================\n"
                  "\tOTA EVENT: REQ_FOR_REBOOT\n"
                  "\tNew firmware is ready, device will reboot in %" PRIu16 " seconds\n"
                  "========================================",
                  (uint16_t) OTA_REBOOT_DELAY_SECONDS
                 );
        esp_rmaker_system_ctrl_reboot(OTA_REBOOT_DELAY_SECONDS);
        break;

    case RMAKER_OTA_EVENT_ERROR_OCCURRED:
        if (event_data != NULL) {
            error = *((esp_rmaker_ota_error_reason_t *)event_data);
        }
        OSAL_LOGE(TAG,
                  "\n========================================\n"
                  "\tOTA EVENT: ERROR_OCCURRED\n"
                  "\tReason: %s\n"
                  "\tUse esp_rmaker_ota_request_recovery() to request recovery from this error, after fixing the underlying issue.\n"
                  "========================================",
                  esp_rmaker_ota_error_reason_to_string(error)
                 );
        break;
    default:
        OSAL_LOGD(TAG, "OTA EVENT: Unknown event %" PRId32, event_id);
        break;
    }
}

/* Public function definitions ****************************************************/

esp_rmaker_error_t app_event_loop_register_default_handler(void)
{
    osal_err_t err;
    /* Here we can initialise the default event loop if not already initialised. */
    err = osal_event_loop_create_default();
    if (err != OSAL_ERR_OK && err != OSAL_ERR_INVALID_STATE) {
        return ESP_RMAKER_FAIL;
    }

    /* Register the default event handlers. */
    err = osal_event_handler_register(RMAKER_EVENT, RMAKER_EVENT_BASE_ANY, esp_rmaker_app_default_event_handler, NULL);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    /* Register the OTA event handler. */
    err = osal_event_handler_register(RMAKER_OTA_EVENT, RMAKER_OTA_EVENT_BASE_ANY, __ota_event_handler, NULL);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t app_event_loop_unregister_default_handler(void)
{
    osal_err_t err = osal_event_handler_unregister(RMAKER_EVENT, RMAKER_EVENT_BASE_ANY, esp_rmaker_app_default_event_handler);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    err = osal_event_handler_unregister(RMAKER_OTA_EVENT, RMAKER_OTA_EVENT_BASE_ANY, __ota_event_handler);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}
