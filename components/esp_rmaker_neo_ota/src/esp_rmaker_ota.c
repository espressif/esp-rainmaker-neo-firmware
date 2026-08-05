/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_ota.c
 * @brief ESP RainMaker Neo OTA implementation
 */

/* Includes **********************************************************************/

/* Standard includes */
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

/* Configuration includes */
#include "sdkconfig.h"

/* Platform common includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_event_loop.h"
#include "osal_scheduler.h"

/* Private includes */
#include "ota_jobs.h"
#include "ota_nvs.h"
#include "ota_filetype_handler_internal.h"
#include "util/ota_partition.h"

/* Public includes */
#include "esp_rmaker_ota.h"
#include "esp_rmaker_common_events.h"
#include "esp_rmaker_ota_status_details.h"

/* Work queue includes */
#include "esp_rmaker_work_queue.h"

/* OTA common includes */
#include "osal_ota.h"

/* Constants **********************************************************************/

/* Logging tag */
static const char *TAG = "rmng_ota_core";

/* Event base definition */
OSAL_EVENT_DEFINE_BASE(RMAKER_OTA_EVENT);

/* Variables **********************************************************************/

/* OTA configuration */
static bool g_ota_enabled = false;
static bool g_ota_enable_job_added = false;

/* Rollback timeout handler */
static rmaker_ota_timeout_handler_handle_t g_rollback_timeout_handler = NULL;

/* Delayed fetch timer */
static osal_scheduler_task_handle_t g_delayed_fetch_timer_handle = NULL;

/* Private function declarations ***************************************************/

/**
 * @brief Work queue task to enable OTA
 *
 * This task is used to enable OTA by adding the OTA job to the work queue.
 *
 * @param[in] ota_config_arg The OTA configuration argument
 */
static void esp_rmaker_ota_enable_work_queue_task(void *ota_config_arg);

/**
 * @brief Handle reboot after OTA
 * - If this is a normal first boot without OTA, do nothing
 * - If this is a reboot after OTA, then perform diagnostic checks
 *
 * @param[in] ota_config The OTA configuration
 */
static void esp_rmaker_ota_handle_reboot(const esp_rmaker_ota_config_t *ota_config);

/**
 * @brief Callback function for the rollback timeout handler
 *
 * @param[in] unused Unused parameter
 */
static void esp_rmaker_ota_rollback_timeout_callback(void *unused);

/**
 * @brief Post MQTT diagnostics handler
 *
 * @param[in] event_handler_arg The argument to pass to the event handler. Expected to be the OTA diagnostics callback.
 * @param[in] event_base The event base. Expected to be RMAKER_COMMON_EVENT.
 * @param[in] event_id The event id. Expected to be RMAKER_MQTT_EVENT_CONNECTED.
 * @param[in] event_data The event data. Unused parameter.
 */
static void esp_rmaker_ota_post_mqtt_diag_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data);

/**
 * @brief Work queue task to fetch OTA with delay
 *
 * This task is used by the delayed fetch timer to fetch OTA after the delay period expires.
 *
 * @param[in] unused Unused parameter
 */
static void esp_rmaker_ota_delayed_fetch_timer_task(void *unused);

/* Private function definitions *****************************************************/

static void esp_rmaker_ota_enable_work_queue_task(void *ota_config_arg)
{
    g_ota_enable_job_added = false;
    esp_rmaker_ota_config_t *ota_config = (esp_rmaker_ota_config_t *)ota_config_arg;
    OSAL_LOGI(TAG, "Enabling OTA");

    /* Initialize OTA Jobs state machine */
    esp_rmaker_ota_cb_t ota_cb = ota_config->ota_cb != NULL
                                 ? ota_config->ota_cb
                                 : esp_rmaker_ota_default_cb;
    /* Pair a validator with the cb. If user provided their own ota_cb without a
     * matching validator, leave it NULL (user opted out of pre-download validation). */
    esp_rmaker_ota_validate_image_ref_t validate_image_ref = ota_config->validate_image_ref;
    if (ota_config->ota_cb == NULL && validate_image_ref == NULL) {
        validate_image_ref = esp_rmaker_ota_default_validate_image_ref;
    }
    ota_job_config_t ota_job_config = {
        .image_download = {
            .ota_cb = ota_cb,
            .validate_image_ref = validate_image_ref,
            .priv = ota_config->priv,
        },
        .filetype_handler_lookup = ota_config->custom_filetype_handler_lookup,
#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
        .custom_job_cb = ota_config->custom_job_cb,
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */
    };
    free(ota_config);
    esp_rmaker_error_t err = ota_job_state_init(&ota_job_config);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize OTA Jobs SM: %d", err);
        return;
    }

    g_ota_enabled = true;
    OSAL_LOGI(TAG, "OTA enabled successfully");
}

