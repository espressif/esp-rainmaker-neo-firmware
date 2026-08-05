/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_image_handler.c
 * @brief Handler functions for the downloaded OTA image
 */

/* Includes *************************************************************/

/* Declarations */
#include "ota_image_handler.h"

/* Standard includes */
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>

/* OTA common includes */
#include "osal_ota.h"
#include "ota_jobs.h"
#include "ota_nvs.h"
#include "ota_filetype_handler_internal.h"

/* Platform common includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Configuration includes */
#include "sdkconfig.h"

/* Util includes */
#include "util/esp_rmaker_convert_base64.h"
#include "util/esp_rmaker_convert_hex.h"

/* Constants ******************************************************************/

/* Logging tag */
static const char *TAG = "rmng_ota_image";

/* Private function declarations ************************************************/

/**
 * @brief End the image handler
 *
 * @param[in] p_ctx Image handler context
 * @param[in] success True if the image download succeeded, false otherwise
 * @param[out] p_should_reboot True if the device should reboot, false otherwise
 * @param[out] p_failure_event If a post-download check fails, set to the specific failure event so
 *                             the caller can report a fine-grained reason to the cloud. Left
 *                             untouched on success or when an existing failure event already
 *                             describes the situation (caller supplies a default).
 * @return ESP_RMAKER_OK on success, error code otherwise
 */
static esp_rmaker_error_t image_handler_end(const image_handler_ctx_t *p_ctx, bool success, bool *p_should_reboot, ota_job_event_t *p_failure_event);

/* Private function definitions ************************************************/

static esp_rmaker_error_t image_handler_end(const image_handler_ctx_t *p_ctx, bool success, bool *p_should_reboot, ota_job_event_t *p_failure_event)
{
    if (!p_ctx) {
        OSAL_LOGE(TAG, "Invalid argument: p_ctx is NULL");
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    esp_rmaker_error_t err_temp = ESP_RMAKER_OK;
    esp_rmaker_ota_ft_download_handle_t handle = p_ctx->handle;
    const esp_rmaker_ota_ft_ctx_t *ft_handler = p_ctx->ft_handler;
    if (ft_handler == NULL) {
        /* This can happen if image_handler_begin() was not called */
        err = ESP_RMAKER_OK;
        goto image_handler_end_end;
    }

    bool passed_post_download_checks = false;
    if (success) {
        /* End the OTA update */
        err = ft_handler->on_download_complete(handle, true);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to end OTA update: error code %d", err);
            goto image_handler_end_end;
        }

        /* Verify the downloaded image's embedded header matches what the job declared.
         * Runs before signature verification so a mismatched image is rejected even if
         * its signature happens to be valid for some other (downgraded / cross-project) build. */
        if (ft_handler->verify_image_header != NULL) {
            err_temp = ft_handler->verify_image_header(handle, p_ctx->expected_fw_version);
            if (err_temp != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Image header verification failed: error code %d", err_temp);
                err = ESP_RMAKER_INVALID_STATE;
                if (p_failure_event) {
                    *p_failure_event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_IMAGE_HEADER_INVALID;
                }
                goto image_handler_end_post_download_checks_complete;
            }
        }

        /* End-to-end MD5 integrity check against the job's declared file_md5.
         * Critical for resumed downloads, whose bytes bypass any single continuous hash.
         * Skipped (with a warning) if the filetype handler cannot compute an MD5. */
        if (p_ctx->expected_md5 != NULL) {
            if (ft_handler->get_md5_hash == NULL) {
                OSAL_LOGW(TAG, "Job provided MD5 but filetype handler has no get_md5_hash; skipping MD5 check");
            } else {
                uint8_t md5[16];
                char md5_hex[2 * sizeof(md5) + 1];
                err_temp = ft_handler->get_md5_hash(handle, md5);
                if (err_temp == ESP_RMAKER_OK) {
                    err_temp = esp_rmaker_convert_bytes_to_hex(md5, sizeof(md5), md5_hex, sizeof(md5_hex));
                }
                if (err_temp != ESP_RMAKER_OK) {
                    OSAL_LOGE(TAG, "Failed to compute image MD5: error code %d", err_temp);
                    err = ESP_RMAKER_INVALID_STATE;
                    if (p_failure_event) {
                        *p_failure_event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_MD5_INVALID;
                    }
                    goto image_handler_end_post_download_checks_complete;
                }
                if (strcasecmp(md5_hex, p_ctx->expected_md5) != 0) {
                    OSAL_LOGE(TAG, "Image MD5 mismatch: computed=%s expected=%s", md5_hex, p_ctx->expected_md5);
                    err = ESP_RMAKER_INVALID_STATE;
                    if (p_failure_event) {
                        *p_failure_event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_MD5_INVALID;
                    }
                    goto image_handler_end_post_download_checks_complete;
                }
                OSAL_LOGI(TAG, "Image MD5 verified successfully");
            }
        }

#if CONFIG_RMNG_OTA_SIGNATURE_VERIFY_ENABLE
        /* Verify the signature of the partition */
        uint8_t hash[32];
        err = ft_handler->get_sha256_hash(handle, hash);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to get partition hash: error code %d", err);
            goto image_handler_end_end;
        }
        ota_signature_verify_context_t verify_ctx = {
            .signature = p_ctx->signature,
            .hash = {
                .data = hash,
                .len = sizeof(hash)
            }
        };
        err_temp = rmaker_ota_signature_verify_context(&verify_ctx);
        if (err_temp != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to verify signature: error code %d", err_temp);
            err = ESP_RMAKER_INVALID_STATE;
            if (p_failure_event) {
                *p_failure_event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_SIGNATURE_INVALID;
            }
            goto image_handler_end_post_download_checks_complete;
        }
        OSAL_LOGI(TAG, "Signature verified successfully");
#endif /* CONFIG_RMNG_OTA_SIGNATURE_VERIFY_ENABLE */

        /* Perform integration check */
        err = ft_handler->perform_integration_check(handle);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to perform integration check: error code %d", err);
            goto image_handler_end_post_download_checks_complete;
        }

        passed_post_download_checks = true;
        goto image_handler_end_post_download_checks_complete;
    } else {
        /* Abort the OTA update */
        err = ft_handler->on_download_complete(handle, false);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to abort OTA update: error code %d", err);
            goto image_handler_end_end;
        }
    }

