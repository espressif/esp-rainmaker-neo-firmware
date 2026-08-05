/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_ota_filetype_handler.h
 * @brief Public header for the filetype handler functionality
 */

#ifndef __ESP_RMAKER_OTA_FILETYPE_HANDLER_H__
#define __ESP_RMAKER_OTA_FILETYPE_HANDLER_H__

/* Includes ******************************************************************/

/* Standard includes */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Error includes */
#include "esp_rmaker_error_types.h"

/* OTA includes */
#include "esp_rmaker_ota_status_details.h"

/* Types ******************************************************************/

/**
 * @brief Version string and length
 */
typedef struct {
    const char *str;
    size_t len;
} esp_rmaker_ota_ft_version_t;

/**
 * @brief Download handle
 */
typedef void *esp_rmaker_ota_ft_download_handle_t;

/* Callback Function Typedefs *********************************************/

/**
 * @brief (Optional Pair [1/2]) Versioning handler: Convert a version string to a uint32_t
 *
 * A higher uint32_t value means a higher version.
 * If the version is not required, then provide a NULL function for this handler.
 * @note This MUST be implemented if the get_version handler is implemented, i.e., as a pair.
 *
 * @param[in] version Version to convert
 * @param[out] version_num Version number
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_version_to_uint32_handler_t)(const esp_rmaker_ota_ft_version_t version, uint32_t *version_num);

/**
 * @brief (Optional Pair [2/2]) Versioning handler: Get the current version
 *
 * If the version is not required, then provide a NULL function for this handler.
 * @note This MUST be implemented if the version_to_uint32 handler is implemented, i.e., as a pair.
 *
 * @param[out] p_version Current version
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_get_version_handler_t)(esp_rmaker_ota_ft_version_t *p_version);

/**
 * @brief (Optional) Versioning handler: Set the version after successful integration
 *
 * If version saving is not required, then provide a NULL function for this handler.
 * - e.g., the version is embedded in the file and does not need to be persisted.
 *
 * @param[in] version Version to persist for this filetype
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_set_version_handler_t)(const esp_rmaker_ota_ft_version_t version);

/**
 * @brief (Required) Execution handler: Called when download is beginning
 * @param[out] p_download_handle Download handle.
 * @param[in] expected_size Expected size of the download.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_on_download_begin_handler_t)(esp_rmaker_ota_ft_download_handle_t *p_download_handle, size_t expected_size);

/**
 * @brief (Optional) Execution handler: Resume an interrupted download without discarding already-written data.
 *
 * Called instead of on_download_begin when the SDK has a valid persisted progress tracker for the
 * exact same target image (matched via the job's file_md5). The handler must reopen its destination
 * WITHOUT erasing the bytes already received, ready to accept further on_download_chunk writes
 * (which use absolute offsets). For firmware this maps to osal_ota_resume().
 *
 * Resume is strictly best-effort: if this returns an error, the SDK falls back to a fresh
 * on_download_begin (full re-download). If NULL, the filetype never resumes.
 *
 * @param[out] p_download_handle Download handle.
 * @param[in] expected_size Expected total size of the download.
 * @param[in] resume_offset Number of leading bytes already known-good (sequential transports);
 *                          MQTT passes 0 since it tracks received blocks via a bitmap.
 * @return ESP_RMAKER_OK on success, otherwise an error code (triggers fresh-download fallback).
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_on_download_resume_handler_t)(esp_rmaker_ota_ft_download_handle_t *p_download_handle, size_t expected_size, size_t resume_offset);

/**
 * @brief (Required) Execution handler: Write chunk data to destination
 * @param[in] download_handle Download handle.
 * @param[in] data Data buffer to process
 * @param[in] size Size of data to write
 * @param[in] offset Offset in the stream
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_on_download_chunk_handler_t)(esp_rmaker_ota_ft_download_handle_t download_handle, const uint8_t *data, size_t size, size_t offset);

/**
 * @brief (Required) Execution handler: Called when download is complete
 * @param[in] download_handle Download handle.
 * @param[in] success True if the download succeeded, false otherwise
 * - If success is true, the download handle should remain valid for post download checks.
 * - If success is false, the download handle should be cleaned up and should no longer be used.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_on_download_complete_handler_t)(esp_rmaker_ota_ft_download_handle_t download_handle, bool success);

/**
 * @brief (Required) Execution handler: Get SHA256 hash of the processed data
 * @param[in] download_handle Download handle.
 * @param[out] hash Buffer to store the hash in. Will be pre-allocated to 32 bytes.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_get_sha256_hash_handler_t)(esp_rmaker_ota_ft_download_handle_t download_handle, uint8_t hash[32]);

/**
 * @brief (Optional) Execution handler: Get MD5 hash of the processed data.
 *
 * Used for the optional end-to-end file_md5 integrity check (job document `file_md5`).
 * If NULL, the MD5 completion check is skipped even when the job declares a file_md5.
 *
 * @param[in] download_handle Download handle.
 * @param[out] hash Buffer to store the hash in. Will be pre-allocated to 16 bytes.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_get_md5_hash_handler_t)(esp_rmaker_ota_ft_download_handle_t download_handle, uint8_t hash[16]);

/**
 * @brief (Required) Execution handler: Perform integration check
 *
 * - You should ensure that the downloaded file is integrated into the system before or during this call.
 * - e.g., for firmware images, written to the appropriate partition and passed checksum verifications.
 *
 * @param[in] download_handle Download handle.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_perform_integration_check_handler_t)(esp_rmaker_ota_ft_download_handle_t download_handle);

/**
 * @brief (Optional) Execution handler: Verify the downloaded image's embedded header matches the job's claims.
 *
 * - Compares the embedded project_name against the currently running project_name.
 * - Compares the embedded firmware version against the version declared in the job document.
 * If NULL, the header verification step is skipped (NOT recommended; allows downgrade-by-lying
 * and cross-project flashes).
 *
 * @param[in] download_handle Download handle.
 * @param[in] expected_fw_version Firmware version string declared in the job document (must match exactly).
 *                                May be NULL if the job did not declare one; implementation should decide policy.
 * @return ESP_RMAKER_OK if the image header is valid and matches, otherwise an error code.
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_verify_image_header_handler_t)(esp_rmaker_ota_ft_download_handle_t download_handle, const char *expected_fw_version);

/**
 * @brief (Required) Execution handler: Called when post download checks are complete, which include:
 *
 * - Signature verification using the hash of the processed data
 * - Integration check (e.g., for firmware images, written to the appropriate partition and passed checksum verifications)
 *
 * @note Before this handler is called, a timer is started to wait for a final status to be reported.
 * - This timer is stopped when esp_rmaker_ota_report_final_status() is called.
 * - If the timer expires, the OTA update is considered failed.
 * You should therefore report the final status within this handler, or within an asynchronous operation that is started by this handler.
 * If you are rebooting the device, then this timer is stopped, and restarted by the on_post_reboot handler.
 *
 * @param[in] download_handle Download handle.
 * @param[in] success True if the post download checks succeeded, false otherwise
 * @param[out] p_should_reboot True if the device should reboot, false otherwise
 * @return ESP_RMAKER_OK on success, otherwise an error code
 * @note The download handle should be cleaned up and should no longer be used after this call.
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_on_post_download_checks_complete_handler_t)(esp_rmaker_ota_ft_download_handle_t download_handle, bool success, bool *p_should_reboot);

/**
 * @brief (Optional) Execution handler: Called when the device has rebooted after an OTA update
 *
 * @note Before this handler is called, a timer is started to wait for a final status to be reported.
 * - This timer is stopped when esp_rmaker_ota_report_final_status() is called.
 * - If the timer expires, the OTA update is considered failed.
 * You should therefore report the final status within this handler, or within an asynchronous operation that is started by this handler.
 *
 * @note If your on_post_download_checks_complete handler signals for a reboot, but this handler is not provided, then the job will be explicitly failed.
 * - The reason for this is that the device will not reboot, and the job will therefore be in the IN_PROGRESS state indefinitely.
 * - Ensure that you provide this handler if you signal for a reboot in your on_post_download_checks_complete handler.
 *
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
typedef esp_rmaker_error_t (*esp_rmaker_ota_ft_on_post_reboot_handler_t)(void);

/**
 * @brief Filetype handler context containing all callback handlers
 */
