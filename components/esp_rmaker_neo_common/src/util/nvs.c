/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file nvs.c
 * @brief NVS convenience functions.
 */

#include "util/esp_rmaker_nvs.h"

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "osal_err.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"

static const char *TAG = "rmng_util_nvs";

/* ---------- osal_err_t -> esp_rmaker_error_t ---------- */

static esp_rmaker_error_t rmk_from_nvs_get(osal_err_t err)
{
    if (err == OSAL_ERR_OK) {
        return ESP_RMAKER_OK;
    }
    if (err == OSAL_ERR_NVS_KEY_NOT_FOUND) {
        return ESP_RMAKER_NOT_FOUND;
    }
    return ESP_RMAKER_FAIL;
}

static esp_rmaker_error_t commit_after_set(osal_storage_handle_t h, const char *key, osal_err_t set_err)
{
    if (set_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to set %s in NVS handle: %s", key, osal_err_strerror(set_err));
        return ESP_RMAKER_FAIL;
    }
    osal_err_t err = osal_storage_commit(h);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to commit %s in NVS handle: %s", key, osal_err_strerror(err));
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

static void log_open_failed(const char *partition_name, const char *name_space, osal_err_t err)
{
    if (err == OSAL_ERR_NVS_NAMESPACE_NOT_FOUND) {
        OSAL_LOGD(TAG, "NVS namespace not found for %s:%s", partition_name, name_space);
        return;
    }

    OSAL_LOGE(TAG, "Failed to open NVS handle for %s:%s: %s", partition_name, name_space, osal_err_strerror(err));
}

/**
 * Read a blob: probe length, allocate, read. Optionally append a trailing zero after data_len bytes.
 * On success sets *out_len to the blob length (excluding optional trailing nul).
 */
static uint8_t *nvs_read_blob_alloc(osal_storage_handle_t h, const char *key, size_t *out_len, bool nul_terminate)
{
    size_t len = 0;
    osal_err_t err = osal_storage_get(h, key, NULL, &len, OSAL_STORAGE_TYPE_BINARY);
    if (err == OSAL_ERR_NVS_KEY_NOT_FOUND) {
        OSAL_LOGD(TAG, "Key '%s' not found", key);
        return NULL;
    }
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to query length for '%s': %s", key, osal_err_strerror(err));
        return NULL;
    }

    const size_t alloc = nul_terminate ? len + 1 : len;
    uint8_t *buf = OSAL_CALLOC_EXTRAM(alloc, sizeof(uint8_t));
    if (buf == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate buffer for '%s'", key);
        return NULL;
    }

    err = osal_storage_get(h, key, buf, &len, OSAL_STORAGE_TYPE_BINARY);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to read '%s': %s", key, osal_err_strerror(err));
        free(buf);
        return NULL;
    }
    if (nul_terminate) {
        buf[len] = '\0';
    }
    *out_len = len;
    return buf;
}

