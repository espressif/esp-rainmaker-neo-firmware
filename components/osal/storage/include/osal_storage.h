/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_storage.h
 * @brief Common Non-Volatile Storage (NVS) functions.
 */

#ifndef __NVS_COMMON_H__
#define __NVS_COMMON_H__

/* Includes *******************************************************/

/* Standard includes. */
#include <stddef.h>

/* Error includes. */
#include "osal_err.h"

/* Defines *******************************************************/

#define OSAL_STORAGE_KEY_MAX_LENGTH 16 /**< Maximum length of a key in the NVS partition. */

/* Types *******************************************************/

/**
 * @brief Handle to the NVS partition.
 */
typedef void *osal_storage_handle_t;

/**
 * @brief Open mode for NVS handles.
 */
typedef enum {
    OSAL_STORAGE_OPEN_READONLY = 0, /**< Read-only mode. */
    OSAL_STORAGE_OPEN_READWRITE = 1, /**< Read-write mode. */
} osal_storage_open_mode_t;

/**
 * @brief Type of the value to get or set.
 * @note add more types as needed.
 */
typedef enum {
    OSAL_STORAGE_TYPE_BINARY = 0, /**< Binary value. */
    OSAL_STORAGE_TYPE_U16 = 1, /**< 16-bit unsigned integer. */
    OSAL_STORAGE_TYPE_I32 = 2, /**< 32-bit signed integer. */
    OSAL_STORAGE_TYPE_U8 = 3, /**< 8-bit unsigned integer. */
} osal_storage_type_t;

/**
 * @brief Iterator to the NVS partition.
 */
typedef void *osal_storage_iterator_t;

/**
 * @brief Entry in the NVS partition.
 */
typedef struct {
    char key[OSAL_STORAGE_KEY_MAX_LENGTH];
} osal_storage_entry_t;

/* Function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the NVS module.
 * @param[in] partition_label The label of the partition to initialize.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_storage_init(char *partition_label);

/**
 * @brief Deinitialize the NVS module.
 * @param[in] partition_label The label of the partition to deinitialize.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_storage_deinit(char *partition_label);

/**
 * @brief Reset the NVS partition. This will erase the partition and deinitialize it.
 * @param[in] partition_label The label of the partition to reset.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_storage_reset(char *partition_label);

/**
 * @brief Open the NVS partition.
 * @param[in] partition_name The name of the partition to open.
 * @param[in] name_space The namespace to open.
 * @param[in] mode Open mode for the handle (read-only or read-write).
 * @param[out] p_handle The pointer to the handle to the NVS partition.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_storage_open(const char *partition_name, const char *name_space, osal_storage_open_mode_t mode, osal_storage_handle_t *p_handle);

/**
 * @brief Close the NVS partition.
 * @param[in] handle The handle to the NVS partition.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_storage_close(osal_storage_handle_t handle);

/**
 * @brief Get a value from the NVS partition.
 * @param[in] handle The handle to the NVS partition.
 * @param[in] key The key to get the value for.
 * @param[out] value Pointer to the value to get. If the type is OSAL_STORAGE_TYPE_BINARY,
 *                   a NULL pointer can be passed to query the required length only.
 * @param[out] p_value_len The pointer to the length of the value. On a binary query with a
 *                         NULL @p value, the required length is returned here.
 * @param[in] type The type of the value to get.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_storage_get(osal_storage_handle_t handle, const char *key, void *value, size_t *p_value_len, osal_storage_type_t type);

/**
 * @brief Set a value in the NVS partition.
 * @param[in] handle The handle to the NVS partition.
 * @param[in] key The key to set the value for.
 * @param[in] value Pointer to the value to set.
 * @param[in] value_len The length of the value.
 * @param[in] type The type of the value to set.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_storage_set(osal_storage_handle_t handle, const char *key, const void *value, size_t value_len, osal_storage_type_t type);

/**
 * @brief Erase a value from the NVS partition.
 * @param[in] handle The handle to the NVS partition.
 * @param[in] key The key to erase.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_storage_erase(osal_storage_handle_t handle, const char *key);

/**
 * @brief Erase all values from the NVS partition.
 * @param[in] handle The handle to the NVS partition.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_storage_erase_all(osal_storage_handle_t handle);

/**
 * @brief Commit the changes to the NVS partition.
 * @param[in] handle The handle to the NVS partition.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_storage_commit(osal_storage_handle_t handle);

/**
 * @brief Get an iterator to the entries of a provided type in the NVS partition.
 * @param[in] partition_label The label of the partition to find the entries in.
 * @param[in] name_space The name space to find the entries in.
 * @param[in] type The type of the value to find.
 * @param[out] iterator The iterator to the NVS partition.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_storage_entry_find(const char *partition_label, const char *name_space, osal_storage_type_t type, osal_storage_iterator_t *iterator);

/**
 * @brief Get the information about the current entry in the NVS partition at the current position of the iterator.
 * @param[in] iterator The iterator to the NVS partition.
 * @param[out] p_entry The entry to the NVS partition.
 * @return OSAL_ERR_OK on success, OSAL_ERR_NVS_KEY_NOT_FOUND if the iterator is at the end of the partition, otherwise an error code.
 */
osal_err_t osal_storage_entry_get_info(osal_storage_iterator_t iterator, osal_storage_entry_t *p_entry);

/**
 * @brief Move the iterator to the next entry in the NVS partition.
 * @param[in,out] iterator The iterator to the NVS partition. The iterator is updated to the next entry.
 * @return OSAL_ERR_OK on success, OSAL_ERR_NVS_KEY_NOT_FOUND if there are no more entries, otherwise an error code.
 */
osal_err_t osal_storage_entry_next(osal_storage_iterator_t *iterator);

/**
 * @brief Release the iterator.
 * @param[in] iterator The iterator to release.
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
osal_err_t osal_storage_release_iterator(osal_storage_iterator_t iterator);

#ifdef __cplusplus
}
#endif

#endif /* __NVS_COMMON_H__ */