static void esp_rmaker_ota_handle_reboot(const esp_rmaker_ota_config_t *ota_config)
{
    char *filetype = esp_rmaker_ota_get_last_filetype();
    if (filetype != NULL) {
        /* Custom filetype: separate reboot check path */
        OSAL_LOGI(TAG, "Custom filetype '%s' detected: starting custom post reboot handler", filetype);

        do {
            /* Retrieve the custom filetype handler */
            const esp_rmaker_ota_ft_ctx_t *ft_ctx = ota_config->custom_filetype_handler_lookup(filetype, strlen(filetype));
            if (ft_ctx == NULL) {
                OSAL_LOGE(TAG, "Failed to get custom filetype handler for filetype '%s'", filetype);
                break;
            }

            /* Call the custom post reboot handler if existent */
            if (ft_ctx->on_post_reboot != NULL) {
                filetype_handler_status_timer_start();
                OSAL_LOGI(TAG, "Calling custom post reboot handler and waiting for final status report via esp_rmaker_ota_report_final_status()");
                esp_rmaker_error_t err = ft_ctx->on_post_reboot();
                if (err != ESP_RMAKER_OK) {
                    OSAL_LOGE(TAG, "Failed to call custom post reboot handler: %d", err);
                }
            } else {
                OSAL_LOGE(TAG, "Custom filetype '%s' handler does not have a post reboot handler even though it was instructed to reboot post-download.\n"
                          "Job will be explicitly failed.", filetype);
                esp_rmaker_ota_status_details_t status_details;
                esp_rmaker_ota_status_details_fill_failed(&status_details, ESP_RMAKER_OTA_FAILED_REASON_CUSTOM_FILETYPE_HANDLER_NO_POST_REBOOT_HANDLER);
                esp_rmaker_ota_report_final_status(&status_details);
            }
        } while (0);

        free(filetype);
        return;
    }

    /* Check this partition's status */
    if (!esp_rmaker_ota_partition_running_is_pending_verify()) {
        // Could be:
        // 1. A normal boot without OTA
        // 2. Already rolled back to the previous version
        // 3. No rollback enabled, i.e., CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is not set
        // in any of these cases, we set the success status in NVS if there isn't a pending failure report
        // then trigger the reboot check event to the state machine
        esp_rmaker_ota_nvs_set_status_if_pending_verification(true, false);
        ota_job_state_reboot_check();
        return;
    }

    /* Run diagnostics if available */
    if (ota_config->ota_diag == NULL) {
        OSAL_LOGI(TAG, "No diagnostics function set, skipping diagnostics and marking OTA as valid");
        esp_rmaker_ota_mark_valid();
        return;
    }

    esp_rmaker_ota_diag_priv_t ota_diag_priv = {
        .state = OTA_DIAG_STATE_INIT,
        .rmaker_ota = true,
    };
    esp_rmaker_ota_diag_status_t diag_status = ota_config->ota_diag(&ota_diag_priv, ota_config->priv);
    if (diag_status == OTA_DIAG_STATUS_FAIL) {
        OSAL_LOGE(TAG, "Diagnostics failed! Start rollback to the previous version...");
        esp_rmaker_ota_mark_invalid();
        return;
    }

    /* --------------- Passed/pending diagnostics --------------- */
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    do {
        /* Start rollback timeout handler */
        if (g_rollback_timeout_handler == NULL) {
            rmaker_ota_timeout_handler_config_t rollback_timeout_handler_config = {
                .timeout_ms = CONFIG_RMNG_OTA_ROLLBACK_WAIT_PERIOD * 1000,
                .callback = esp_rmaker_ota_rollback_timeout_callback,
                .priv_data = NULL,
            };
            err = rmaker_ota_timeout_handler_init(&rollback_timeout_handler_config, &g_rollback_timeout_handler);
            if (err != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to initialize rollback timeout handler: %d, will not perform post MQTT diagnostics", err);
                break;
            }
        }
        err = rmaker_ota_timeout_handler_restart(g_rollback_timeout_handler);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to start rollback timeout handler: %d, will not perform post MQTT diagnostics", err);
            break;
        }
        OSAL_LOGI(TAG, "Rollback will occur in %" PRIu32 " seconds if OTA is not marked as valid", (uint32_t)CONFIG_RMNG_OTA_ROLLBACK_WAIT_PERIOD);

        /* Register event handler for MQTT connected event */
        osal_err_t event_loop_err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, esp_rmaker_ota_post_mqtt_diag_handler, ota_config->ota_diag);
        if (event_loop_err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to register post MQTT diagnostics event handler: %d, will not perform post MQTT diagnostics", event_loop_err);
            break;
        }

        /* Return to avoid marking the OTA as invalid */
        return;
    } while (0);

    /* If we reach here, something went wrong, so mark the OTA as invalid */
    esp_rmaker_ota_mark_invalid();
}

