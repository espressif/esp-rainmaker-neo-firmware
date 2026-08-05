/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_nvs.c
 * @brief ESP RainMaker Neo OTA NVS helper functions
 */

/* Includes **********************************************************************/

/* Declarations */
#include "ota_nvs.h"

/* Standard includes */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* NVS common (per-key erase) */
#include "osal_storage.h"

/* Constants ********************************************************************/

#define RMAKER_OTA_STATUS_FLAG_IS_CHECKED (1 << 0)
#define RMAKER_OTA_STATUS_FLAG_IS_PASSED (1 << 1)

/* Private function declarations ***************************************************/

/**
 * @brief Convert a uint16_t value to esp_rmaker_ota_nvs_flags_t
 *
 * @param[in] value The uint16_t value to convert
 * @return The esp_rmaker_ota_nvs_flags_t value
 */
static esp_rmaker_ota_nvs_flags_t __u16_to_flags(uint16_t value);

/**
 * @brief Convert a esp_rmaker_ota_nvs_flags_t value to a uint16_t
 *
 * @param[in] flags The esp_rmaker_ota_nvs_flags_t value to convert
 * @return The uint16_t value
 */
static uint16_t __flags_to_u16(esp_rmaker_ota_nvs_flags_t flags);

/* Private function definitions ***************************************************/

static esp_rmaker_ota_nvs_flags_t __u16_to_flags(uint16_t value)
{
    esp_rmaker_ota_nvs_flags_t flags = {0};
    flags.is_checked = (value & RMAKER_OTA_STATUS_FLAG_IS_CHECKED) != 0;
    flags.is_passed = (value & RMAKER_OTA_STATUS_FLAG_IS_PASSED) != 0;
    return flags;
}

static uint16_t __flags_to_u16(esp_rmaker_ota_nvs_flags_t flags)
{
    uint16_t value = 0;
    if (flags.is_checked) {
        value |= RMAKER_OTA_STATUS_FLAG_IS_CHECKED;
    }
    if (flags.is_passed) {
        value |= RMAKER_OTA_STATUS_FLAG_IS_PASSED;
    }
    return value;
}

/* Public function definitions ***************************************************/

esp_rmaker_error_t esp_rmaker_ota_nvs_set_job_pending_verification(const char *job_id, const char *filetype, int next_expected_version)
{
    if (job_id == NULL || next_expected_version < 0) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;

    /* Load the NVS handle */
    osal_storage_handle_t nvs_handle;
    err = esp_rmaker_load_nvs_handle(RMAKER_NVS_PART_NAME, RMAKER_NVS_OTA_NAMESPACE, &nvs_handle);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    /* Update the NVS values */
    err = esp_rmaker_nvs_update_string_with_handle(nvs_handle, RMAKER_NVS_OTA_KEY_LAST_JOB_ID, job_id);
    if (err != ESP_RMAKER_OK) {
        goto esp_rmaker_ota_nvs_set_job_pending_verification_fail;
    }
    if (filetype != NULL) {
        err = esp_rmaker_nvs_update_string_with_handle(nvs_handle, RMAKER_NVS_OTA_KEY_LAST_FILETYPE, filetype);
        if (err != ESP_RMAKER_OK) {
            goto esp_rmaker_ota_nvs_set_job_pending_verification_fail;
        }
    }
    err = esp_rmaker_nvs_update_int_with_handle(nvs_handle, RMAKER_NVS_OTA_KEY_LAST_VERSION, next_expected_version);
    if (err != ESP_RMAKER_OK) {
        goto esp_rmaker_ota_nvs_set_job_pending_verification_fail;
    }
    err = esp_rmaker_nvs_update_u16_with_handle(nvs_handle, RMAKER_NVS_OTA_KEY_STATUS_FLAGS, __flags_to_u16((esp_rmaker_ota_nvs_flags_t) {
        .is_checked = false, .is_passed = false
    }));
    if (err != ESP_RMAKER_OK) {
        goto esp_rmaker_ota_nvs_set_job_pending_verification_fail;
    }

esp_rmaker_ota_nvs_set_job_pending_verification_fail:
    /* Close and return */
    osal_storage_close(nvs_handle);
    return err;
}

esp_rmaker_error_t esp_rmaker_ota_nvs_set_status_if_pending_verification(bool is_passed, bool override_existing)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    /* Load the NVS handle */
    osal_storage_handle_t nvs_handle;
    err = esp_rmaker_load_nvs_handle(RMAKER_NVS_PART_NAME, RMAKER_NVS_OTA_NAMESPACE, &nvs_handle);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    do {
        /* Check if there is a pending verification job */
        char *job_id = esp_rmaker_nvs_get_string_with_handle(nvs_handle, RMAKER_NVS_OTA_KEY_LAST_JOB_ID);
        if (job_id == NULL || job_id[0] == '\0') {
            break;
        }
        int next_expected_version = esp_rmaker_nvs_get_int_default_with_handle(nvs_handle, RMAKER_NVS_OTA_KEY_LAST_VERSION, -1);
        if (next_expected_version < 0) {
            break;
        }

        /* Check if the status flags are set */
        uint16_t status_flags_u16 = 0;
        err = esp_rmaker_nvs_get_u16_with_handle(nvs_handle, RMAKER_NVS_OTA_KEY_STATUS_FLAGS, &status_flags_u16);
        if (err != ESP_RMAKER_OK) {
            break;
        }
        esp_rmaker_ota_nvs_flags_t status_flags = __u16_to_flags(status_flags_u16);
        if (!override_existing && status_flags.is_checked) {
            break;
        }

        /* Set the checked and passed status */
        status_flags.is_checked = true;
        status_flags.is_passed = is_passed;
        err = esp_rmaker_nvs_update_u16_with_handle(nvs_handle, RMAKER_NVS_OTA_KEY_STATUS_FLAGS, __flags_to_u16(status_flags));
        if (err != ESP_RMAKER_OK) {
            break;
        }
    } while (0);

    /* Close and return */
    osal_storage_close(nvs_handle);
    return err;
}

