/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_filetype_handler_internal.c
 * @brief Internal filetype handler implementation
 */

/* Includes ******************************************************************/

/* Standard includes */
#include <stddef.h>
#include <inttypes.h>
#include <string.h>

/* Declarations includes */
#include "ota_filetype_handler_internal.h"

/* Platform includes */
#include "osal_sysinfo.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_event_loop.h"

/* OTA common includes */
#include "osal_ota.h"

/* MQTT common includes */
#include "osal_mqtt_events.h"

/* AWS IoT Jobs includes */
#include "jobs.h"

/* RMNG includes */
#include "esp_rmaker_common_events.h"
#include "ota_timeout_handler.h"
#include "ota_jobs.h"

/* Types ******************************************************************/

/**
 * @brief Default filetype handler context
 */
typedef struct {
    osal_ota_handle_t handle; /**< OTA handle */
    const osal_ota_partition_t *partition; /**< Partition being updated */
    size_t expected_size; /**< Expected size of the download */
} __default_download_ctx_t;

/* Constants ******************************************************************/

/**
 * @brief Status timer interval in milliseconds
 */
#define STATUS_TIMER_INTERVAL 10000

/**
 * @brief Tag for logging
 */
static const char *TAG = "rmng_ota_filetype";

/* Variables ******************************************************************/

/**
 * @brief Status timer handle
 */
static rmaker_ota_timeout_handler_handle_t status_timer_handle = NULL;

/* Private function declarations ************************************************/

/**
 * @brief Status timer callback
 * @param[in] priv_data Private data passed to the callback
 */
static void __status_timer_callback(void *priv_data);

/* Private function definitions ************************************************/

static void __status_timer_callback(void *priv_data)
{
    esp_rmaker_ota_status_details_t status_details;
    esp_rmaker_ota_status_details_fill_failed(&status_details, "Timed out waiting for final status");
    esp_rmaker_ota_report_final_status(&status_details);
}

/* Default callback functions ************************************************/