image_handler_end_post_download_checks_complete:
    if (passed_post_download_checks) {
        filetype_handler_status_timer_start();
    }
    err_temp = ft_handler->on_post_download_checks_complete(handle, passed_post_download_checks, p_should_reboot);
    if (err_temp != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to post download checks complete: error code %d", (int)err_temp);
        filetype_handler_status_timer_stop();
        err = err_temp;
        goto image_handler_end_end;
    }
    if (*p_should_reboot) {
        filetype_handler_status_timer_stop();
    }
image_handler_end_end:
    if (p_ctx->signature.data != NULL) {
        free(p_ctx->signature.data);
    }
    if (p_ctx->expected_fw_version != NULL) {
        free(p_ctx->expected_fw_version);
    }
    if (p_ctx->expected_md5 != NULL) {
        free(p_ctx->expected_md5);
    }
    return err;
}

/* Public function definitions *************************************************************/

esp_rmaker_error_t image_handler_begin(const char *signature_base64, size_t image_size, const esp_rmaker_ota_ft_ctx_t *ft_handler, const char *fw_version, const char *file_md5, bool resume, size_t resume_offset, image_handler_ctx_t *p_ctx)
{
    if (!p_ctx) {
        OSAL_LOGE(TAG, "Invalid argument: p_ctx=%p", p_ctx);
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    p_ctx->signature.data = NULL;
    p_ctx->signature.len = 0;
    p_ctx->expected_fw_version = NULL;
    p_ctx->expected_md5 = NULL;

    /* Stash the job's declared fw_version so image_handler_end can pass it to verify_image_header. */
    if (fw_version != NULL && fw_version[0] != '\0') {
        size_t sz = strlen(fw_version) + 1;
        p_ctx->expected_fw_version = (char *)OSAL_MALLOC_EXTRAM(sz * sizeof(char));
        if (p_ctx->expected_fw_version == NULL) {
            OSAL_LOGE(TAG, "Failed to allocate expected_fw_version");
            return ESP_RMAKER_NO_MEM;
        }
        memcpy(p_ctx->expected_fw_version, fw_version, sz);
    }

    /* Stash the job's declared MD5 so image_handler_end can run the completion integrity check. */
    if (file_md5 != NULL && file_md5[0] != '\0') {
        size_t sz = strlen(file_md5) + 1;
        p_ctx->expected_md5 = (char *)OSAL_MALLOC_EXTRAM(sz * sizeof(char));
        if (p_ctx->expected_md5 == NULL) {
            OSAL_LOGE(TAG, "Failed to allocate expected_md5");
            err = ESP_RMAKER_NO_MEM;
            goto image_handler_begin_fail;
        }
        memcpy(p_ctx->expected_md5, file_md5, sz);
    }

#if CONFIG_RMNG_OTA_SIGNATURE_VERIFY_ENABLE
    /* Require and convert base64 signature when verification is enabled */
    if (signature_base64 == NULL || strlen(signature_base64) == 0) {
        OSAL_LOGE(TAG, "OTA signature verification enabled but signature is missing");
        return ESP_RMAKER_INVALID_ARG;
    }
    p_ctx->signature.data = esp_rmaker_convert_base64_to_bytes(signature_base64, strlen(signature_base64), &p_ctx->signature.len);
    if (p_ctx->signature.data == NULL) {
        OSAL_LOGE(TAG, "Failed to convert signature to bytes (invalid base64)");
        err = ESP_RMAKER_INVALID_ARG;
        goto image_handler_begin_fail;
    }
#endif /* CONFIG_RMNG_OTA_SIGNATURE_VERIFY_ENABLE */

    /* Begin (or resume) the image. Resume is best-effort: any failure falls back to a
     * fresh begin (full re-download), so an interrupted-resume never fails a job that a
     * normal OTA would have completed. */
    bool resumed = false;
    if (resume && ft_handler->on_download_resume != NULL) {
        err = ft_handler->on_download_resume(&p_ctx->handle, image_size, resume_offset);
        if (err == ESP_RMAKER_OK) {
            resumed = true;
            OSAL_LOGI(TAG, "Resuming download at offset %" PRIu32, (uint32_t)resume_offset);
        } else {
            OSAL_LOGW(TAG, "Resume failed (error %d); falling back to fresh download", (int)err);
            p_ctx->handle = NULL;
        }
    }
    if (!resumed) {
        err = ft_handler->on_download_begin(&p_ctx->handle, image_size);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to begin download: error code %d", (int)err);
            goto image_handler_begin_fail;
        }
    }

    p_ctx->image_size = image_size;
    p_ctx->ft_handler = ft_handler;
    return ESP_RMAKER_OK;

image_handler_begin_fail:
    if (p_ctx->signature.data != NULL) {
        free(p_ctx->signature.data);
        p_ctx->signature.data = NULL;
        p_ctx->signature.len = 0;
    }
    if (p_ctx->expected_fw_version != NULL) {
        free(p_ctx->expected_fw_version);
        p_ctx->expected_fw_version = NULL;
    }
    if (p_ctx->expected_md5 != NULL) {
        free(p_ctx->expected_md5);
        p_ctx->expected_md5 = NULL;
    }
    if (p_ctx->handle != NULL) {
        ft_handler->on_download_complete(p_ctx->handle, false);
        p_ctx->handle = NULL;
    }
    return err;
}

