/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file nvs_esp_impl.c
 * @brief ESP NVS implementation.
 */

/* Includes *******************************************************/

/* Standard includes. */
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

/* Component includes. */
#include "osal_storage.h"

/* ESP-IDF includes. */
#include "nvs_flash.h"
#include "nvs.h"

/* Static functions *******************************************************/

static osal_err_t to_nvs_common_error(esp_err_t err)
{
    switch (err) {
    /* Success */
    case ESP_OK:
        return OSAL_ERR_OK;

    /* Not initialized */
    case ESP_ERR_NVS_NOT_INITIALIZED:
        return OSAL_ERR_NVS_NOT_INITIALIZED;

    /* "Not found" should be handled by the caller */

    /* Partition not found */
    case ESP_ERR_NVS_PART_NOT_FOUND:
        return OSAL_ERR_NVS_PARTITION_NOT_FOUND;

    /* Invalid parameter */
    case ESP_ERR_NVS_INVALID_NAME:
    case ESP_ERR_NVS_INVALID_HANDLE:
        return OSAL_ERR_INVALID_ARG;

    /* No memory */
    case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
        return OSAL_ERR_NO_MEM;

    /* Other errors */
    default:
        return OSAL_ERR_FAIL;
    }
}

static nvs_type_t to_esp_nvs_type(osal_storage_type_t type)
{
    switch (type) {
    case OSAL_STORAGE_TYPE_BINARY:
        return NVS_TYPE_BLOB;
    case OSAL_STORAGE_TYPE_U8:
        return NVS_TYPE_U8;
    case OSAL_STORAGE_TYPE_U16:
        return NVS_TYPE_U16;
    case OSAL_STORAGE_TYPE_I32:
        return NVS_TYPE_I32;
    default:
        return NVS_TYPE_BLOB; /* Default fallback */
    }
}

/* Function declarations *******************************************************/

osal_err_t osal_storage_init(char *partition_label)
{
    /* Default to default partition if NULL. */
    char *label = partition_label;
    if (label == NULL) {
        label = NVS_DEFAULT_PART_NAME;
    }

    esp_err_t err = nvs_flash_init_partition(label);

    if ( ( err == ESP_ERR_NVS_NO_FREE_PAGES ) ||
            ( err == ESP_ERR_NVS_NEW_VERSION_FOUND ) ) {
        /* NVS partition was truncated
         * and needs to be erased */
        err = nvs_flash_erase();
        if ( err != ESP_OK ) {
            return to_nvs_common_error(err);
        }

        /* Retry nvs_flash_init */
        err = nvs_flash_init_partition(label);
    }
    return to_nvs_common_error(err);
}

osal_err_t osal_storage_deinit(char *partition_label)
{
    /* Default to default partition if NULL. */
    char *label = partition_label;
    if (label == NULL) {
        label = NVS_DEFAULT_PART_NAME;
    }
    return to_nvs_common_error(nvs_flash_deinit_partition(label));
}

osal_err_t osal_storage_reset(char *partition_label)
{
    /* Default to default partition if NULL. */
    char *label = partition_label;
    if (label == NULL) {
        label = NVS_DEFAULT_PART_NAME;
    }
    return to_nvs_common_error(nvs_flash_erase_partition(label));
}

