/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ota.h
 * @brief OTA (Over-The-Air) update common interface.
 *
 * This interface provides abstracted OTA functionality that works across different platforms.
 * On ESP-IDF platforms, it wraps the native esp_ota_* functions.
 * On POSIX platforms, it provides stub implementations focused on download verification.
 */

#ifndef __OTA_COMMON_H__
#define __OTA_COMMON_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "osal_err.h"

/**
 * @brief OTA image states
 */
typedef enum {
    OSAL_OTA_IMG_VALID = 0,           /**< Valid image */
    OSAL_OTA_IMG_UNDEFINED = 1,       /**< Undefined state */
    OSAL_OTA_IMG_INVALID = 2,         /**< Invalid image */
    OSAL_OTA_IMG_ABORTED = 3,         /**< Aborted update */
    OSAL_OTA_IMG_NEW = 4,             /**< New image */
    OSAL_OTA_IMG_PENDING_VERIFY = 5,  /**< Pending verification */
} osal_ota_img_states_t;

/**
 * @brief OTA partition information
 */
typedef struct {
    const char *label;           /**< Partition label/name */
    uint32_t address;            /**< Partition address */
    uint32_t size;               /**< Partition size */
    uint32_t type;               /**< Partition type */
    uint32_t subtype;            /**< Partition subtype */
    bool encrypted;              /**< Whether partition is encrypted */
} osal_ota_partition_t;

/**
 * @brief OTA supported hash types
 */
typedef enum {
    OSAL_OTA_HASH_SHA256 = 0,
    OSAL_OTA_HASH_MD5 = 1,
} osal_ota_hash_type_t;

/**
 * @brief OTA supported hash lengths
 */
#define OSAL_OTA_HASH_LEN_SHA256 32
#define OSAL_OTA_HASH_LEN_MD5 16

/**
 * @brief Application description structure
 */
typedef struct {
    uint32_t magic_word;        /**< Magic word */
    uint32_t secure_version;    /**< Secure version */
    char version[32];           /**< Version string */
    char project_name[32];      /**< Project name */
    uint8_t app_elf_sha256[32]; /**< ELF SHA256 */
} osal_ota_app_desc_t;

/**
 * @brief Opaque handle for an OTA update
 */
typedef void *osal_ota_handle_t;

/**
 * @brief Special value for unknown OTA image size
 */
#define OSAL_OTA_SIZE_UNKNOWN ((size_t)(-1))

/**
 * @brief Flag for sequential writes with incremental erase
 */
#define OSAL_OTA_WITH_SEQUENTIAL_WRITES ((size_t)(-2))

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Begin an OTA update:
 *
 * - Specified partition is erased to the specified size (OSAL_OTA_SIZE_UNKNOWN for unknown size)
 *
 * @param[in] partition Pointer to the partition to update
 * @param[in] image_size Size of the image to be written (use OSAL_OTA_SIZE_UNKNOWN if unknown)
 * @param[out] out_handle Handle for the OTA update operation
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_begin(const osal_ota_partition_t *partition,
                          size_t image_size,
                          osal_ota_handle_t *out_handle);

/**
 * @brief Resume an interrupted OTA update by continuing to write to the specified partition.
 *
 * This function is used when an OTA update was previously started and needs to be resumed after an interruption. It continues the OTA process from the specified offset within the partition.
 * Unlike osal_ota_begin(), this function does not erase the partition which receives the OTA update, but rather expects that part of the image has already been written correctly, and it resumes writing from the given offset.
 *
 * @param[in] partition Pointer to the partition to update
 * @param[in] erase_size Size of the erase to be performed
 * @param[in] image_offset Offset of the image to be written
 * @param[out] out_handle Handle for the OTA update operation
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_resume(const osal_ota_partition_t *partition,
                           const size_t erase_size,
                           const size_t image_offset,
                           osal_ota_handle_t *out_handle);

/**
 * @brief End an OTA update:
 *
 * @param[in] handle OTA handle from osal_ota_begin()
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_end(osal_ota_handle_t handle);

/**
 * @brief Abort an OTA update
 *
 * @param[in] handle OTA handle from osal_ota_begin()
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_abort(osal_ota_handle_t handle);

/**
 * @brief Write data to the OTA partition at an offset.
 *
 * @param[in] handle OTA handle from osal_ota_begin()
 * @param[in] data Pointer to the data to write
 * @param[in] size Size of the data to write
 * @param[in] offset Offset to write the data to
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_write_with_offset(osal_ota_handle_t handle,
                                      const void *data,
                                      size_t size,
                                      uint32_t offset);
/**
 * @brief Set the boot partition
 *
 * @param[in] partition Pointer to the partition to boot from
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_set_boot_partition(const osal_ota_partition_t *partition);

/**
 * @brief Get the current boot partition
 *
 * @return Pointer to the boot partition info, or NULL if error
 */
const osal_ota_partition_t *osal_ota_get_boot_partition(void);

/**
 * @brief Get the currently running partition
 *
 * @return Pointer to the running partition info, or NULL if error
 */
const osal_ota_partition_t *osal_ota_get_running_partition(void);

/**
 * @brief Get the next OTA partition for update
 *
 * @param[in] start_from Starting partition (NULL to use running partition)
 * @return Pointer to the next update partition, or NULL if error
 */
const osal_ota_partition_t *osal_ota_get_next_update_partition(const osal_ota_partition_t *start_from);

/**
 * @brief Get application description for a partition
 *
 * @param[in] partition Pointer to the partition
 * @param[out] app_desc Application description structure
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_get_partition_description(const osal_ota_partition_t *partition,
        osal_ota_app_desc_t *app_desc);

/**
 * @brief Get the hash of a partition
 *
 * @param[in] partition Pointer to the partition
 * @param[in] size_to_hash The size of the data to hash. If 0, the entire partition will be hashed.
 * @param[in] hash_type The type of hash to get
 * @param[out] hash The buffer to store the hash of the partition
 * @param[in] hash_len The length of the buffer to store the hash
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_get_partition_hash(const osal_ota_partition_t *partition,
                                       size_t size_to_hash,
                                       osal_ota_hash_type_t hash_type,
                                       uint8_t *hash,
                                       size_t hash_len);

/**
 * @brief Mark the current application as valid and cancel rollback
 *
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_mark_app_valid_cancel_rollback(void);

/**
 * @brief Mark the current application as invalid and reboot to rollback
 *
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_mark_app_invalid_rollback_and_reboot(void);

/**
 * @brief Get the state of a partition
 *
 * @param[in] partition Pointer to the partition
 * @param[out] ota_state State of the partition
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_get_state_partition(const osal_ota_partition_t *partition,
                                        osal_ota_img_states_t *ota_state);

/**
 * @brief Get the last partition with invalid state
 *
 * @return Pointer to the last invalid partition, or NULL if none
 */
const osal_ota_partition_t *osal_ota_get_last_invalid_partition(void);

/**
 * @brief Get the number of OTA application partitions
 *
 * @return Number of OTA partitions
 */
uint8_t osal_ota_get_app_partition_count(void);

/**
 * @brief Erase the last boot application partition
 *
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_erase_last_boot_app_partition(void);

/**
 * @brief Check if rollback is possible
 *
 * @return true if rollback is possible, false otherwise
 */
bool osal_ota_check_rollback_is_possible(void);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_COMMON_H__ */
