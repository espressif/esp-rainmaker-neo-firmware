/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_nvs.h
 * @brief ESP RainMaker Neo NVS convenience functions.
 */

#ifndef __UTIL_ESP_RMAKER_NVS_H__
#define __UTIL_ESP_RMAKER_NVS_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stdint.h>
#include <stdbool.h>

/* Error types includes */
#include "esp_rmaker_error_types.h"

/* NVS includes */
#include "osal_storage.h"

/* Public function declarations *************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load an NVS handle
 * @param[in] partition_name The name of the partition to load
 * @param[in] name_space The namespace to load
 * @param[out] p_nvs_handle The handle to the NVS partition
 * @return ESP_RMAKER_OK on success, otherwise error code
 */
esp_rmaker_error_t esp_rmaker_load_nvs_handle(const char *partition_name, const char *name_space, osal_storage_handle_t *p_nvs_handle);

/**
 * @brief Clear an NVS namespace
 * @param[in] partition_name The name of the partition to clear
 * @param[in] name_space The namespace to clear
 * @return ESP_RMAKER_OK on success, otherwise error code
 */
esp_rmaker_error_t esp_rmaker_clear_nvs_namespace(const char *partition_name, const char *name_space);

/**
 * @brief Update a bool value in NVS with a handle.
 * @param[in] nvs_handle The handle to the NVS partition.
 * @param[in] key The key to update.
 * @param[in] value The bool value to update.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_update_bool_with_handle(osal_storage_handle_t nvs_handle, const char *key, bool value);

/**
 * @brief Update a bool value in NVS.
 * @param[in] partition_name The name of the partition to update.
 * @param[in] name_space The namespace to update.
 * @param[in] key The key to update.
 * @param[in] value The bool value to update.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_update_bool(const char *partition_name, const char *name_space, const char *key, bool value);

/**
 * @brief Get a bool value from NVS with a handle.
 * @param[in] nvs_handle The handle to the NVS partition.
 * @param[in] key The key to get.
 * @param[out] value The bool value to get.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_get_bool_with_handle(osal_storage_handle_t nvs_handle, const char *key, bool *value);

/**
 * @brief Get a bool value from NVS.
 * @param[in] partition_name The name of the partition to get.
 * @param[in] name_space The namespace to get.
 * @param[in] key The key to get.
 * @param[out] value The bool value to get.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_get_bool(const char *partition_name, const char *name_space, const char *key, bool *value);

/**
 * @brief Update a uint16_t value in NVS with a handle.
 * @param[in] nvs_handle The handle to the NVS partition.
 * @param[in] key The key to update.
 * @param[in] value The uint16_t value to update.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_update_u16_with_handle(osal_storage_handle_t nvs_handle, const char *key, uint16_t value);

/**
 * @brief Update a uint16_t value in NVS.
 * @param[in] partition_name The name of the partition to update.
 * @param[in] name_space The namespace to update.
 * @param[in] key The key to update.
 * @param[in] value The uint16_t value to update.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_update_u16(const char *partition_name, const char *name_space, const char *key, uint16_t value);

/**
 * @brief Get a uint16_t value from NVS with a handle.
 * @param[in] nvs_handle The handle to the NVS partition.
 * @param[in] key The key to get.
 * @param[out] value The uint16_t value to get.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_get_u16_with_handle(osal_storage_handle_t nvs_handle, const char *key, uint16_t *value);

/**
 * @brief Get a uint16_t value from NVS.
 * @param[in] partition_name The name of the partition to get.
 * @param[in] name_space The namespace to get.
 * @param[in] key The key to get.
 * @param[out] value The uint16_t value to get.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_get_u16(const char *partition_name, const char *name_space, const char *key, uint16_t *value);

/**
 * @brief Update an int value in NVS with a handle.
 * @param[in] nvs_handle The handle to the NVS partition.
 * @param[in] key The key to update.
 * @param[in] value The int value to update.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_update_int_with_handle(osal_storage_handle_t nvs_handle, const char *key, int value);

/**
 * @brief Update an int value in NVS.
 * @param[in] partition_name The name of the partition to update.
 * @param[in] name_space The namespace to update.
 * @param[in] key The key to update.
 * @param[in] value The int value to update.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_update_int(const char *partition_name, const char *name_space, const char *key, int value);

/**
 * @brief Get an int value from NVS with a handle.
 * @param[in] nvs_handle The handle to the NVS partition.
 * @param[in] key The key to get.
 * @param[out] value The int value to get.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_get_int_with_handle(osal_storage_handle_t nvs_handle, const char *key, int *value);

/**
 * @brief Get an int value from NVS.
 * @param[in] partition_name The name of the partition to get.
 * @param[in] name_space The namespace to get.
 * @param[in] key The key to get.
 * @param[out] value The int value to get.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_get_int(const char *partition_name, const char *name_space, const char *key, int *value);

/**
 * @brief Get an int value from NVS with a default value with a handle.
 * @param[in] nvs_handle The handle to the NVS partition.
 * @param[in] key The key to get.
 * @param[in] default_value The default value to return if the key is not found or if nvs_handle/key is NULL.
 * @return The int value, or the default value if not found, if nvs_handle/key is NULL, or if NVS get fails.
 */
