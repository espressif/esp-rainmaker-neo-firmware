/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ota_posix_config.h
 * @brief POSIX OTA configuration management header
 */

#ifndef OSAL_OTA_POSIX_CONFIG_H
#define OSAL_OTA_POSIX_CONFIG_H

#include <stdint.h>
#include "osal_ota.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the boot partition index from config
 *
 * @param[out] partition_idx Boot partition index
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_posix_config_get_boot_partition(uint8_t *partition_idx);

/**
 * @brief Set the boot partition index in config
 *
 * @param[in] partition_idx Boot partition index
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_posix_config_set_boot_partition(uint8_t partition_idx);

/**
 * @brief Get the last valid partition index from config
 *
 * @param[out] partition_idx Last valid partition index
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_posix_config_get_last_valid_partition(uint8_t *partition_idx);

/**
 * @brief Set the last valid partition index in config
 *
 * @param[in] partition_idx Last valid partition index
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_posix_config_set_last_valid_partition(uint8_t partition_idx);

/**
 * @brief Mark current partition as invalid and rollback to last valid
 *
 * @param[in] current_partition_idx Current partition index
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_posix_config_mark_invalid_rollback(uint8_t current_partition_idx);

/**
 * @brief Get the state of a partition from config
 *
 * @param[in] partition_idx Partition index
 * @param[out] state Partition state
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_posix_config_get_partition_state(uint8_t partition_idx, osal_ota_img_states_t *state);

/**
 * @brief Set the state of a partition in config
 *
 * @param[in] partition_idx Partition index
 * @param[in] state Partition state
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_posix_config_set_partition_state(uint8_t partition_idx, osal_ota_img_states_t state);

#ifdef __cplusplus
}
#endif

#endif // OSAL_OTA_POSIX_CONFIG_H
