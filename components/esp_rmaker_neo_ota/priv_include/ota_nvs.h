/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_nvs.h
 * @brief ESP RainMaker Neo OTA NVS helper functions
 */

#ifndef __OTA_NVS_H__
#define __OTA_NVS_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stdbool.h>

/* Constants includes */
#include "constants/ota_nvs_keys.h"

/* NVS utils includess */
#include "util/esp_rmaker_nvs.h"

/* Types ************************************************************************/

/**
 * @brief Flags for the OTA status in NVS
 */
typedef struct {
    /** Whether the OTA has been checked */
    bool is_checked : 1;
    /** Whether the OTA has passed diagnostics */
    bool is_passed : 1;
    /** Reserved for future use */
    uint8_t reserved : 6;
} esp_rmaker_ota_nvs_flags_t;

/* Standard includes */
#include <stdint.h>

/** Length of an MD5 hex string (32 hex chars, no NULL) */
#define ESP_RMAKER_OTA_MD5_HEX_LEN 32

/**
 * @brief Transport used for an in-progress download. Tagged into the resume
 * descriptor so we never resume across a transport change ("no cross-transport resume").
 */
typedef enum {
    ESP_RMAKER_OTA_TRANSPORT_NONE = 0,
    ESP_RMAKER_OTA_TRANSPORT_MQTT = 1,
} esp_rmaker_ota_transport_t;

/**
 * @brief Auto-resume descriptor. Identifies the partially-downloaded image so a
 * persisted progress tracker is only reused for the exact same target image and transport.
 * `md5` is the identity (no job_id - a re-issued job for the same image still resumes).
 */
typedef struct {
    /** Lowercase MD5 hex of the target image (NULL-terminated). */
    char md5_hex[ESP_RMAKER_OTA_MD5_HEX_LEN + 1];
    /** Expected total image size in bytes. */
    uint32_t filesize;
    /** Transport (esp_rmaker_ota_transport_t). */
    uint8_t transport;
    /** MQTT block size the bitmap was built against; 0 for HTTPS. */
    uint32_t block_size;
} esp_rmaker_ota_resume_desc_t;

/* Macros ***********************************************************************/

/**
 * @brief Get the last job ID from NVS
 *
 * @return The last job ID, or NULL if not found. Must be freed by the caller.
 */
#define esp_rmaker_ota_get_last_job_id() esp_rmaker_nvs_get_string(RMAKER_NVS_PART_NAME, RMAKER_NVS_OTA_NAMESPACE, RMAKER_NVS_OTA_KEY_LAST_JOB_ID)

/**
 * @brief Get the last filetype from NVS
 *
 * @return The last filetype, or NULL if not found. Must be freed by the caller.
 */
#define esp_rmaker_ota_get_last_filetype() esp_rmaker_nvs_get_string(RMAKER_NVS_PART_NAME, RMAKER_NVS_OTA_NAMESPACE, RMAKER_NVS_OTA_KEY_LAST_FILETYPE)

/**
 * @brief Get the last expected version number for updating from NVS
 *
 * @return The last version number for updating, or -1 if not found.
 */
#define esp_rmaker_ota_get_last_version() esp_rmaker_nvs_get_int_default(RMAKER_NVS_PART_NAME, RMAKER_NVS_OTA_NAMESPACE, RMAKER_NVS_OTA_KEY_LAST_VERSION, -1)

/**
 * @brief Clear the NVS namespace for OTA
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
#define esp_rmaker_ota_clear_nvs() esp_rmaker_clear_nvs_namespace(RMAKER_NVS_PART_NAME, RMAKER_NVS_OTA_NAMESPACE)

/* Public function declarations ***************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the state of NVS to indicate that a job is pending verification.
 * The NVS values are then used on reboot to verify and update the job.
 *
 * @param[in] job_id The job ID
 * @param[in] filetype The filetype of the OTA image. NULL if not specified.
 * @param[in] next_expected_version The next expected version number for updating
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_ota_nvs_set_job_pending_verification(const char *job_id, const char *filetype, int next_expected_version);

/**
 * @brief Set the status in NVS, only if there is a pending verification job
 *
 * @param[in] is_passed Whether the OTA has passed diagnostics
 * @param[in] override_existing True if the status should be overridden if it already exists
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_ota_nvs_set_status_if_pending_verification(bool is_passed, bool override_existing);

/**
 * @brief Get the status flags from NVS
 *
 * @param[out] status_flags Pointer to the status flags
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_ota_nvs_get_status(esp_rmaker_ota_nvs_flags_t *status_flags);

/**
 * @brief Persist the auto-resume descriptor and progress tracker.
 *
 * Both are written under one NVS handle. `tracker` is the transport-specific
 * progress blob (MQTT bitmap bytes, or an HTTP byte count). Safe to call
 * repeatedly; for MQTT call only on batch boundaries to bound NVS wear.
 *
 * @param[in] desc Resume descriptor (image identity + transport).
 * @param[in] tracker Progress tracker blob.
 * @param[in] tracker_len Length of the tracker blob.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_ota_nvs_resume_save(const esp_rmaker_ota_resume_desc_t *desc, const void *tracker, size_t tracker_len);

/**
 * @brief Load a persisted auto-resume descriptor and progress tracker.
 *
 * @param[out] out_desc Loaded descriptor.
 * @param[out] out_tracker Loaded tracker blob (malloc'd; caller must free on success).
 * @param[out] out_tracker_len Length of the loaded tracker blob.
 * @return ESP_RMAKER_OK on success; ESP_RMAKER_NOT_FOUND if no tracker is stored;
 *         otherwise an error code. On any non-OK return nothing is allocated.
 */
esp_rmaker_error_t esp_rmaker_ota_nvs_resume_load(esp_rmaker_ota_resume_desc_t *out_desc, void **out_tracker, size_t *out_tracker_len);

/**
 * @brief Erase the persisted auto-resume descriptor and progress tracker.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_ota_nvs_resume_clear(void);

/**
 * @brief Whether a loaded descriptor is usable to resume the current job.
 *
 * Compares md5 + filesize + transport; for MQTT also requires equal block_size
 * (bitmap granularity must match). No job_id comparison.
 *
 * @param[in] loaded Descriptor loaded from NVS.
 * @param[in] current Descriptor describing the current job.
 * @return true if the loaded tracker may be reused, false otherwise.
 */
bool esp_rmaker_ota_nvs_resume_matches(const esp_rmaker_ota_resume_desc_t *loaded, const esp_rmaker_ota_resume_desc_t *current);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_NVS_H__ */