int esp_rmaker_nvs_get_int_default_with_handle(osal_storage_handle_t nvs_handle, const char *key, int default_value);

/**
 * @brief Get an int value from NVS with a default value.
 * @param[in] partition_name The name of the partition to get.
 * @param[in] name_space The namespace to get.
 * @param[in] key The key to get.
 * @param[in] default_value The default value to return if the key is not found.
 * @return The int value, or the default value if not found.
 */
int esp_rmaker_nvs_get_int_default(const char *partition_name, const char *name_space, const char *key, int default_value);

/**
 * @brief Update a string value in NVS with a handle.
 * @param[in] nvs_handle The handle to the NVS partition.
 * @param[in] key The key to update.
 * @param[in] value The string value to update.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_update_string_with_handle(osal_storage_handle_t nvs_handle, const char *key, const char *value);

/**
 * @brief Update a string value in NVS.
 * @param[in] partition_name The name of the partition to update.
 * @param[in] name_space The namespace to update.
 * @param[in] key The key to update.
 * @param[in] value The string value to update.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_update_string(const char *partition_name, const char *name_space, const char *key, const char *value);

/**
 * @brief Get a string value from NVS with a handle.
 * @param[in] nvs_handle The handle to the NVS partition.
 * @param[in] key The key to get.
 * @return The string value, or NULL if not found, if nvs_handle/key is NULL, if NVS get fails, or if memory allocation fails.
 * @note If not NULL, the caller must free the returned string.
 */
char *esp_rmaker_nvs_get_string_with_handle(osal_storage_handle_t nvs_handle, const char *key);

/**
 * @brief Get a string value from NVS.
 * @param[in] partition_name The name of the partition to get.
 * @param[in] name_space The namespace to get.
 * @param[in] key The key to get.
 * @return The string value, or NULL if not found. If not NULL, the caller must free the data.
 */
char *esp_rmaker_nvs_get_string(const char *partition_name, const char *name_space, const char *key);

/**
 * @brief Get a binary value from NVS with a handle.
 * @note This will always allocate an extra byte for the NULL terminator, but returns the length of the actual data.
 * @param[in] nvs_handle The handle to the NVS partition.
 * @param[in] key The key to get.
 * @param[out] data_len The length of the data read.
 * @return The binary value, or NULL if not found. If not NULL, the caller must free the data.
 */
uint8_t *esp_rmaker_nvs_get_binary_with_handle(osal_storage_handle_t nvs_handle, const char *key, size_t *data_len);

/**
 * @brief Get a binary value from NVS.
 * @note This will always allocate an extra byte for the NULL terminator, but returns the length of the actual data.
 * @param[in] partition_name The name of the partition to get.
 * @param[in] name_space The namespace to get.
 * @param[in] key The key to get.
 * @param[out] data_len The length of the data read.
 * @return The binary value, or NULL if not found. If not NULL, the caller must free the data.
 */
uint8_t *esp_rmaker_nvs_get_binary(const char *partition_name, const char *name_space, const char *key, size_t *data_len);

/**
 * @brief Update a binary value in NVS with a handle.
 *
 * Unlike ::esp_rmaker_nvs_update_string this stores exactly ``data_len``
 * bytes verbatim, so the value may contain embedded NULs. Pairs with
 * ::esp_rmaker_nvs_get_binary_with_handle. A zero-length blob is allowed
 * (``data`` may be NULL only when ``data_len`` is 0).
 *
 * @param[in] nvs_handle The handle to the NVS partition.
 * @param[in] key The key to update.
 * @param[in] data Pointer to the binary data.
 * @param[in] data_len Length of the binary data.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_update_binary_with_handle(osal_storage_handle_t nvs_handle, const char *key, const void *data, size_t data_len);

/**
 * @brief Update a binary value in NVS.
 * @param[in] partition_name The name of the partition to update.
 * @param[in] name_space The namespace to update.
 * @param[in] key The key to update.
 * @param[in] data Pointer to the binary data.
 * @param[in] data_len Length of the binary data.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_nvs_update_binary(const char *partition_name, const char *name_space, const char *key, const void *data, size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* __UTIL_ESP_RMAKER_NVS_H__ */