osal_err_t osal_storage_open(const char *partition_label, const char *name_space, osal_storage_open_mode_t mode, osal_storage_handle_t *p_handle)
{
    if (name_space == NULL || p_handle == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    const char *partition_label_to_use = partition_label;
    if ( partition_label == NULL ) {
        partition_label_to_use = NVS_DEFAULT_PART_NAME;
    }
    nvs_open_mode_t esp_mode = (mode == OSAL_STORAGE_OPEN_READONLY) ? NVS_READONLY : NVS_READWRITE;
    esp_err_t err = nvs_open_from_partition(partition_label_to_use, name_space, esp_mode, (nvs_handle_t *) p_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return OSAL_ERR_NVS_NAMESPACE_NOT_FOUND;
    }
    if (err == ESP_ERR_NVS_PART_NOT_FOUND) {
        return OSAL_ERR_NVS_PARTITION_NOT_FOUND;
    }
    return to_nvs_common_error(err);
}

osal_err_t osal_storage_close(osal_storage_handle_t handle)
{
    nvs_close((nvs_handle_t) handle);
    return OSAL_ERR_OK;
}

osal_err_t osal_storage_get(osal_storage_handle_t handle, const char *key, void *value, size_t *p_value_len, osal_storage_type_t type)
{
    if ( handle == NULL || key == NULL || p_value_len == NULL ) {
        return OSAL_ERR_INVALID_ARG;
    }
    esp_err_t err;
    switch (type) {
    case OSAL_STORAGE_TYPE_BINARY:
        err = nvs_get_blob((nvs_handle_t) handle, key, value, p_value_len);
        break;
    case OSAL_STORAGE_TYPE_U8:
        err = nvs_get_u8((nvs_handle_t) handle, key, (uint8_t *) value);
        *p_value_len = sizeof(uint8_t);
        break;
    case OSAL_STORAGE_TYPE_U16:
        err = nvs_get_u16((nvs_handle_t) handle, key, (uint16_t *) value);
        *p_value_len = sizeof(uint16_t);
        break;
    case OSAL_STORAGE_TYPE_I32:
        err = nvs_get_i32((nvs_handle_t) handle, key, (int32_t *) value);
        *p_value_len = sizeof(int32_t);
        break;
    default:
        return OSAL_ERR_INVALID_ARG;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return OSAL_ERR_NVS_KEY_NOT_FOUND;
    }
    return to_nvs_common_error(err);
}

osal_err_t osal_storage_set(osal_storage_handle_t handle, const char *key, const void *value, size_t value_len, osal_storage_type_t type)
{
    if ( handle == NULL || key == NULL ) {
        return OSAL_ERR_INVALID_ARG;
    }
    /* A zero-length blob is valid, and NULL data is meaningful for it. Every other type dereferences value, so NULL is
       rejected for them (matches the POSIX backend). */
    if ( value == NULL && !(type == OSAL_STORAGE_TYPE_BINARY && value_len == 0) ) {
        return OSAL_ERR_INVALID_ARG;
    }
    esp_err_t err;
    switch (type) {
    case OSAL_STORAGE_TYPE_BINARY:
        err = nvs_set_blob((nvs_handle_t) handle, key, value, value_len);
        break;
    case OSAL_STORAGE_TYPE_U8:
        if (value_len != sizeof(uint8_t)) {
            return OSAL_ERR_INVALID_ARG;
        }
        err = nvs_set_u8((nvs_handle_t) handle, key, *((uint8_t *) value));
        break;
    case OSAL_STORAGE_TYPE_U16:
        if (value_len != sizeof(uint16_t)) {
            return OSAL_ERR_INVALID_ARG;
        }
        err = nvs_set_u16((nvs_handle_t) handle, key, *((uint16_t *) value));
        break;
    case OSAL_STORAGE_TYPE_I32:
        err = nvs_set_i32((nvs_handle_t) handle, key, *((int32_t *) value));
        break;
    default:
        return OSAL_ERR_INVALID_ARG;
    }
    return to_nvs_common_error(err);
}

osal_err_t osal_storage_erase(osal_storage_handle_t handle, const char *key)
{
    if ( handle == NULL || key == NULL ) {
        return OSAL_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_erase_key((nvs_handle_t) handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return OSAL_ERR_NVS_KEY_NOT_FOUND;
    }
    return to_nvs_common_error(err);
}

osal_err_t osal_storage_erase_all(osal_storage_handle_t handle)
{
    if ( handle == NULL ) {
        return OSAL_ERR_INVALID_ARG;
    }
    return to_nvs_common_error(nvs_erase_all((nvs_handle_t) handle));
}

osal_err_t osal_storage_commit(osal_storage_handle_t handle)
{
    if ( handle == NULL ) {
        return OSAL_ERR_INVALID_ARG;
    }
    return to_nvs_common_error(nvs_commit((nvs_handle_t) handle));
}

osal_err_t osal_storage_entry_find(const char *partition_label, const char *name_space, osal_storage_type_t type, osal_storage_iterator_t *iterator)
{
    if (name_space == NULL || iterator == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    const char *partition_label_to_use = partition_label;
    if (partition_label == NULL) {
        partition_label_to_use = NVS_DEFAULT_PART_NAME;
    }

    esp_err_t err = nvs_entry_find(partition_label_to_use, name_space, to_esp_nvs_type(type), (nvs_iterator_t *) iterator);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return OSAL_ERR_NVS_KEY_NOT_FOUND;
    }
    if (err == ESP_ERR_NVS_PART_NOT_FOUND) {
        return OSAL_ERR_NVS_PARTITION_NOT_FOUND;
    }
    return to_nvs_common_error(err);
}

osal_err_t osal_storage_entry_get_info(osal_storage_iterator_t iterator, osal_storage_entry_t *p_entry)
{
    if (iterator == NULL || p_entry == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    nvs_entry_info_t esp_entry;
    esp_err_t err = nvs_entry_info((nvs_iterator_t) iterator, &esp_entry);
    if (err != ESP_OK) {
        return to_nvs_common_error(err);
    }

    /* Copy the key to the common entry structure */
    size_t key_len = strlen(esp_entry.key);
    if (key_len >= OSAL_STORAGE_KEY_MAX_LENGTH) {
        key_len = OSAL_STORAGE_KEY_MAX_LENGTH - 1; /* Leave space for null terminator */
    }
    memcpy(p_entry->key, esp_entry.key, key_len);
    p_entry->key[key_len] = '\0';

    return OSAL_ERR_OK;
}

osal_err_t osal_storage_entry_next(osal_storage_iterator_t *iterator)
{
    if (iterator == NULL || *iterator == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_entry_next((nvs_iterator_t *) iterator);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return OSAL_ERR_NVS_KEY_NOT_FOUND;
    }
    return to_nvs_common_error(err);
}

osal_err_t osal_storage_release_iterator(osal_storage_iterator_t iterator)
{
    nvs_release_iterator((nvs_iterator_t) iterator);
    return OSAL_ERR_OK;
}