static esp_rmaker_error_t __default_version_to_uint32(const esp_rmaker_ota_ft_version_t version, uint32_t *version_num)
{
    *version_num = 0;

    /* Validate arguments */
    if (version.str == NULL || version.len == 0) {
        OSAL_LOGE(TAG, "Invalid firmware version '%.*s', version_len=%" PRIu32, (int)version.len, version.str, (uint32_t)version.len);
        return ESP_RMAKER_INVALID_ARG;
    }

    /**
        We expect the firmware version is a string in the format of "x.y[.z]", where x, y, z are integers.
        x=major, y=minor, z=patch. Encoded as version_num = x << 20 | y << 10 | z,
        giving each field 10 bits (range 0-1023). At least "x.y" is required (one dot).
        Segments beyond patch are ignored. Each field must be a non-empty integer <= 1023,
        otherwise INVALID_ARG.
     */
    uint32_t fields[3] = {0}; // major, minor, patch
    int field_idx = 0;
    bool field_has_digit = false;
    for (size_t i = 0; i < version.len; i++) {
        char c = version.str[i];
        if (c == '.') {
            if (!field_has_digit) {
                OSAL_LOGE(TAG, "Invalid firmware version string: %.*s", (int)version.len, version.str);
                return ESP_RMAKER_INVALID_ARG;
            }
            if (field_idx == 2) {
                // Patch already parsed; ignore any segments beyond patch
                break;
            }
            field_idx++;
            field_has_digit = false;
            continue;
        }
        if (c < '0' || c > '9') {
            OSAL_LOGE(TAG, "Invalid firmware version string: %.*s", (int)version.len, version.str);
            return ESP_RMAKER_INVALID_ARG;
        }
        fields[field_idx] = fields[field_idx] * 10 + (c - '0');
        if (fields[field_idx] > 1023) {
            OSAL_LOGE(TAG, "Firmware version field out of range (max 1023): %.*s", (int)version.len, version.str);
            return ESP_RMAKER_INVALID_ARG;
        }
        field_has_digit = true;
    }
    // Need at least "x.y" (one dot) and the last field must be non-empty (no trailing dot)
    if (field_idx < 1 || !field_has_digit) {
        OSAL_LOGE(TAG, "Invalid firmware version string: %.*s", (int)version.len, version.str);
        return ESP_RMAKER_INVALID_ARG;
    }
    *version_num = (fields[0] << 20) | (fields[1] << 10) | fields[2];
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __default_get_version(esp_rmaker_ota_ft_version_t *p_version)
{
    if (!p_version) {
        OSAL_LOGE(TAG, "Invalid argument: p_version=%p", p_version);
        return ESP_RMAKER_INVALID_ARG;
    }

    const char *firmware_version = osal_sysinfo_get_fw_version();
    if (firmware_version == NULL) {
        OSAL_LOGE(TAG, "Failed to get current firmware version");
        return ESP_RMAKER_FAIL;
    }
    p_version->str = firmware_version;
    p_version->len = strlen(firmware_version);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __default_on_download_begin(esp_rmaker_ota_ft_download_handle_t *p_download_handle, size_t expected_size)
{
    if (!p_download_handle) {
        OSAL_LOGE(TAG, "Invalid argument: p_download_handle=%p", p_download_handle);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Allocate the download context */
    __default_download_ctx_t *ctx = (__default_download_ctx_t *)OSAL_CALLOC_EXTRAM(1, sizeof(__default_download_ctx_t));
    if (!ctx) {
        OSAL_LOGE(TAG, "Failed to allocate download context");
        return ESP_RMAKER_NO_MEM;
    }
    ctx->expected_size = expected_size;

    /* Get the next update partition */
    ctx->partition = osal_ota_get_next_update_partition(NULL);
    if (!ctx->partition) {
        OSAL_LOGE(TAG, "Failed to get next update partition");
        return ESP_RMAKER_NOT_FOUND;
    }

    /* Begin the OTA update */
    osal_err_t err = osal_ota_begin(ctx->partition, OSAL_OTA_SIZE_UNKNOWN, &ctx->handle);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to begin OTA update: error code %d", err);
        return ESP_RMAKER_FAIL;
    }

    *p_download_handle = (esp_rmaker_ota_ft_download_handle_t)ctx;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __default_on_download_resume(esp_rmaker_ota_ft_download_handle_t *p_download_handle, size_t expected_size, size_t resume_offset)
{
    if (!p_download_handle) {
        OSAL_LOGE(TAG, "Invalid argument: p_download_handle=%p", p_download_handle);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Allocate the download context */
    __default_download_ctx_t *ctx = (__default_download_ctx_t *)OSAL_CALLOC_EXTRAM(1, sizeof(__default_download_ctx_t));
    if (!ctx) {
        OSAL_LOGE(TAG, "Failed to allocate download context");
        return ESP_RMAKER_NO_MEM;
    }
    ctx->expected_size = expected_size;

    /* Get the next update partition (same one the interrupted download targeted) */
    ctx->partition = osal_ota_get_next_update_partition(NULL);
    if (!ctx->partition) {
        OSAL_LOGE(TAG, "Failed to get next update partition");
        free(ctx);
        return ESP_RMAKER_NOT_FOUND;
    }

    /* Resume the OTA update WITHOUT erasing the partition: the already-written bytes are
     * retained in flash across reboot. erase_size 0 because writes use absolute offsets and
     * the partition was fully erased when the original (fresh) download began. */
    osal_err_t err = osal_ota_resume(ctx->partition, 0, resume_offset, &ctx->handle);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to resume OTA update: error code %d", err);
        free(ctx);
        return ESP_RMAKER_FAIL;
    }

    *p_download_handle = (esp_rmaker_ota_ft_download_handle_t)ctx;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __default_on_download_chunk(esp_rmaker_ota_ft_download_handle_t download_handle, const uint8_t *data, size_t size, size_t offset)
{
    /* Validate the arguments */
    if (!download_handle || !data || size == 0) {
        OSAL_LOGE(TAG, "Invalid argument: download_handle=%p, data=%p, size=%" PRIu32, (void *)download_handle, (void *)data, (uint32_t)size);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Write the chunk to the partition */
    __default_download_ctx_t *ctx = (__default_download_ctx_t *)download_handle;
    osal_err_t err = osal_ota_write_with_offset(ctx->handle, data, size, offset);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to write chunk to partition: error code %d", err);
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __default_on_download_complete(esp_rmaker_ota_ft_download_handle_t download_handle, bool success)
{
    if (!download_handle) {
        OSAL_LOGE(TAG, "Invalid argument: download_handle=%p", (void *)download_handle);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* End the OTA update */
    __default_download_ctx_t *ctx = (__default_download_ctx_t *)download_handle;
    osal_err_t err = success ? osal_ota_end(ctx->handle) : osal_ota_abort(ctx->handle);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to end OTA update: error code %d", err);
        return ESP_RMAKER_FAIL;
    }

    /* Clean up the download context */
    if (!success) {
        free(ctx);
    }

    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __default_get_sha256_hash(esp_rmaker_ota_ft_download_handle_t download_handle, uint8_t hash[32])
{
    if (!download_handle) {
        OSAL_LOGE(TAG, "Invalid argument: download_handle=%p", (void *)download_handle);
        return ESP_RMAKER_INVALID_ARG;
    }

    __default_download_ctx_t *ctx = (__default_download_ctx_t *)download_handle;
    osal_err_t err = osal_ota_get_partition_hash(ctx->partition, ctx->expected_size, OSAL_OTA_HASH_SHA256, hash, 32);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to get partition hash: error code %d", err);
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __default_get_md5_hash(esp_rmaker_ota_ft_download_handle_t download_handle, uint8_t hash[16])
{
    if (!download_handle) {
        OSAL_LOGE(TAG, "Invalid argument: download_handle=%p", (void *)download_handle);
        return ESP_RMAKER_INVALID_ARG;
    }

    __default_download_ctx_t *ctx = (__default_download_ctx_t *)download_handle;
    osal_err_t err = osal_ota_get_partition_hash(ctx->partition, ctx->expected_size, OSAL_OTA_HASH_MD5, hash, OSAL_OTA_HASH_LEN_MD5);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to get partition MD5: error code %d", err);
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __default_verify_image_header(esp_rmaker_ota_ft_download_handle_t download_handle, const char *expected_fw_version)
{
    if (!download_handle) {
        OSAL_LOGE(TAG, "Invalid argument: download_handle=%p", (void *)download_handle);
        return ESP_RMAKER_INVALID_ARG;
    }
    if (expected_fw_version == NULL || expected_fw_version[0] == '\0') {
        /* Job document did not carry a fw_version: refuse rather than blindly accept,
         * since the whole point of this hook is to bind the binary to the job's claim. */
        OSAL_LOGE(TAG, "Refusing to verify image header without an expected fw_version");
        return ESP_RMAKER_INVALID_ARG;
    }
    __default_download_ctx_t *ctx = (__default_download_ctx_t *)download_handle;

    /* Read the just-written partition's embedded descriptor. */
    osal_ota_app_desc_t downloaded;
    osal_err_t err = osal_ota_get_partition_description(ctx->partition, &downloaded);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to read downloaded image descriptor: error code %d", err);
        return ESP_RMAKER_FAIL;
    }

    /* Read the running partition's descriptor - the source of truth for the expected project name. */
    const osal_ota_partition_t *running = osal_ota_get_running_partition();
    if (!running) {
        OSAL_LOGE(TAG, "Failed to get running partition");
        return ESP_RMAKER_FAIL;
    }
    osal_ota_app_desc_t running_desc;
    err = osal_ota_get_partition_description(running, &running_desc);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to read running partition descriptor: error code %d", err);
        return ESP_RMAKER_FAIL;
    }

    /* Project name MUST match the running app's project name. */
    if (strncmp(downloaded.project_name, running_desc.project_name, sizeof(downloaded.project_name)) != 0) {
        OSAL_LOGE(TAG, "Project name mismatch: downloaded='%.*s' running='%.*s'",
                  (int)sizeof(downloaded.project_name), downloaded.project_name,
                  (int)sizeof(running_desc.project_name), running_desc.project_name);
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Version MUST exactly match what the job document declared. */
    if (strncmp(downloaded.version, expected_fw_version, sizeof(downloaded.version)) != 0) {
        OSAL_LOGE(TAG, "Firmware version mismatch: downloaded='%.*s' job-declared='%s'",
                  (int)sizeof(downloaded.version), downloaded.version,
                  expected_fw_version);
        return ESP_RMAKER_INVALID_STATE;
    }

    OSAL_LOGI(TAG, "Image header verified: project='%.*s' version='%.*s'",
              (int)sizeof(downloaded.project_name), downloaded.project_name,
              (int)sizeof(downloaded.version), downloaded.version);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __default_perform_integration_check(esp_rmaker_ota_ft_download_handle_t download_handle)
{
    if (!download_handle) {
        OSAL_LOGE(TAG, "Invalid argument: download_handle=%p", (void *)download_handle);
        return ESP_RMAKER_INVALID_ARG;
    }

    __default_download_ctx_t *ctx = (__default_download_ctx_t *)download_handle;
    osal_err_t err;

    /* Check that version does not match last failed version */
    const osal_ota_partition_t *last_invalid_partition = osal_ota_get_last_invalid_partition();
    if (last_invalid_partition) {
        osal_ota_app_desc_t last_invalid_app_desc;
        err = osal_ota_get_partition_description(last_invalid_partition, &last_invalid_app_desc);
        if (err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to get partition description: error code %d", err);
            return ESP_RMAKER_FAIL;
        }

        osal_ota_app_desc_t current_app_desc;
        err = osal_ota_get_partition_description(ctx->partition, &current_app_desc);
        if (err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to get partition description: error code %d", err);
            return ESP_RMAKER_FAIL;
        }

        if (strcmp(current_app_desc.version, last_invalid_app_desc.version) == 0) {
            OSAL_LOGE(TAG, "This OTA version '%s' matches last failed version '%s' - will not mark as valid", current_app_desc.version, last_invalid_app_desc.version);
            return ESP_RMAKER_FAIL;
        }
    }

    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __default_on_post_download_checks_complete(esp_rmaker_ota_ft_download_handle_t download_handle, bool success, bool *p_should_reboot)
{
    if (!download_handle) {
        OSAL_LOGE(TAG, "Invalid argument: download_handle=%p", (void *)download_handle);
        return ESP_RMAKER_INVALID_ARG;
    }
    __default_download_ctx_t *ctx = (__default_download_ctx_t *)download_handle;

    if (success) {
        /* Set the partition as the next boot partition */
        osal_err_t err = osal_ota_set_boot_partition(ctx->partition);
        if (err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to set partition as next boot partition: error code %d", err);
            return ESP_RMAKER_FAIL;
        }
    }

    /* Clean up the download context */
    free(ctx);

    *p_should_reboot = true;
    return ESP_RMAKER_OK;
}

/* Public function definitions ************************************************/

static const esp_rmaker_ota_ft_ctx_t default_filetype_handler_ctx = {
    /* Versioning handlers */
    .version_to_uint32 = __default_version_to_uint32,
    .get_version = __default_get_version,
    .set_version = NULL, // Not implemented

    /* Download handlers */
    .on_download_begin = __default_on_download_begin,
    .on_download_resume = __default_on_download_resume,
    .on_download_chunk = __default_on_download_chunk,
    .on_download_complete = __default_on_download_complete,

    /* Post download handlers */
    .get_sha256_hash = __default_get_sha256_hash,
    .get_md5_hash = __default_get_md5_hash,
    .verify_image_header = __default_verify_image_header,
    .perform_integration_check = __default_perform_integration_check,
    .on_post_download_checks_complete = __default_on_post_download_checks_complete,

    /* Post reboot handlers */
    .on_post_reboot = NULL, // Not implemented
};

const esp_rmaker_ota_ft_ctx_t *filetype_handler_get_default_ctx(void)
{
    return &default_filetype_handler_ctx;
}

bool filetype_handler_is_valid_ctx(const esp_rmaker_ota_ft_ctx_t *ctx)
{
    if (!ctx) {
        return false;
    }

    /* Booleans for all fields */
    bool has_get_version = ctx->get_version != NULL;
    bool has_version_to_uint32 = ctx->version_to_uint32 != NULL;
    bool has_on_download_begin = ctx->on_download_begin != NULL;
    bool has_on_download_chunk = ctx->on_download_chunk != NULL;
    bool has_on_download_complete = ctx->on_download_complete != NULL;
    bool has_get_sha256_hash = ctx->get_sha256_hash != NULL;
    bool has_perform_integration_check = ctx->perform_integration_check != NULL;
    bool has_on_post_download_checks_complete = ctx->on_post_download_checks_complete != NULL;
    // bool has_on_post_reboot = ctx->on_post_reboot != NULL;

    /* Ensure all required fields are present */
    if (!has_on_download_begin ||
            !has_on_download_chunk ||
            !has_on_download_complete ||
            !has_get_sha256_hash ||
            !has_perform_integration_check ||
            !has_on_post_download_checks_complete) {
        OSAL_LOGE(TAG, "Invalid filetype handler context: missing required fields (check for 0)\n"
                  "on_download_begin: %d\n"
                  "on_download_chunk: %d\n"
                  "on_download_complete: %d\n"
                  "get_sha256_hash: %d\n"
                  "perform_integration_check: %d\n"
                  "on_post_download_checks_complete: %d\n",
                  has_on_download_begin,
                  has_on_download_chunk,
                  has_on_download_complete,
                  has_get_sha256_hash,
                  has_perform_integration_check,
                  has_on_post_download_checks_complete);
        return false;
    }

    /* Ensure both version handlers are present or both are absent */
    if (has_get_version ^ has_version_to_uint32) {
        OSAL_LOGE(TAG, "Invalid filetype handler context: version handlers are not present or both are present\n"
                  "get_version: %d\n"
                  "version_to_uint32: %d\n",
                  has_get_version,
                  has_version_to_uint32);
        return false;
    }

    return true;
}

esp_rmaker_error_t filetype_handler_status_timer_start(void)
{
    if (!status_timer_handle) {
        /* Initialize the status timer */
        rmaker_ota_timeout_handler_config_t config = {
            .timeout_ms = STATUS_TIMER_INTERVAL,
            .callback = __status_timer_callback,
            .priv_data = NULL,
        };
        esp_rmaker_error_t err = rmaker_ota_timeout_handler_init(&config, &status_timer_handle);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to initialize status timer: %d", (int)err);
            return err;
        }
    }

    /* Restart the status timer */
    return rmaker_ota_timeout_handler_restart(status_timer_handle);
}

esp_rmaker_error_t filetype_handler_status_timer_stop(void)
{
    if (!status_timer_handle) {
        return ESP_RMAKER_OK;
    }

    esp_rmaker_error_t err = rmaker_ota_timeout_handler_deinit(status_timer_handle);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to deinitialize status timer: %d", (int)err);
        return err;
    }
    status_timer_handle = NULL;
    return ESP_RMAKER_OK;
}
