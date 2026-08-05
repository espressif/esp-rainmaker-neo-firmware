/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bridge_child_triggers_nvs.c
 * @brief Per-child automation trigger-details persistence implementation.
 */

#include "bridge/bridge_child_triggers_nvs.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

#include "bridge/bridge_internal.h"
#include "constants/nvs.h"
#include "constants/esp_rmaker_nvs_common.h"
#include "util/esp_rmaker_nvs.h"

#include "osal_storage.h"
#include "osal_log.h"

#include <string.h>

static const char *TAG = "rmng_br_child_trig";

/* Resolve the child's NVS key (SHA-256-derived, NUL-terminated within cap). */
static const char *__key_of(esp_rmaker_bridge_child_handle_t child)
{
    const char *key = bridge_internal_child_nvs_key(child);
    if (!key || key[0] == '\0' || strlen(key) > RMAKER_NVS_KEY_LEN_MAX) {
        return NULL;
    }
    return key;
}

static esp_rmaker_error_t __open(osal_storage_open_mode_t mode, osal_storage_handle_t *out_h)
{
    osal_err_t err = osal_storage_open(RMAKER_NVS_PART_NAME,
                                       RMAKER_NVS_BRIDGE_TRIGGERS_NAMESPACE,
                                       mode, out_h);
    if (err != OSAL_ERR_OK) {
        /* A missing namespace is normal before the first write (e.g. on a
         * fresh boot reading triggers that were never persisted). */
        if (err == OSAL_ERR_NVS_NAMESPACE_NOT_FOUND) {
            OSAL_LOGD(TAG, "osal_storage_open(%s): namespace not present yet: %d",
                      RMAKER_NVS_BRIDGE_TRIGGERS_NAMESPACE, (int)err);
        } else {
            OSAL_LOGE(TAG, "osal_storage_open(%s) failed: %d",
                      RMAKER_NVS_BRIDGE_TRIGGERS_NAMESPACE, (int)err);
        }
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t bridge_child_triggers_nvs_set(esp_rmaker_bridge_child_handle_t child,
        const uint8_t *data, size_t data_len)
{
    if (data == NULL && data_len != 0) {
        return ESP_RMAKER_INVALID_ARG;
    }
    const char *key = __key_of(child);
    if (!key) {
        return ESP_RMAKER_INVALID_ARG;
    }
    osal_storage_handle_t h;
    esp_rmaker_error_t err = __open(OSAL_STORAGE_OPEN_READWRITE, &h);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    /* update_binary_with_handle commits internally. */
    err = esp_rmaker_nvs_update_binary_with_handle(h, key, data, data_len);
    osal_storage_close(h);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to persist trigger details for key '%s': %d", key, err);
    }
    return err;
}

uint8_t *bridge_child_triggers_nvs_get(esp_rmaker_bridge_child_handle_t child, size_t *out_len)
{
    if (!out_len) {
        return NULL;
    }
    const char *key = __key_of(child);
    if (!key) {
        return NULL;
    }
    osal_storage_handle_t h;
    if (__open(OSAL_STORAGE_OPEN_READONLY, &h) != ESP_RMAKER_OK) {
        return NULL;
    }
    uint8_t *data = esp_rmaker_nvs_get_binary_with_handle(h, key, out_len);
    osal_storage_close(h);
    return data;
}

esp_rmaker_error_t bridge_child_triggers_nvs_erase(esp_rmaker_bridge_child_handle_t child)
{
    const char *key = __key_of(child);
    if (!key) {
        return ESP_RMAKER_INVALID_ARG;
    }
    osal_storage_handle_t h;
    esp_rmaker_error_t err = __open(OSAL_STORAGE_OPEN_READWRITE, &h);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    /* Best-effort: absent key is fine. */
    if (osal_storage_erase(h, key) == OSAL_ERR_OK) {
        (void)osal_storage_commit(h);
    }
    osal_storage_close(h);
    return ESP_RMAKER_OK;
}

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
