/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file ota_custom_mock.c
 * @brief Sample custom OTA filetype handler + custom job callback for this example.
 *
 * As this is a pure simulation, the filetype handler downloads the file directly
 * to a dynamically allocated buffer. Keep the OTA filesize small (especially on
 * ESP-IDF) and reduce the download block size instead.
 */

/* Includes ******************************************************************/

/* Declarations includes */
#include "ota_custom_mock.h"

/* Standard includes */
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* Platform common includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_task.h"

/* RMNG OTA includes */
#include "esp_rmaker_ota.h"

/* RMNG util includes */
#include "util/esp_rmaker_crypto.h"
#include "util/esp_rmaker_convert_base64.h"
#include "util/esp_rmaker_nvs.h"
#include "constants/esp_rmaker_nvs_common.h"

/* Constants ******************************************************************/

/** Tag for logging. */
static const char *TAG = "ota_custom_mock";

/** Filetypes handled by this mock. */
#define CUSTOM_MOCK_FILETYPE "mock"
#define CUSTOM_MOCK_FILETYPE_NO_VERSION "mock_no_ver"

/** Version string for the custom mock. */
static char custom_mock_version_str[16] = {0};

/** Versioning handlers *********************************************************
 * These can be omitted if versioning is not required. In particular:
 * - If the version is not required, then provide a NULL function for all three handlers.
 * - If the version is embedded in the file and does not need to be manually persisted, then provide a NULL function for the set_version handler.
 *
 * This mock uses pure non-negative integers (>= 0) stored in NVS.
 */

#define CUSTOM_MOCK_VERSION_NVS_NAMESPACE "custom_mock"
#define CUSTOM_MOCK_VERSION_NVS_KEY "version"

static esp_rmaker_error_t version_to_uint32(const esp_rmaker_ota_ft_version_t version, uint32_t *p_version_num)
{
    uint32_t version_num = 0;
    for (size_t i = 0; i < version.len; i++) {
        char c = version.str[i];
        if (c < '0' || c > '9') {
            OSAL_LOGE(TAG, "Invalid version string: %.*s", (int)version.len, version.str);
            return ESP_RMAKER_INVALID_ARG;
        }
        version_num = version_num * 10 + (c - '0');
    }

    *p_version_num = version_num;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t get_version(esp_rmaker_ota_ft_version_t *p_version)
{
    /* Read the version integer from NVS */
    int custom_mock_version = esp_rmaker_nvs_get_int_default(RMAKER_NVS_PART_NAME, CUSTOM_MOCK_VERSION_NVS_NAMESPACE, CUSTOM_MOCK_VERSION_NVS_KEY, 0);

    /* Write the version string to the version string buffer */
    int len = snprintf(custom_mock_version_str, sizeof(custom_mock_version_str), "%d", custom_mock_version);
    if (len < 0 || len >= sizeof(custom_mock_version_str)) {
        OSAL_LOGE(TAG, "Failed to format version string");
        return ESP_RMAKER_FAIL;
    }

    /* Set the version string and length */
    p_version->str = custom_mock_version_str;
    p_version->len = len;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t set_version(const esp_rmaker_ota_ft_version_t version)
{
    /* Convert the version string to an integer */
    uint32_t custom_mock_version;
    esp_rmaker_error_t err;
    err = version_to_uint32(version, &custom_mock_version);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to convert version string to integer");
        return err;
    }

    /* Write the version integer to NVS */
    err = esp_rmaker_nvs_update_int(RMAKER_NVS_PART_NAME, CUSTOM_MOCK_VERSION_NVS_NAMESPACE, CUSTOM_MOCK_VERSION_NVS_KEY, (int)custom_mock_version);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to set version in NVS");
        return err;
    }
    return ESP_RMAKER_OK;
}

