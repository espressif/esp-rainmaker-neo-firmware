/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ota_posix_shared.h
 * @brief POSIX OTA shared definitions
 */

#ifndef OSAL_OTA_POSIX_SHARED_H
#define OSAL_OTA_POSIX_SHARED_H

#include "osal_ota.h"
#include "posix_exit_codes.h"

#ifndef OSAL_OTA_POSIX_PART_PREFIX
#define OSAL_OTA_POSIX_PART_PREFIX "ota_"
#endif

#ifndef OSAL_OTA_POSIX_PART_COUNT
#define OSAL_OTA_POSIX_PART_COUNT 2
#endif

#ifndef OSAL_OTA_POSIX_BASE_DIR
#define OSAL_OTA_POSIX_BASE_DIR "partitions"
#endif

#define OSAL_OTA_POSIX_MAX_LABEL_LEN 32
#define OSAL_OTA_POSIX_DEFAULT_PART_SIZE (1024 * 1024)
#define OSAL_OTA_POSIX_CONFIG_FILE "ota_config.bin"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the path to the partition image file
 *
 * @param[in] partition_idx The index of the partition
 * @param[out] out_filename The path to the partition image file
 * @param[in] max_len The maximum length of the path
 * @return OSAL_ERR_OK on success, error code otherwise
 */
osal_err_t osal_ota_build_partition_path(uint8_t partition_idx, char *out_filename, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // OSAL_OTA_POSIX_SHARED_H
