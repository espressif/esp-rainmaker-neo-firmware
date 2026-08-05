/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_ota_status_details.h
 * @brief RMAKER OTA status details.
 */

#ifndef __ESP_RMAKER_OTA_STATUS_DETAILS_H__
#define __ESP_RMAKER_OTA_STATUS_DETAILS_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stdint.h>

/* Configuration includes */
#include "sdkconfig.h"

/* Types **********************************************************************/

typedef enum {
    ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_INVALID = 0,
    ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_STARTING,
    ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_IN_PROGRESS,
    ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_SUCCEEDED,
    ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_FAILED,
    ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_REJECTED,
    ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_DELAYED,
} esp_rmaker_ota_status_details_type_t;

/**
 * @brief Status details for a STARTING update.
 */
typedef struct {
    /** The job ID of the OTA job **/
    const char *job_id;
    /** The job filetype of the OTA job **/
    const char *filetype;
    /** The firmware version of the OTA image **/
    const char *fw_version;
} esp_rmaker_ota_status_details_starting_t;

/**
 * @brief Status details for an IN_PROGRESS update.
 */
typedef struct {
    /** The number of bytes downloaded so far */
    uint32_t downloaded_bytes;
    /** The total number of bytes to download */
    uint32_t total_bytes;
} esp_rmaker_ota_status_details_in_progress_t;

/**
 * @brief Status details for a SUCCEEDED update.
 */
typedef struct {
    /** The job filetype of the OTA job **/
    const char *filetype;
    /** The firmware version of the OTA image **/
    const char *fw_version;
} esp_rmaker_ota_status_details_succeeded_t;

/**
 * @brief Status details for a FAILED update.
 */
typedef struct {
    /** Reason for failure */
    const char *reason;
} esp_rmaker_ota_status_details_failed_t;

/**
 * @brief Status details for a REJECTED update.
 */
typedef struct {
    /** Reason for rejection */
    const char *reason;
} esp_rmaker_ota_status_details_rejected_t;

/**
 * @brief Status details for a DELAYED update.
 */
typedef struct {
    /** Reason for delay */
    const char *reason;
} esp_rmaker_ota_status_details_delayed_t;

/**
 * @brief Union of status details.
 */
typedef union {
    esp_rmaker_ota_status_details_starting_t starting;
    esp_rmaker_ota_status_details_in_progress_t in_progress;
    esp_rmaker_ota_status_details_succeeded_t succeeded;
    esp_rmaker_ota_status_details_failed_t failed;
    esp_rmaker_ota_status_details_rejected_t rejected;
    esp_rmaker_ota_status_details_delayed_t delayed;
} esp_rmaker_ota_status_details_union_t;

/**
 * @brief Status details for an OTA update.
 */
typedef struct {
    esp_rmaker_ota_status_details_type_t type;
    esp_rmaker_ota_status_details_union_t details;
} esp_rmaker_ota_status_details_t;

/* Constants **********************************************************************/

/* Reasons for rejection */
/** Firmware version is required for filetype */
#define ESP_RMAKER_OTA_REJECTED_REASON_FW_VERSION_REQUIRED "Firmware version is required for filetype"

/** Unsupported firmware version */
#define ESP_RMAKER_OTA_REJECTED_REASON_FW_VERSION_UNSUPPORTED "Unsupported firmware version"

/** Firmware version too low */
#define ESP_RMAKER_OTA_REJECTED_REASON_FW_VERSION_TOO_LOW "Firmware version too low"

/** Missing RainMaker Neo-specific fields in job document */
#define ESP_RMAKER_OTA_REJECTED_REASON_JOB_DOC_MISSING_RMNG "Missing RMNG-specific fields in job document"

/** Missing AFR-specific fields in job document */
#define ESP_RMAKER_OTA_REJECTED_REASON_JOB_DOC_MISSING_AFR_OTA "Missing AFR-OTA-specific fields in job document"

/** Missing signature in job document */
#define ESP_RMAKER_OTA_REJECTED_REASON_SIGNATURE_MISSING "Missing signature in job document"

/** Invalid signature in job document */
#define ESP_RMAKER_OTA_REJECTED_REASON_SIGNATURE_INVALID_BASE64 "Invalid base64 signature in job document"

/** Filetype too long */
#define ESP_RMAKER_OTA_REJECTED_REASON_FILETYPE_TOO_LONG "Filetype too long"

/** No custom filetype implementation - This means a lookup function was not provided. */
#define ESP_RMAKER_OTA_REJECTED_REASON_NO_CUSTOM_FILETYPE_IMPL "No custom filetype implementation"

/** Filetype not supported - This means a handler was not found for the filetype using the provided lookup function. */
#define ESP_RMAKER_OTA_REJECTED_REASON_FILETYPE_NOT_SUPPORTED "Filetype not supported"

/** Filetype handler is invalid - This means the provided implementation has missing or invalid fields. */
#define ESP_RMAKER_OTA_REJECTED_REASON_FILETYPE_HANDLER_INVALID "Filetype handler is invalid"

/** Image reference (MQTT stream id) rejected by transport validator. */
#define ESP_RMAKER_OTA_REJECTED_REASON_IMAGE_REFERENCE_INVALID "Image reference is invalid"

/** The job execution can no longer accept updates (deleted or already in a terminal state),
 * so its outcome can never be reported to the cloud. */
