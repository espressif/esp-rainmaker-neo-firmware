/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_partition.h
 * @brief Partition utility functions
 */

#ifndef __OTA_PARTITION_H__
#define __OTA_PARTITION_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stdbool.h>

/* Public function declarations ***************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if the running partition is pending verify
 *
 * @return True if the running partition is pending verify, false otherwise
 */
bool esp_rmaker_ota_partition_running_is_pending_verify(void);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_PARTITION_H__ */