esp_rmaker_error_t esp_rmaker_load_nvs_handle(const char *partition_name, const char *name_space,
        osal_storage_handle_t *p_nvs_handle)
{
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READWRITE, p_nvs_handle);
    if (err == OSAL_ERR_NVS_PARTITION_NOT_FOUND) {
        OSAL_LOGE(TAG, "Partition '%s' not found", partition_name);
        return ESP_RMAKER_FAIL;
    }
    if (err == OSAL_ERR_NVS_NAMESPACE_NOT_FOUND) {
        OSAL_LOGD(TAG, "Namespace '%s' not found in partition '%s'", name_space, partition_name);
        return ESP_RMAKER_FAIL;
    }
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to open partition '%s' namespace '%s': %s", partition_name, name_space,
                  osal_err_strerror(err));
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_clear_nvs_namespace(const char *partition_name, const char *name_space)
{
    osal_storage_handle_t nvs_handle;
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READWRITE, &nvs_handle);
    if (err == OSAL_ERR_NVS_NAMESPACE_NOT_FOUND) {
        OSAL_LOGD(TAG, "Namespace %s not found. No need to clear.", name_space);
        return ESP_RMAKER_OK;
    }
    if (err != OSAL_ERR_OK) {
        log_open_failed(partition_name, name_space, err);
        return ESP_RMAKER_FAIL;
    }

    err = osal_storage_erase_all(nvs_handle);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to erase all for namespace %s: %s", name_space, osal_err_strerror(err));
        osal_storage_close(nvs_handle);
        return ESP_RMAKER_FAIL;
    }
    err = osal_storage_commit(nvs_handle);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to commit erase for namespace %s: %s", name_space, osal_err_strerror(err));
        osal_storage_close(nvs_handle);
        return ESP_RMAKER_FAIL;
    }
    osal_storage_close(nvs_handle);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_nvs_update_bool_with_handle(osal_storage_handle_t nvs_handle, const char *key, bool value)
{
    if (nvs_handle == NULL || key == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    uint8_t v = value ? 1U : 0U;
    osal_err_t err = osal_storage_set(nvs_handle, key, &v, sizeof(v), OSAL_STORAGE_TYPE_BINARY);
    return commit_after_set(nvs_handle, key, err);
}

esp_rmaker_error_t esp_rmaker_nvs_update_bool(const char *partition_name, const char *name_space, const char *key,
        bool value)
{
    if (name_space == NULL || key == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_storage_handle_t h;
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READWRITE, &h);
    if (err != OSAL_ERR_OK) {
        log_open_failed(partition_name, name_space, err);
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_error_t ret = esp_rmaker_nvs_update_bool_with_handle(h, key, value);
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update %s:%s:%s in NVS", partition_name, name_space, key);
    }
    osal_storage_close(h);
    return ret;
}

esp_rmaker_error_t esp_rmaker_nvs_get_bool_with_handle(osal_storage_handle_t nvs_handle, const char *key, bool *value)
{
    if (nvs_handle == NULL || key == NULL || value == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    uint8_t v = 0;
    size_t len = sizeof(v);
    osal_err_t err = osal_storage_get(nvs_handle, key, &v, &len, OSAL_STORAGE_TYPE_BINARY);
    esp_rmaker_error_t r = rmk_from_nvs_get(err);
    if (r == ESP_RMAKER_NOT_FOUND) {
        OSAL_LOGD(TAG, "Key '%s' not found", key);
    } else if (r != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get %s from NVS handle: %s", key, osal_err_strerror(err));
    } else {
        *value = v ? true : false;
    }
    return r;
}

esp_rmaker_error_t esp_rmaker_nvs_get_bool(const char *partition_name, const char *name_space, const char *key,
        bool *value)
{
    if (name_space == NULL || key == NULL || value == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_storage_handle_t h;
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READONLY, &h);
    if (err != OSAL_ERR_OK) {
        log_open_failed(partition_name, name_space, err);
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_error_t ret = esp_rmaker_nvs_get_bool_with_handle(h, key, value);
    if (ret == ESP_RMAKER_NOT_FOUND) {
        OSAL_LOGD(TAG, "Key '%s' not found in %s:%s", key, partition_name, name_space);
    } else if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get %s:%s:%s from NVS", partition_name, name_space, key);
    }
    osal_storage_close(h);
    return ret;
}

esp_rmaker_error_t esp_rmaker_nvs_update_u16_with_handle(osal_storage_handle_t nvs_handle, const char *key, uint16_t value)
{
    if (nvs_handle == NULL || key == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }
    osal_err_t err = osal_storage_set(nvs_handle, key, &value, sizeof(value), OSAL_STORAGE_TYPE_U16);
    return commit_after_set(nvs_handle, key, err);
}

esp_rmaker_error_t esp_rmaker_nvs_update_u16(const char *partition_name, const char *name_space, const char *key,
        uint16_t value)
{
    if (name_space == NULL || key == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_storage_handle_t h;
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READWRITE, &h);
    if (err != OSAL_ERR_OK) {
        log_open_failed(partition_name, name_space, err);
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_error_t ret = esp_rmaker_nvs_update_u16_with_handle(h, key, value);
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update %s:%s:%s in NVS", partition_name, name_space, key);
    }
    osal_storage_close(h);
    return ret;
}

esp_rmaker_error_t esp_rmaker_nvs_get_u16_with_handle(osal_storage_handle_t nvs_handle, const char *key, uint16_t *value)
{
    if (nvs_handle == NULL || key == NULL || value == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    uint16_t v = 0;
    size_t len = sizeof(v);
    osal_err_t err = osal_storage_get(nvs_handle, key, &v, &len, OSAL_STORAGE_TYPE_U16);
    esp_rmaker_error_t r = rmk_from_nvs_get(err);
    if (r == ESP_RMAKER_NOT_FOUND) {
        OSAL_LOGD(TAG, "Key '%s' not found", key);
    } else if (r != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get %s from NVS handle: %s", key, osal_err_strerror(err));
    } else {
        *value = v;
    }
    return r;
}

esp_rmaker_error_t esp_rmaker_nvs_get_u16(const char *partition_name, const char *name_space, const char *key,
        uint16_t *value)
{
    if (name_space == NULL || key == NULL || value == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_storage_handle_t h;
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READONLY, &h);
    if (err != OSAL_ERR_OK) {
        log_open_failed(partition_name, name_space, err);
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_error_t ret = esp_rmaker_nvs_get_u16_with_handle(h, key, value);
    if (ret == ESP_RMAKER_NOT_FOUND) {
        OSAL_LOGD(TAG, "Key '%s' not found in %s:%s", key, partition_name, name_space);
    } else if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get %s:%s:%s from NVS", partition_name, name_space, key);
    }
    osal_storage_close(h);
    return ret;
}

esp_rmaker_error_t esp_rmaker_nvs_update_int_with_handle(osal_storage_handle_t nvs_handle, const char *key, int value)
{
    if (nvs_handle == NULL || key == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }
    osal_err_t err = osal_storage_set(nvs_handle, key, &value, sizeof(value), OSAL_STORAGE_TYPE_I32);
    return commit_after_set(nvs_handle, key, err);
}

esp_rmaker_error_t esp_rmaker_nvs_update_int(const char *partition_name, const char *name_space, const char *key, int value)
{
    if (name_space == NULL || key == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_storage_handle_t h;
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READWRITE, &h);
    if (err != OSAL_ERR_OK) {
        log_open_failed(partition_name, name_space, err);
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_error_t ret = esp_rmaker_nvs_update_int_with_handle(h, key, value);
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update %s:%s:%s in NVS", partition_name, name_space, key);
    }
    osal_storage_close(h);
    return ret;
}

esp_rmaker_error_t esp_rmaker_nvs_get_int_with_handle(osal_storage_handle_t nvs_handle, const char *key, int *value)
{
    if (nvs_handle == NULL || key == NULL || value == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    int32_t v = -1;
    size_t len = sizeof(v);
    osal_err_t err = osal_storage_get(nvs_handle, key, &v, &len, OSAL_STORAGE_TYPE_I32);
    esp_rmaker_error_t r = rmk_from_nvs_get(err);
    if (r == ESP_RMAKER_NOT_FOUND) {
        OSAL_LOGD(TAG, "Key '%s' not found", key);
    } else if (r != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get %s from NVS handle: %s", key, osal_err_strerror(err));
    } else {
        *value = (int)v;
    }
    return r;
}

esp_rmaker_error_t esp_rmaker_nvs_get_int(const char *partition_name, const char *name_space, const char *key, int *value)
{
    if (name_space == NULL || key == NULL || value == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_storage_handle_t h;
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READONLY, &h);
    if (err != OSAL_ERR_OK) {
        log_open_failed(partition_name, name_space, err);
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_error_t ret = esp_rmaker_nvs_get_int_with_handle(h, key, value);
    if (ret == ESP_RMAKER_NOT_FOUND) {
        OSAL_LOGD(TAG, "Key '%s' not found in %s:%s", key, partition_name, name_space);
    } else if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get %s:%s:%s from NVS", partition_name, name_space, key);
    }
    osal_storage_close(h);
    return ret;
}

int esp_rmaker_nvs_get_int_default_with_handle(osal_storage_handle_t nvs_handle, const char *key, int default_value)
{
    if (nvs_handle == NULL || key == NULL) {
        return default_value;
    }
    int value = default_value;
    return (esp_rmaker_nvs_get_int_with_handle(nvs_handle, key, &value) == ESP_RMAKER_OK) ? value : default_value;
}

int esp_rmaker_nvs_get_int_default(const char *partition_name, const char *name_space, const char *key, int default_value)
{
    if (name_space == NULL || key == NULL) {
        return default_value;
    }

    osal_storage_handle_t h;
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READONLY, &h);
    if (err != OSAL_ERR_OK) {
        log_open_failed(partition_name, name_space, err);
        return default_value;
    }
    int v = esp_rmaker_nvs_get_int_default_with_handle(h, key, default_value);
    osal_storage_close(h);
    return v;
}

esp_rmaker_error_t esp_rmaker_nvs_update_string_with_handle(osal_storage_handle_t nvs_handle, const char *key,
        const char *value)
{
    if (nvs_handle == NULL || key == NULL || value == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }
    osal_err_t err = osal_storage_set(nvs_handle, key, value, strlen(value), OSAL_STORAGE_TYPE_BINARY);
    return commit_after_set(nvs_handle, key, err);
}

esp_rmaker_error_t esp_rmaker_nvs_update_string(const char *partition_name, const char *name_space, const char *key,
        const char *value)
{
    if (name_space == NULL || key == NULL || value == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_storage_handle_t h;
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READWRITE, &h);
    if (err != OSAL_ERR_OK) {
        log_open_failed(partition_name, name_space, err);
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_error_t ret = esp_rmaker_nvs_update_string_with_handle(h, key, value);
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update %s:%s:%s in NVS", partition_name, name_space, key);
    }
    osal_storage_close(h);
    return ret;
}

char *esp_rmaker_nvs_get_string_with_handle(osal_storage_handle_t nvs_handle, const char *key)
{
    if (nvs_handle == NULL || key == NULL) {
        return NULL;
    }
    size_t len = 0;
    return (char *)nvs_read_blob_alloc(nvs_handle, key, &len, true);
}

char *esp_rmaker_nvs_get_string(const char *partition_name, const char *name_space, const char *key)
{
    if (name_space == NULL || key == NULL) {
        return NULL;
    }

    osal_storage_handle_t h;
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READONLY, &h);
    if (err != OSAL_ERR_OK) {
        log_open_failed(partition_name, name_space, err);
        return NULL;
    }
    char *s = esp_rmaker_nvs_get_string_with_handle(h, key);
    osal_storage_close(h);
    return s;
}

uint8_t *esp_rmaker_nvs_get_binary_with_handle(osal_storage_handle_t nvs_handle, const char *key, size_t *data_len)
{
    if (nvs_handle == NULL || key == NULL || data_len == NULL) {
        return NULL;
    }
    return nvs_read_blob_alloc(nvs_handle, key, data_len, true);
}

uint8_t *esp_rmaker_nvs_get_binary(const char *partition_name, const char *name_space, const char *key, size_t *data_len)
{
    if (name_space == NULL || key == NULL || data_len == NULL) {
        return NULL;
    }

    osal_storage_handle_t h;
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READONLY, &h);
    if (err != OSAL_ERR_OK) {
        log_open_failed(partition_name, name_space, err);
        return NULL;
    }
    uint8_t *data = esp_rmaker_nvs_get_binary_with_handle(h, key, data_len);
    osal_storage_close(h);
    return data;
}

esp_rmaker_error_t esp_rmaker_nvs_update_binary_with_handle(osal_storage_handle_t nvs_handle, const char *key, const void *data, size_t data_len)
{
    if (nvs_handle == NULL || key == NULL || (data == NULL && data_len != 0)) {
        return ESP_RMAKER_INVALID_ARG;
    }
    osal_err_t err = osal_storage_set(nvs_handle, key, data, data_len, OSAL_STORAGE_TYPE_BINARY);
    return commit_after_set(nvs_handle, key, err);
}

esp_rmaker_error_t esp_rmaker_nvs_update_binary(const char *partition_name, const char *name_space, const char *key, const void *data, size_t data_len)
{
    if (name_space == NULL || key == NULL || (data == NULL && data_len != 0)) {
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_storage_handle_t h;
    osal_err_t err = osal_storage_open(partition_name, name_space, OSAL_STORAGE_OPEN_READWRITE, &h);
    if (err != OSAL_ERR_OK) {
        log_open_failed(partition_name, name_space, err);
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_error_t ret = esp_rmaker_nvs_update_binary_with_handle(h, key, data, data_len);
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update %s:%s:%s in NVS", partition_name, name_space, key);
    }
    osal_storage_close(h);
    return ret;
}