/** Download handlers ***********************************************************
 * These are required:
 * - on_download_begin() is called right before the first chunk of data is passed to the handler:
 *   - You should allocate any necessary resources (e.g., open a partition) and return a handle.
 * - on_download_chunk() is called for each chunk of data received:
 *   - You should write the data to the appropriate destination.
 *   - Return an error if the write fails; this block may be retried.
 *   - There is no guarantee of uniqueness for the offset parameter; it may be called multiple times with the same offset.
 * - on_download_complete() is called when the download is finished (successfully or otherwise):
 *   - You should perform any necessary cleanup and return the result.
 *   - If success is true, the download handle should remain valid for post download checks.
 *   - If success is false, the download handle should be cleaned up and should no longer be used.
 */

/**
 * @brief Download context for the custom mock filetype.
 */
typedef struct {
    uint8_t *buffer; /**< Buffer to store the downloaded data */
    size_t expected_size; /**< Expected size of the downloaded data */
} custom_mock_download_ctx_t;

/**
 * @brief Free the download context.
 * @param[in] ctx The download context to free.
 */
static void free_download_ctx(custom_mock_download_ctx_t *ctx)
{
    if (ctx->buffer) {
        free(ctx->buffer);
    }
    free(ctx);
}

static esp_rmaker_error_t on_download_begin(esp_rmaker_ota_ft_download_handle_t *handle, size_t expected_size)
{
    /* Use a dynamically allocated buffer to store the data */
    custom_mock_download_ctx_t *ctx = OSAL_CALLOC_EXTRAM(1, sizeof(custom_mock_download_ctx_t));
    if (!ctx) {
        OSAL_LOGE(TAG, "Failed to allocate download context");
        return ESP_RMAKER_NO_MEM;
    }
    ctx->buffer = OSAL_CALLOC_EXTRAM(expected_size, sizeof(uint8_t));
    if (!ctx->buffer) {
        OSAL_LOGE(TAG, "Failed to allocate buffer for download");
        free_download_ctx(ctx);
        return ESP_RMAKER_NO_MEM;
    }
    ctx->expected_size = expected_size;
    *handle = (esp_rmaker_ota_ft_download_handle_t)ctx;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t on_download_chunk(esp_rmaker_ota_ft_download_handle_t handle, const uint8_t *data, size_t size, size_t offset)
{
    /* Write to the dynamically allocated buffer */
    custom_mock_download_ctx_t *ctx = (custom_mock_download_ctx_t *)handle;
    if (!ctx->buffer) {
        OSAL_LOGE(TAG, "Invalid download handle");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* A filetype handler is the last thing between the stream and its own buffer, so
     * bound the write here too. Subtraction so the check cannot be wrapped. */
    if (offset > ctx->expected_size || size > ctx->expected_size - offset) {
        OSAL_LOGE(TAG, "Chunk at offset %" PRIu32 " (%" PRIu32 " B) does not fit the %" PRIu32 " B buffer",
                  (uint32_t)offset, (uint32_t)size, (uint32_t)ctx->expected_size);
        return ESP_RMAKER_INVALID_ARG;
    }

    OSAL_LOGI(TAG, "==> Writing chunk %8" PRIu32 " (%8" PRIu32 " B) to buffer", (uint32_t)offset, (uint32_t)size);
    memcpy(ctx->buffer + offset, data, size);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t on_download_complete(esp_rmaker_ota_ft_download_handle_t handle, bool success)
{
    custom_mock_download_ctx_t *ctx = (custom_mock_download_ctx_t *)handle;
    /* If the download failed, we need to cleanup the dynamically allocated buffer here */
    if (!success) {
        OSAL_LOGE(TAG, "Download failed");
        free_download_ctx(ctx);
    }

    return ESP_RMAKER_OK;
}

/* Helper functions **************************************************************/

static esp_rmaker_error_t write_final_success_status(void)
{
    esp_rmaker_ota_status_details_t status_details;
    esp_rmaker_ota_ft_version_t version;
    esp_rmaker_error_t err;
    err = get_version(&version);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get version string");
        return err;
    }
    esp_rmaker_ota_status_details_fill_succeeded(&status_details, CUSTOM_MOCK_FILETYPE, version.str);
    err = esp_rmaker_ota_report_final_status(&status_details);
    if (err == ESP_RMAKER_NOT_CONNECTED) {
        OSAL_LOGI(TAG, "Not connected to MQTT yet, will report final status on MQTT connected event");
        err = ESP_RMAKER_OK;
    }
    return err;
}

/** Post download handlers ******************************************************
 * These are required:
 * - get_sha256_hash() is called after the download is complete:
 *   - You should calculate the SHA256 hash of the downloaded data and return it.
 * - perform_integration_check() is called after the SHA256 hash is calculated:
 *   - You should perform any necessary integration checks and return the result.
 * - on_post_download_checks_complete() is called after the integration checks are complete:
 *   - You should decide if the device should reboot and return the result.
 */

static esp_rmaker_error_t get_sha256_hash(esp_rmaker_ota_ft_download_handle_t handle, uint8_t hash[32])
{
    /* Calculate the SHA256 hash of the downloaded data */
    custom_mock_download_ctx_t *ctx = (custom_mock_download_ctx_t *)handle;
    return esp_rmaker_crypto_gen_sha256(ctx->buffer, ctx->expected_size, hash);
}

static esp_rmaker_error_t perform_integration_check(esp_rmaker_ota_ft_download_handle_t handle)
{
    /* Perform any necessary integration checks */
    custom_mock_download_ctx_t *ctx = (custom_mock_download_ctx_t *)handle;
    OSAL_LOGI(TAG, "Performing integration check");

    /* Here we do XOR of the entire buffer for fun */
    uint8_t *buffer = ctx->buffer;
    uint8_t xor_value = buffer[0];
    for (size_t i = 1; i < ctx->expected_size; i++) {
        buffer[i] ^= xor_value;
    }

    OSAL_LOGI(TAG, "-> XORed buffer byte-wise = 0x%02X", xor_value);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t on_post_download_checks_complete(esp_rmaker_ota_ft_download_handle_t handle, bool success, bool *p_should_reboot)
{
    /* Download context must be cleaned up in all cases before returning */
    custom_mock_download_ctx_t *ctx = (custom_mock_download_ctx_t *)handle;
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    /* Print out the downloaded data as a single base64 string */
    size_t base64_str_len;
    char *base64_str = esp_rmaker_convert_bytes_to_base64(ctx->buffer, ctx->expected_size, &base64_str_len);
    if (!base64_str) {
        OSAL_LOGE(TAG, "Failed to convert downloaded data to base64 string");
        err = ESP_RMAKER_FAIL;
        goto on_post_download_checks_complete_end;
    }
    if (success) {
        OSAL_LOGI(TAG, "[SUCCESS] Downloaded data as base64 string:\n%s", base64_str);
    } else {
        OSAL_LOGE(TAG, "[FAILURE] Downloaded data as base64 string:\n%s", base64_str);
    }
    free(base64_str);

    /* Here we randomize the reboot result. You should NOT do this in a real implementation; only reboot the RMNG node if necessary. */
    bool should_reboot = rand() % 2;
    if (should_reboot) {
        OSAL_LOGI(TAG, "==> Rebooting the RMNG node");

        /* Delay the status reporting to post-reboot */
    } else {
        OSAL_LOGI(TAG, "==> Not rebooting the RMNG node");

        /* Need to report the status within a certain timeout. Here we report a success now since we are not rebooting. */
        err = write_final_success_status();
    }
    *p_should_reboot = should_reboot;

on_post_download_checks_complete_end:
    free_download_ctx(ctx);
    return err;
}

/** Post reboot handlers ********************************************************
 * This is optional if on_post_download_checks_complete() never signals for a reboot.
 * If provided, it is called after the device has rebooted:
 * - You should perform any necessary cleanup and write the final status of the job.
 */

static esp_rmaker_error_t on_post_reboot(void)
{
    /* Write the final success status */
    return write_final_success_status();
}

/* Filetype handler contexts *****************************************************/

/**
 * @brief Context holding the custom mock filetype handler.
 * @note This is owned by the implementation and should not be freed.
 */
static const esp_rmaker_ota_ft_ctx_t custom_mock_ctx = {
    .version_to_uint32 = version_to_uint32,
    .get_version = get_version,
    .set_version = set_version,
    .on_download_begin = on_download_begin,
    .on_download_chunk = on_download_chunk,
    .on_download_complete = on_download_complete,
    .get_sha256_hash = get_sha256_hash,
    .perform_integration_check = perform_integration_check,
    .on_post_download_checks_complete = on_post_download_checks_complete,
    .on_post_reboot = on_post_reboot,
};

static const esp_rmaker_ota_ft_ctx_t custom_mock_ctx_no_version = {
    .version_to_uint32 = NULL,
    .get_version = NULL,
    .set_version = NULL,
    .on_download_begin = on_download_begin,
    .on_download_chunk = on_download_chunk,
    .on_download_complete = on_download_complete,
    .get_sha256_hash = get_sha256_hash,
    .perform_integration_check = perform_integration_check,
    .on_post_download_checks_complete = on_post_download_checks_complete,
    .on_post_reboot = on_post_reboot,
};

/* Public function definitions ****************************************************/

const esp_rmaker_ota_ft_ctx_t *custom_mock_lookup(const char *filetype, size_t filetype_len)
{
    /* We can do a strncmp for each possible filetype */
    if (strncmp(filetype, CUSTOM_MOCK_FILETYPE, filetype_len) == 0) {
        return &custom_mock_ctx;
    }
    if (strncmp(filetype, CUSTOM_MOCK_FILETYPE_NO_VERSION, filetype_len) == 0) {
        return &custom_mock_ctx_no_version;
    }

    /* No match */
    return NULL;
}

#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT

/** Macro to get the length of a string constant. */
#define CONST_STRLEN(str) (sizeof(str) - 1)

/** Status details for an IN_PROGRESS status. */
#define STATUS_DETAILS_IN_PROGRESS "{\"status\": \"in_progress\"}"

/** Status details for a SUCCEEDED status. */
#define STATUS_DETAILS_SUCCESS "{\"status\": \"success\"}"

static void custom_job_callback_task(void *unused)
{
    /* Example: report a InProgress status */
    esp_rmaker_ota_report_custom_job_status(OTA_STATUS_IN_PROGRESS, STATUS_DETAILS_IN_PROGRESS, CONST_STRLEN(STATUS_DETAILS_IN_PROGRESS));

    /* Example: do some work - here we just wait for 10 seconds */
    osal_task_delay( osal_ticks_from_ms(10000) );

    /* Example: report a Succeeded status */
    esp_rmaker_ota_report_custom_job_status(OTA_STATUS_SUCCESS, STATUS_DETAILS_SUCCESS, CONST_STRLEN(STATUS_DETAILS_SUCCESS));
}

esp_rmaker_error_t custom_mock_custom_job_cb(const char *job_doc, size_t job_doc_len)
{
    /* You can process the job document here and report the status using esp_rmaker_ota_report_custom_job_status() */
    OSAL_LOGI(TAG, "Custom job callback called with job document:\n%.*s", (int)job_doc_len, job_doc);

    /* If the job document is invalid, you should return ESP_RMAKER_INVALID_ARG to trigger a Rejected status report. */
    /* Example: look for a specific keyword in the job document */
    if (strstr(job_doc, "REPORT_INVALID") != NULL) {
        OSAL_LOGE(TAG, "Invalid job document");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* You should transfer work to another context outside of this context to avoid blocking the RMNG SDK. */
    /* Example: Create a new task to run the custom job */
    osal_err_t err = osal_task_create(
                         custom_job_callback_task,
                         "custom_job_callback_task",
                         2048,
                         NULL,
                         1,
                         NULL
                     );
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to create custom job callback task: %d", err);

        /* This will trigger a Failed status report (as with any other error). */
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */
