/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_image_progress.c
 * @brief Progress tracking for the image download
 */

/* Includes ******************************************************************/

/* Declarations */
#include "ota_image_progress.h"

/* Standard includes */
#include <inttypes.h>

/* Configuration includes */
#include "sdkconfig.h"

/* Platform common includes */
#include "osal_log.h"

/* RMNG OTA includes */
#include "ota_jobs.h"

/* Constants ******************************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "rmng_ota_progress";

/* Private function declarations ************************************************/

/**
 * @brief Check if the image progress should be reported
 *
 * @param[in] p_ctx Image progress context
 * @return True if the image progress should be reported, false otherwise
 */
#define image_progress_should_report_progress(p_ctx) (p_ctx->bytes_received >= p_ctx->checkpoint.next)

/**
 * @brief Increment the checkpoint of the image progress
 *
 * @param[in] p_ctx Image progress context
 * @return ESP_RMAKER_OK on success, error code otherwise
 */
#define image_progress_increment_checkpoint(p_ctx) (p_ctx->checkpoint.next += p_ctx->checkpoint.inc)
/**
 * @brief Report progress of the image download
 *
 * @param[in] p_ctx Image progress context
 * @return ESP_RMAKER_OK on success, error code otherwise
 */
static esp_rmaker_error_t image_progress_report_progress(const image_progress_ctx_t *p_ctx);

/* Private function definitions ************************************************/

static esp_rmaker_error_t image_progress_report_progress(const image_progress_ctx_t *p_ctx)
{
    if (!p_ctx) {
        OSAL_LOGE(TAG, "Invalid argument: p_ctx is NULL");
        return ESP_RMAKER_INVALID_ARG;
    }
    char downloaded_bytes_str[16];
    int downloaded_bytes_str_len = snprintf(downloaded_bytes_str, sizeof(downloaded_bytes_str), "%" PRIu32, p_ctx->bytes_received);
    if (downloaded_bytes_str_len < 0 || downloaded_bytes_str_len >= sizeof(downloaded_bytes_str)) {
        OSAL_LOGE(TAG, "Failed to format downloaded bytes string");
        return ESP_RMAKER_INVALID_ARG;
    }
    ota_job_event_data_payload_t payload = {
        .data = downloaded_bytes_str,
        .len = downloaded_bytes_str_len + 1,
    };
    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_PROGRESS,
        .payload = &payload,
    };
    return ota_job_state_post_event(&event_data);
}
/* Public function definitions *************************************************/

esp_rmaker_error_t image_progress_init(image_progress_ctx_t *p_ctx, uint32_t filesize)
{
    if (!p_ctx) {
        OSAL_LOGE(TAG, "Invalid argument: p_ctx is NULL");
        return ESP_RMAKER_INVALID_ARG;
    }
    if (filesize == 0) {
        OSAL_LOGE(TAG, "Invalid argument: filesize is 0");
        return ESP_RMAKER_INVALID_ARG;
    }
    p_ctx->bytes_received = 0;
    uint32_t checkpoint_increment = filesize / CONFIG_RMNG_OTA_PROGRESS_CHECKPOINTS;
    p_ctx->checkpoint.next = checkpoint_increment;
    p_ctx->checkpoint.inc = checkpoint_increment;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t image_progress_add_bytes(image_progress_ctx_t *p_ctx, uint32_t bytes)
{
    if (!p_ctx) {
        OSAL_LOGE(TAG, "Invalid argument: p_ctx is NULL");
        return ESP_RMAKER_INVALID_ARG;
    }
    p_ctx->bytes_received += bytes;

    /* Report progress if a checkpoint has been reached */
    if (image_progress_should_report_progress(p_ctx)) {
        image_progress_report_progress(p_ctx);
        image_progress_increment_checkpoint(p_ctx);
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t image_progress_seed(image_progress_ctx_t *p_ctx, uint32_t bytes)
{
    if (!p_ctx) {
        OSAL_LOGE(TAG, "Invalid argument: p_ctx is NULL");
        return ESP_RMAKER_INVALID_ARG;
    }
    p_ctx->bytes_received = bytes;
    /* Fast-forward the next checkpoint to the first multiple strictly above the seeded
     * count, so we neither emit now nor replay every checkpoint already passed. */
    if (p_ctx->checkpoint.inc > 0) {
        p_ctx->checkpoint.next = (bytes / p_ctx->checkpoint.inc + 1) * p_ctx->checkpoint.inc;
    }
    return ESP_RMAKER_OK;
}