esp_rmaker_error_t image_handler_write_chunk(const image_handler_ctx_t *p_ctx, const uint8_t *data, size_t size, uint32_t offset)
{
    /* Validate the arguments */
    esp_rmaker_ota_ft_download_handle_t handle = p_ctx->handle;
    if (!handle || !data || size == 0) {
        OSAL_LOGE(TAG, "Invalid arguments: handle=%p, data=%p, size=%" PRIu32, handle, data, (uint32_t)size);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Bound against the declared image size: the offset comes from a block id in the
     * incoming data block, and custom filetype handlers receive it verbatim with nothing
     * behind them. Subtraction, not `offset + size`, so the check cannot be wrapped. */
    if (offset > p_ctx->image_size || size > p_ctx->image_size - offset) {
        OSAL_LOGE(TAG, "Chunk out of range: offset %" PRIu32 " + size %" PRIu32 " exceeds image size %" PRIu32,
                  (uint32_t)offset, (uint32_t)size, (uint32_t)p_ctx->image_size);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Write the chunk */
    esp_rmaker_error_t err = p_ctx->ft_handler->on_download_chunk(handle, data, size, offset);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to write chunk: error code %d", err);
        return err;
    }
    return ESP_RMAKER_OK;
}

void image_handler_end_and_cleanup(const image_handler_ctx_t *p_ctx, image_handler_status_t status, image_handler_cleanup_fn_t cleanup_fn)
{
    if (!p_ctx) {
        OSAL_LOGE(TAG, "Invalid argument: p_ctx is NULL");
        return;
    }

    /* Fully initialize: payload defaults to NULL and fixed_data to NULL so no field is
     * ever read uninitialized regardless of which branches below run. */
    ota_job_event_data_t event_data = {
        .event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_UNKNOWN_ERROR,
        .payload = NULL,
        .fixed_data = NULL,
    };
    switch (status) {
    case IMAGE_HANDLER_STATUS_SUCCESS:
        event_data.event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED;
        break;
    case IMAGE_HANDLER_STATUS_FAILED_SETUP:
        event_data.event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_SETUP;
        break;
    case IMAGE_HANDLER_STATUS_FAILED_STREAM_SUBSCRIPTION:
        event_data.event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_STREAM_SUBSCRIPTION;
        break;
    case IMAGE_HANDLER_STATUS_FAILED_UNKNOWN_ERROR:
        event_data.event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_UNKNOWN_ERROR;
        break;
    case IMAGE_HANDLER_STATUS_TIMEOUT:
        event_data.event = OTA_JOB_EVENT_TIMEOUT;
        break;
    default:
        OSAL_LOGW(TAG, "Invalid status, defaulting to failure: %d", status);
        event_data.event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_UNKNOWN_ERROR;
        break;
    }

    bool should_reboot = false;
    /* Default failure event covers any post-download check that doesn't specify
     * something more precise. image_handler_end will overwrite this when it can
     * attribute the failure to a specific step (e.g. header verification). */
    ota_job_event_t failure_event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_FAILED_POST_DOWNLOAD_CHECKS;
    esp_rmaker_error_t err = image_handler_end(p_ctx, status == IMAGE_HANDLER_STATUS_SUCCESS, &should_reboot, &failure_event);
    if (should_reboot) {
        /* Carried as a scalar (fixed_data), not a heap payload, so the event stays
         * payload-free and is served from the static event pool on the FSM side. */
        event_data.fixed_data = (void *) true;
    }
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to end image handler: error code %d", err);
        event_data.event = failure_event;
        event_data.payload = NULL;
        event_data.fixed_data = NULL;
    }

    /* Terminal download->FSM handoff. */
    (void) ota_job_state_post_terminal_event(&event_data);

#if CONFIG_RMNG_OTA_RESUME
    /* Resume-tracker lifecycle: clear ONLY when the download actually completed (all bytes
     * received). SUCCESS means complete - whether or not post-download checks passed, the bytes
     * are fully downloaded so a resume offers nothing (and a bad complete image would re-fail the
     * same way). Any non-SUCCESS outcome (timeout/stream-subscription/setup/unknown) is an
     * INCOMPLETE download, typically transient/network: keep the tracker so a retry, reconnect, or
     * reboot resumes instead of restarting from 0. */
    if (status == IMAGE_HANDLER_STATUS_SUCCESS) {
        esp_rmaker_ota_nvs_resume_clear();
    }
#endif

    if (cleanup_fn) {
        cleanup_fn();
    }
}