static void esp_rmaker_ota_rollback_timeout_callback(void *unused)
{
    OSAL_LOGE(TAG, "OTA not marked valid within timeout period! Start rollback to the previous version...");
    esp_rmaker_ota_mark_invalid();
}

static void esp_rmaker_ota_post_mqtt_diag_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base != RMAKER_COMMON_EVENT || event_id != RMAKER_MQTT_EVENT_CONNECTED) {
        OSAL_LOGE(TAG, "Unexpected event received: event_base: %s, event_id: %" PRId32, event_base, event_id);
        return;
    }

    OSAL_LOGI(TAG, "MQTT connected event received, performing post MQTT diagnostics");
    esp_rmaker_post_ota_diag_t ota_diag = (esp_rmaker_post_ota_diag_t)event_handler_arg;
    esp_rmaker_ota_diag_priv_t ota_diag_priv = {
        .state = OTA_DIAG_STATE_POST_MQTT,
        .rmaker_ota = true,
    };
    esp_rmaker_ota_diag_status_t diag_status = ota_diag(&ota_diag_priv, event_handler_arg);
    switch (diag_status) {
    case OTA_DIAG_STATUS_FAIL:
        OSAL_LOGE(TAG, "Post MQTT diagnostics failed! Start rollback to the previous version...");
        esp_rmaker_ota_mark_invalid();
        break;
    case OTA_DIAG_STATUS_SUCCESS:
        OSAL_LOGI(TAG, "Post MQTT diagnostics successful! Marking OTA as valid.");
        esp_rmaker_ota_mark_valid();
        break;
    case OTA_DIAG_STATUS_PENDING:
        OSAL_LOGW(TAG, "Post MQTT diagnostics pending! esp_rmaker_ota_mark_valid() or esp_rmaker_ota_mark_invalid() should be called later to complete the diagnostics before timeout");
        break;
    default:
        OSAL_LOGE(TAG, "Unknown OTA diagnostic status: %d", diag_status);
        break;
    }

    /* Unregister event handler for MQTT connected event */
    osal_err_t event_loop_err = osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, esp_rmaker_ota_post_mqtt_diag_handler);
    if (event_loop_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to unregister post MQTT diagnostics event handler: %d", event_loop_err);
    }
}

static void esp_rmaker_ota_delayed_fetch_timer_task(void *unused)
{
    OSAL_LOGI(TAG, "Delayed fetch timer expired, fetching OTA jobs now...");
    esp_rmaker_ota_fetch();
}

/* Public function definitions *****************************************************/

esp_rmaker_error_t esp_rmaker_ota_enable(const esp_rmaker_ota_config_t *ota_config)
{
    if (g_ota_enabled || g_ota_enable_job_added) {
        OSAL_LOGW(TAG, "OTA already enabled or pending");
        return ESP_RMAKER_OK;
    }

    /* Initialize critical components for OTA Jobs state machine */
    esp_rmaker_error_t err = ota_job_critical_init();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize critical components for OTA Jobs state machine: %d", err);
        return err;
    }

    /* Run initial reboot check */
    esp_rmaker_ota_handle_reboot(ota_config);

    /* Copy config to heap */
    esp_rmaker_ota_config_t *ota_config_copy = OSAL_MALLOC_EXTRAM(sizeof(esp_rmaker_ota_config_t));
    if (ota_config_copy == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for OTA config copy");
        return ESP_RMAKER_NO_MEM;
    }
    memcpy(ota_config_copy, ota_config, sizeof(esp_rmaker_ota_config_t));

    /* Add work function to enable OTA */
    if (esp_rmaker_work_queue_add_task(esp_rmaker_ota_enable_work_queue_task, ota_config_copy) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add work function to enable OTA");
        return ESP_RMAKER_FAIL;
    }

    g_ota_enable_job_added = true;
    OSAL_LOGI(TAG, "OTA enable job added; will be executed on start");
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_ota_disable(void)
{
    if (!g_ota_enabled) {
        OSAL_LOGW(TAG, "OTA not enabled");
        return ESP_RMAKER_OK;
    }

    /* Cancel the delayed fetch timer */
    if (g_delayed_fetch_timer_handle != NULL) {
        osal_err_t err = osal_scheduler_cancel_task(&g_delayed_fetch_timer_handle);
        if (err != OSAL_ERR_OK) {
            OSAL_LOGW(TAG, "Failed to cancel delayed fetch timer: %d", err);
        }
    }

    /* Deinitialize OTA Jobs state machine */
    esp_rmaker_error_t err = ota_job_state_deinit();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to deinitialize OTA Jobs SM: %d", err);
        return err;
    }

    /* Reset OTA enable job added flag */
    g_ota_enabled = false;
    OSAL_LOGI(TAG, "OTA disabled");
    return ESP_RMAKER_OK;
}