typedef struct {
    /* Versioning handlers */
    esp_rmaker_ota_ft_version_to_uint32_handler_t version_to_uint32;
    esp_rmaker_ota_ft_get_version_handler_t get_version;
    esp_rmaker_ota_ft_set_version_handler_t set_version;

    /* Download handlers */
    esp_rmaker_ota_ft_on_download_begin_handler_t on_download_begin;
    esp_rmaker_ota_ft_on_download_resume_handler_t on_download_resume; /* Optional; NULL = no resume support */
    esp_rmaker_ota_ft_on_download_chunk_handler_t on_download_chunk;
    esp_rmaker_ota_ft_on_download_complete_handler_t on_download_complete;

    /* Post download handlers */
    esp_rmaker_ota_ft_get_sha256_hash_handler_t get_sha256_hash;
    esp_rmaker_ota_ft_get_md5_hash_handler_t get_md5_hash; /* Optional; for the file_md5 integrity check */
    esp_rmaker_ota_ft_verify_image_header_handler_t verify_image_header;
    esp_rmaker_ota_ft_perform_integration_check_handler_t perform_integration_check;
    esp_rmaker_ota_ft_on_post_download_checks_complete_handler_t on_post_download_checks_complete;

    /* Post reboot handlers */
    esp_rmaker_ota_ft_on_post_reboot_handler_t on_post_reboot;
} esp_rmaker_ota_ft_ctx_t;

/* Lookup function ******************************************************************/

/**
 * @brief Lookup function: Get the OTA Filetype handler context
 * @param[in] filetype OTA Filetype to get the context for
 * @param[in] filetype_len Length of the filetype
 * @return OTA Filetype handler context
 */
typedef const esp_rmaker_ota_ft_ctx_t *(*esp_rmaker_ota_ft_lookup_handler_t)(const char *filetype, size_t filetype_len);

/* Asynchronous operations ******************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Report the final status of the OTA update. This uses the current job ID.
 *
 * You should only call this when you are sure that the OTA update has completed successfully or failed.
 * Some possible locations:
 * - Within the on_post_download_checks_complete handler
 * - After the on_post_download_checks_complete handler has started an asynchronous operation, and that operation has completed successfully or failed.
 * - Within the on_post_reboot handler
 * - After the on_post_reboot handler has started an asynchronous operation, and that operation has completed successfully or failed.
 *
 * @note This function will only accept "final" statuses:
 * - SUCCEEDED
 * - FAILED
 * Fill the status details struct with the appropriate fill function from esp_rmaker_ota_status_details.h.
 *
 * @param[in] status_details The status details to report. The status itself is part of this struct.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_ota_report_final_status(const esp_rmaker_ota_status_details_t *status_details);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_OTA_FILETYPE_HANDLER_H__ */