#define ESP_RMAKER_OTA_REJECTED_REASON_JOB_UPDATE_UNRECOVERABLE "Job execution no longer accepts updates"

#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
/** Invalid custom job document */
#define ESP_RMAKER_OTA_REJECTED_REASON_INVALID_CUSTOM_JOB_DOCUMENT "Invalid custom job document"
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */

/* Reasons for failure */

/** Image downloader setup failed */
#define ESP_RMAKER_OTA_FAILED_REASON_IMAGE_DOWNLOADER_SETUP_FAILED "Image downloader setup failed"

/** MQTT stream subscription failed */
#define ESP_RMAKER_OTA_FAILED_REASON_MQTT_STREAM_SUBSCRIPTION_FAILED "MQTT stream subscription failed"

/** Post download checks failed */
#define ESP_RMAKER_OTA_FAILED_REASON_POST_DOWNLOAD_CHECKS_FAILED "Post download checks failed"

/** Downloaded image header failed verification (e.g. project name or firmware version mismatch
 *  between the binary's embedded application descriptor and the running app / job document). */
#define ESP_RMAKER_OTA_FAILED_REASON_IMAGE_HEADER_INVALID "Image header invalid"

/** Downloaded image's signature failed cryptographic verification against the codesign cert. */
#define ESP_RMAKER_OTA_FAILED_REASON_SIGNATURE_INVALID "Image signature invalid"

/** Downloaded image's MD5 did not match the file_md5 declared in the job document. */
#define ESP_RMAKER_OTA_FAILED_REASON_MD5_INVALID "Image MD5 mismatch"

/** Custom filetype handler requested a post-download reboot but provides no on_post_reboot
 *  callback, so the job could never terminate. Reported after the reboot. */
#define ESP_RMAKER_OTA_FAILED_REASON_CUSTOM_FILETYPE_HANDLER_NO_POST_REBOOT_HANDLER "Custom filetype handler does not have a post reboot handler even though it was instructed to reboot post-download"

/** Unknown error */
#define ESP_RMAKER_OTA_FAILED_REASON_UNKNOWN_ERROR "Unknown error"

#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
/** Failed to process custom job document */
#define ESP_RMAKER_OTA_FAILED_REASON_FAILED_TO_PROCESS_CUSTOM_JOB_DOCUMENT "Failed to process custom job document"
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */

/* Public function declarations ****************************************************/

#ifdef __cplusplus
extern "C" {
#endif /* __ESP_RMAKER_OTA_STATUS_DETAILS_H__ */
/**
 * @brief Fill a STARTING status details struct.
 * @param[out] status_details The status details struct to fill.
 * @param[in] job_id The job ID of the OTA job.
 * @param[in] filetype The filetype of the OTA job.
 * @param[in] fw_version The firmware version of the OTA image.
 */
void esp_rmaker_ota_status_details_fill_starting(esp_rmaker_ota_status_details_t *status_details, const char *job_id, const char *filetype, const char *fw_version);

/**
 * @brief Fill an IN_PROGRESS status details struct.
 * @param[out] status_details The status details struct to fill.
 * @param[in] downloaded_bytes The number of bytes downloaded so far.
 * @param[in] total_bytes The total number of bytes to download.
 */
void esp_rmaker_ota_status_details_fill_in_progress(esp_rmaker_ota_status_details_t *status_details, uint32_t downloaded_bytes, uint32_t total_bytes);

/**
 * @brief Fill a SUCCEEDED status details struct.
 * @param[out] status_details The status details struct to fill.
 * @param[in] filetype The filetype of the OTA job.
 * @param[in] fw_version The firmware version of the OTA image.
 */
void esp_rmaker_ota_status_details_fill_succeeded(esp_rmaker_ota_status_details_t *status_details, const char *filetype, const char *fw_version);

/**
 * @brief Fill a FAILED status details struct.
 * @param[out] status_details The status details struct to fill.
 * @param[in] reason The reason for failure.
 */
void esp_rmaker_ota_status_details_fill_failed(esp_rmaker_ota_status_details_t *status_details, const char *reason);

/**
 * @brief Fill a REJECTED status details struct.
 * @param[out] status_details The status details struct to fill.
 * @param[in] reason The reason for rejection.
 */
void esp_rmaker_ota_status_details_fill_rejected(esp_rmaker_ota_status_details_t *status_details, const char *reason);

/**
 * @brief Fill a DELAYED status details struct.
 *
 * @note The reason is only carried to the application in ::RMAKER_OTA_EVENT_DELAYED.
 *
 * @param[out] status_details The status details struct to fill.
 * @param[in] reason The reason for delay.
 */
void esp_rmaker_ota_status_details_fill_delayed(esp_rmaker_ota_status_details_t *status_details, const char *reason);

/**
 * @brief Copy a status details struct.
 * @param[in] status_details The status details struct to copy.
 * @return A pointer to the copied status details struct. If not NULL, the caller is responsible for freeing the pointer.
 */
esp_rmaker_ota_status_details_t *esp_rmaker_ota_status_details_copy(const esp_rmaker_ota_status_details_t *status_details);

/**
 * @brief Convert a status details struct to a JSON string.
 * @param[in] status_details The status details struct to convert.
 * @return A pointer to the JSON string. If not NULL, the caller is responsible for freeing the string using free().
 */
char *esp_rmaker_ota_status_details_to_json(const esp_rmaker_ota_status_details_t *status_details);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_OTA_STATUS_DETAILS_H__ */