#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
esp_rmaker_error_t esp_rmaker_ota_report_custom_job_status(ota_status_t status, const char *status_details_json, size_t status_details_json_len)
{
    ota_job_event_t event_id;
    switch (status) {
    case OTA_STATUS_IN_PROGRESS:
        event_id = OTA_JOB_EVENT_CUSTOM_JOB_PROGRESS;
        break;
    case OTA_STATUS_SUCCESS:
        event_id = OTA_JOB_EVENT_CUSTOM_JOB_SUCCEEDED;
        break;
    case OTA_STATUS_FAILED:
        event_id = OTA_JOB_EVENT_CUSTOM_JOB_FAILED;
        break;
    case OTA_STATUS_REJECTED:
        event_id = OTA_JOB_EVENT_CUSTOM_JOB_REJECTED;
        break;
    default:
        OSAL_LOGE(TAG, "Unsupported custom job status: %d", status);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Construct event data */
    ota_job_event_data_payload_t payload = {
        .data = (void *)status_details_json,
        .len = status_details_json_len,
    };
    ota_job_event_data_t event_data = {
        .event = event_id,
        .payload = &payload,
    };
    return ota_job_state_post_event(&event_data);
}
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */

esp_rmaker_error_t esp_rmaker_ota_report_status(esp_rmaker_ota_handle_t ota_handle,
        ota_status_t status, const esp_rmaker_ota_status_details_t *status_details)
{
    if (status_details == NULL) {
        OSAL_LOGE(TAG, "Status details is NULL");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Map status events to event loop and AWS IoT Job events */
    JobCurrentStatus_t job_status; bool job_status_set = false;
    esp_rmaker_ota_event_t event_id;
    switch (status) {
    case OTA_STATUS_IN_PROGRESS:
        event_id = RMAKER_OTA_EVENT_IN_PROGRESS;
        job_status = InProgress; job_status_set = true;
        break;
    case OTA_STATUS_SUCCESS:
        event_id = RMAKER_OTA_EVENT_SUCCESSFUL;
        job_status = Succeeded; job_status_set = true;
        break;
    case OTA_STATUS_FAILED:
        event_id = RMAKER_OTA_EVENT_FAILED;
        job_status = Failed; job_status_set = true;
        break;
    case OTA_STATUS_DELAYED:
        event_id = RMAKER_OTA_EVENT_DELAYED;
        job_status_set = false;
        break;
    case OTA_STATUS_REJECTED:
        event_id = RMAKER_OTA_EVENT_REJECTED;
        job_status = Rejected; job_status_set = true;
        break;
    default:
        OSAL_LOGE(TAG, "Unknown OTA status: %d", status);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Post event to user application */
    osal_event_post(RMAKER_OTA_EVENT, event_id,
                    (void *)status_details, sizeof(esp_rmaker_ota_status_details_t),
                    OSAL_MAX_DELAY);

    /* Post event to AWS IoT Jobs */
    if (job_status_set) {
        esp_rmaker_error_t err = ota_jobs_mqtt_publish_update_job_status(job_status, status_details);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to publish update job status: %d", err);
            return err;
        }
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_ota_default_cb(esp_rmaker_ota_handle_t handle, esp_rmaker_ota_data_t *ota_data, const esp_rmaker_ota_ft_ctx_t *ft_handler)
{
#if CONFIG_RMNG_OTA_TRANSPORT_MQTT
    return esp_rmaker_ota_mqtt_cb(handle, ota_data, ft_handler);
#else
    OSAL_LOGE(TAG, "Invalid OTA transport");
    return ESP_RMAKER_INVALID_ARG;
#endif
}

esp_rmaker_error_t esp_rmaker_ota_default_validate_image_ref(const char *image_ref, size_t image_ref_len)
{
#if CONFIG_RMNG_OTA_TRANSPORT_MQTT
    return esp_rmaker_ota_mqtt_validate_image_ref(image_ref, image_ref_len);
#else
    (void)image_ref;
    (void)image_ref_len;
    OSAL_LOGE(TAG, "Invalid OTA transport");
    return ESP_RMAKER_INVALID_ARG;
#endif
}

esp_rmaker_error_t esp_rmaker_ota_fetch(void)
{
    if (!g_ota_enabled) {
        OSAL_LOGE(TAG, "OTA not enabled");
        return ESP_RMAKER_INVALID_STATE;
    }

    OSAL_LOGI(TAG, "Fetching OTA jobs...");

    /* Cancel the delayed fetch timer */
    if (g_delayed_fetch_timer_handle != NULL) {
        osal_err_t err = osal_scheduler_stop_timer(g_delayed_fetch_timer_handle);
        if (err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to cancel delayed fetch timer: %d", err);
            return ESP_RMAKER_FAIL;
        }
    }

    /* Post fetch event to state machine */
    esp_rmaker_error_t err = ota_job_state_fetch();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to post fetch event: %d", err);
        return err;
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_ota_fetch_with_delay(int time)
{
    if (time <= 0) {
        OSAL_LOGE(TAG, "Invalid delay time: %d. Must be greater than 0.", time);
        return ESP_RMAKER_INVALID_ARG;
    }

    if (!g_ota_enabled) {
        OSAL_LOGE(TAG, "OTA not enabled");
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Set the delayed fetch timer */
    osal_err_t err;
    if (g_delayed_fetch_timer_handle == NULL) {
        err = osal_scheduler_schedule_task(&g_delayed_fetch_timer_handle, time * 1000, esp_rmaker_ota_delayed_fetch_timer_task, NULL);
    } else {
        err = osal_scheduler_reset_timer(g_delayed_fetch_timer_handle, time * 1000);
    }
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to set delayed fetch timer: %d", err);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_ota_request_recovery(void)
{
    if (!g_ota_enabled) {
        OSAL_LOGE(TAG, "OTA not enabled");
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Post recovery request to state machine */
    return ota_job_state_recover();
}

esp_rmaker_error_t esp_rmaker_ota_mark_valid(void)
{
    if (!esp_rmaker_ota_partition_running_is_pending_verify()) {
        OSAL_LOGI(TAG, "Cannot mark valid due to invalid OTA state");
        return ESP_RMAKER_INVALID_STATE;
    }

    bool passed = true;

    /* Mark the OTA as valid */
    osal_err_t ota_err = osal_ota_mark_app_valid_cancel_rollback();
    if (ota_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to mark OTA as valid: %d", ota_err);
        passed = false;
    }

    esp_rmaker_error_t err;

    /* Deinitialize the rollback timeout handler */
    if (g_rollback_timeout_handler != NULL) {
        err = rmaker_ota_timeout_handler_deinit(g_rollback_timeout_handler);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to deinitialize rollback timeout handler: %d", err);
            return err;
        }
        g_rollback_timeout_handler = NULL;
    }

    /* Force set the success status in NVS */
    err = esp_rmaker_ota_nvs_set_status_if_pending_verification(passed, true);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to set success status: %d", err);
        return err;
    }

    /* Post reboot check requested event to state machine */
    return ota_job_state_reboot_check();
}

esp_rmaker_error_t esp_rmaker_ota_mark_invalid(void)
{
    if (!esp_rmaker_ota_partition_running_is_pending_verify()) {
        OSAL_LOGI(TAG, "Cannot rollback due to invalid OTA state");
        return ESP_RMAKER_INVALID_STATE;
    }
    esp_rmaker_error_t err;

    /* Deinitialize the rollback timeout handler */
    if (g_rollback_timeout_handler != NULL) {
        err = rmaker_ota_timeout_handler_deinit(g_rollback_timeout_handler);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to deinitialize rollback timeout handler: %d", err);
            return err;
        }
        g_rollback_timeout_handler = NULL;
    }

    /* Force set the failed status in NVS */
    err = esp_rmaker_ota_nvs_set_status_if_pending_verification(false, true);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to set failed status: %d", err);
        return err;
    }

    /* Mark the OTA as invalid */
    osal_err_t ota_err = osal_ota_mark_app_invalid_rollback_and_reboot();
    if (ota_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to mark OTA as invalid: %d", ota_err);
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}
