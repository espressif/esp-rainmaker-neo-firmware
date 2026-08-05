/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file checksum_impl.h
 * @brief Internal checksum implementation.
 * - Checksums generated using SHA-256.
 * - Stored in NVS.
 * - Used to detect changes.
 */

#ifndef __CHECKSUM_IMPL_H__
#define __CHECKSUM_IMPL_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Pre-processor definitions *****************************************************/

#define RMAKER_CHECKSUM_LEN (RMAKER_CRYPTO_SHA256_HASH_LEN)

/* Includes **********************************************************************/

/* Standard includes */
#include <stddef.h>
#include <stdint.h>

/* Crypto includes */
#include "util/esp_rmaker_crypto.h"

/* Types **********************************************************************/

typedef enum {
    RMAKER_CHECKSUM_CHANGED = 0,
    RMAKER_CHECKSUM_NOT_CHANGED = 1,
    RMAKER_CHECKSUM_FAILED = 2,
} esp_rmaker_checksum_status_t;

/* Public function declarations *************************************************/

/**
 * @brief Initialize the checksum module.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_checksum_init(void);

/**
 * @brief Deinitialize the checksum module.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_checksum_deinit(void);

/**
 * @brief Get the checksum of the given data.
 *
 * @param[in] data Pointer to the data to hash.
 * @param[in] data_len Length of the data to hash.
 * @param[out] checksum Pointer to the checksum, of length RMAKER_CHECKSUM_LEN.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
#define esp_rmaker_checksum_generate(data, data_len, checksum) esp_rmaker_crypto_gen_sha256(data, data_len, checksum)

/**
 * @brief Compare the checksum with the stored checksum.
 *
 * @param[in] checksum Pointer to the checksum to compare.
 * @param[in] key Key to load the checksum from NVS.
 *
 * @return RMAKER_CHECKSUM_CHANGED if the checksum is different/non-existent, RMAKER_CHECKSUM_NOT_CHANGED if the checksum is the same, RMAKER_CHECKSUM_FAILED if the checksum comparison fails.
 */
esp_rmaker_checksum_status_t esp_rmaker_checksum_compare(const uint8_t checksum[RMAKER_CHECKSUM_LEN], const char *key);

/**
 * @brief Store the checksum in NVS.
 *
 * @param[in] checksum Pointer to the checksum to store.
 * @param[in] key Key to store the checksum in NVS.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_checksum_store(const uint8_t checksum[RMAKER_CHECKSUM_LEN], const char *key);

/**
 * @brief Load the stored checksum from NVS.
 *
 * @param[in] key Key to load the checksum from NVS.
 * @param[out] checksum Buffer of length RMAKER_CHECKSUM_LEN, filled on success.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_NOT_FOUND if no checksum is
 *         stored for the key, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_checksum_load(const char *key, uint8_t checksum[RMAKER_CHECKSUM_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* __CHECKSUM_IMPL_H__ */