esp_rmaker_error_t esp_rmaker_ota_nvs_get_status(esp_rmaker_ota_nvs_flags_t *status_flags)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    *status_flags = (esp_rmaker_ota_nvs_flags_t) {
        0
    };

    /* Load the u16 value from NVS */
    uint16_t status_flags_u16 = 0;
    err = esp_rmaker_nvs_get_u16(RMAKER_NVS_PART_NAME, RMAKER_NVS_OTA_NAMESPACE, RMAKER_NVS_OTA_KEY_STATUS_FLAGS, &status_flags_u16);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    *status_flags = __u16_to_flags(status_flags_u16);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_ota_nvs_resume_save(const esp_rmaker_ota_resume_desc_t *desc, const void *tracker, size_t tracker_len)
{
    if (desc == NULL || (tracker == NULL && tracker_len > 0)) {
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_storage_handle_t nvs_handle;
    esp_rmaker_error_t err = esp_rmaker_load_nvs_handle(RMAKER_NVS_PART_NAME, RMAKER_NVS_OTA_NAMESPACE, &nvs_handle);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    /* Write the tracker first, then the descriptor; load() keys off the descriptor,
     * so a tracker without a descriptor is simply ignored (treated as no resume). */
    err = esp_rmaker_nvs_update_binary_with_handle(nvs_handle, RMAKER_NVS_OTA_KEY_RESUME_DATA, tracker, tracker_len);
    if (err != ESP_RMAKER_OK) {
        goto esp_rmaker_ota_nvs_resume_save_end;
    }
    err = esp_rmaker_nvs_update_binary_with_handle(nvs_handle, RMAKER_NVS_OTA_KEY_RESUME_DESC, desc, sizeof(*desc));

esp_rmaker_ota_nvs_resume_save_end:
    osal_storage_close(nvs_handle);
    return err;
}

esp_rmaker_error_t esp_rmaker_ota_nvs_resume_load(esp_rmaker_ota_resume_desc_t *out_desc, void **out_tracker, size_t *out_tracker_len)
{
    if (out_desc == NULL || out_tracker == NULL || out_tracker_len == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }
    *out_tracker = NULL;
    *out_tracker_len = 0;

    osal_storage_handle_t nvs_handle;
    esp_rmaker_error_t err = esp_rmaker_load_nvs_handle(RMAKER_NVS_PART_NAME, RMAKER_NVS_OTA_NAMESPACE, &nvs_handle);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    esp_rmaker_error_t ret = ESP_RMAKER_NOT_FOUND;
    size_t desc_len = 0;
    uint8_t *desc_blob = esp_rmaker_nvs_get_binary_with_handle(nvs_handle, RMAKER_NVS_OTA_KEY_RESUME_DESC, &desc_len);
    if (desc_blob == NULL || desc_len != sizeof(*out_desc)) {
        free(desc_blob);
        goto esp_rmaker_ota_nvs_resume_load_end;
    }

    size_t tracker_len = 0;
    uint8_t *tracker_blob = esp_rmaker_nvs_get_binary_with_handle(nvs_handle, RMAKER_NVS_OTA_KEY_RESUME_DATA, &tracker_len);
    if (tracker_blob == NULL) {
        free(desc_blob);
        goto esp_rmaker_ota_nvs_resume_load_end;
    }

    memcpy(out_desc, desc_blob, sizeof(*out_desc));
    free(desc_blob);
    *out_tracker = tracker_blob;
    *out_tracker_len = tracker_len;
    ret = ESP_RMAKER_OK;

esp_rmaker_ota_nvs_resume_load_end:
    osal_storage_close(nvs_handle);
    return ret;
}

esp_rmaker_error_t esp_rmaker_ota_nvs_resume_clear(void)
{
    osal_storage_handle_t nvs_handle;
    esp_rmaker_error_t err = esp_rmaker_load_nvs_handle(RMAKER_NVS_PART_NAME, RMAKER_NVS_OTA_NAMESPACE, &nvs_handle);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    /* Erase both keys; ignore NOT_FOUND so clearing an absent tracker is a no-op. */
    osal_storage_erase(nvs_handle, RMAKER_NVS_OTA_KEY_RESUME_DESC);
    osal_storage_erase(nvs_handle, RMAKER_NVS_OTA_KEY_RESUME_DATA);
    osal_storage_commit(nvs_handle);

    osal_storage_close(nvs_handle);
    return ESP_RMAKER_OK;
}

bool esp_rmaker_ota_nvs_resume_matches(const esp_rmaker_ota_resume_desc_t *loaded, const esp_rmaker_ota_resume_desc_t *current)
{
    if (loaded == NULL || current == NULL) {
        return false;
    }
    if (loaded->transport == ESP_RMAKER_OTA_TRANSPORT_NONE || loaded->transport != current->transport) {
        return false;
    }
    if (loaded->filesize != current->filesize) {
        return false;
    }
    if (strncmp(loaded->md5_hex, current->md5_hex, ESP_RMAKER_OTA_MD5_HEX_LEN + 1) != 0) {
        return false;
    }
    /* MQTT bitmap granularity must match: each bit maps to a different byte range otherwise. */
    if (loaded->transport == ESP_RMAKER_OTA_TRANSPORT_MQTT && loaded->block_size != current->block_size) {
        return false;
    }
    return true;
}
