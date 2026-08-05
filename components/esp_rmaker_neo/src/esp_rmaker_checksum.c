/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_checksum.c
 * @brief Implementation of the checksum functions.
 */

/* Include headers **************************************************************/

/* Declarations */
#include "checksum_impl.h"

/* Standard includes */
#include <string.h>

/* NVS includes */
#include "osal_storage.h"
#include "constants/nvs.h"

/* Platform includes */
#include "osal_log.h"

/* Constants ******************************************************/

static const char *TAG = "rmng_checksum";

/* Variables ******************************************************/

/**
 * @brief NVS handle for the checksum namespace.
 */
osal_storage_handle_t __checksum_nvs_handle = NULL;

/* Public function definitions *************************************************/

esp_rmaker_error_t esp_rmaker_checksum_init(void)
{
    osal_err_t nvs_err;
    nvs_err = osal_storage_open(RMAKER_NVS_PART_NAME, RMAKER_NVS_CHECKSUM_NAMESPACE, OSAL_STORAGE_OPEN_READWRITE, &__checksum_nvs_handle);
    if (nvs_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to open NVS handle for checksum namespace");
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_checksum_deinit(void)
{
    if (__checksum_nvs_handle == NULL) {
        return ESP_RMAKER_OK;
    }
    osal_err_t nvs_err = osal_storage_close(__checksum_nvs_handle);
    __checksum_nvs_handle = NULL;
    if (nvs_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to close NVS handle for checksum namespace");
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_checksum_status_t esp_rmaker_checksum_compare(const uint8_t checksum[RMAKER_CHECKSUM_LEN], const char *key)
{
    if (checksum == NULL || key == NULL) {
        OSAL_LOGE(TAG, "Passed NULL pointers to esp_rmaker_checksum_compare: checksum=%p, key=%p.", checksum, key);
        return RMAKER_CHECKSUM_FAILED;
    }

    osal_err_t nvs_err;
    uint8_t hash_nvs[RMAKER_CHECKSUM_LEN];
    size_t hash_nvs_len = RMAKER_CHECKSUM_LEN;
    nvs_err = osal_storage_get(__checksum_nvs_handle, key, hash_nvs, &hash_nvs_len, OSAL_STORAGE_TYPE_BINARY);
    if (nvs_err == OSAL_ERR_NVS_KEY_NOT_FOUND) {
        OSAL_LOGD(TAG, "No checksum found in NVS for key %s (treating as non-existent)", key);
        return RMAKER_CHECKSUM_CHANGED;
    } else if (nvs_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to get checksum from NVS for key %s", key);
        return RMAKER_CHECKSUM_FAILED;
    }

    /* Compare the checksums */
    if (memcmp(checksum, hash_nvs, RMAKER_CHECKSUM_LEN) == 0) {
        OSAL_LOGD(TAG, "Checksum is the same for key %s", key);
        return RMAKER_CHECKSUM_NOT_CHANGED;
    } else {
        OSAL_LOGD(TAG, "Checksum is different for key %s", key);
        return RMAKER_CHECKSUM_CHANGED;
    }

    /* We should never reach here */
    return RMAKER_CHECKSUM_FAILED;
}

esp_rmaker_error_t esp_rmaker_checksum_store(const uint8_t checksum[RMAKER_CHECKSUM_LEN], const char *key)
{
    if (checksum == NULL || key == NULL) {
        OSAL_LOGE(TAG, "Passed NULL pointers to esp_rmaker_checksum_store: checksum=%p, key=%p.", checksum, key);
        return ESP_RMAKER_FAIL;
    }

    osal_err_t nvs_err = osal_storage_set(__checksum_nvs_handle, key, checksum, RMAKER_CHECKSUM_LEN, OSAL_STORAGE_TYPE_BINARY);
    if (nvs_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to set checksum in NVS for key %s", key);
        return ESP_RMAKER_FAIL;
    }
    nvs_err = osal_storage_commit(__checksum_nvs_handle);
    if (nvs_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to commit checksum in NVS for key %s", key);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_checksum_load(const char *key, uint8_t checksum[RMAKER_CHECKSUM_LEN])
{
    if (checksum == NULL || key == NULL) {
        OSAL_LOGE(TAG, "Passed NULL pointers to esp_rmaker_checksum_load: key=%p, checksum=%p.", key, checksum);
        return ESP_RMAKER_INVALID_ARG;
    }

    size_t hash_len = RMAKER_CHECKSUM_LEN;
    osal_err_t nvs_err = osal_storage_get(__checksum_nvs_handle, key, checksum, &hash_len, OSAL_STORAGE_TYPE_BINARY);
    if (nvs_err == OSAL_ERR_NVS_KEY_NOT_FOUND) {
        return ESP_RMAKER_NOT_FOUND;
    } else if (nvs_err != OSAL_ERR_OK || hash_len != RMAKER_CHECKSUM_LEN) {
        OSAL_LOGE(TAG, "Failed to load checksum from NVS for key %s", key);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}
