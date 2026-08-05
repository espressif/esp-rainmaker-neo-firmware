/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_convert_hex.h
 * @brief Utility functions for converting between byte arrays and hex strings.
 */

#ifndef __ESP_RMAKER_CONVERT_HEX_H__
#define __ESP_RMAKER_CONVERT_HEX_H__

/* Standard C headers */
#include <stddef.h>
#include <stdint.h>

/* Error types */
#include "esp_rmaker_error_types.h"

/* Public function declarations *************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert bytes to a hex string.
 * @param[in] bytes The bytes to convert.
 * @param[in] bytes_len The length of the bytes to convert.
 * @param[out] hex_str The hex string to store the result in.
 * @param[in] hex_str_size The size of the hex string to store the result in.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if bytes or hex_str is NULL, or hex_str_size is smaller
 *         than bytes_len * 2 + 1.
 */
esp_rmaker_error_t esp_rmaker_convert_bytes_to_hex(const uint8_t *bytes, size_t bytes_len, char *hex_str, size_t hex_str_size);

/**
 * @brief Convert a hex string to bytes.
 * @param[in] hex_str The hex string to convert.
 * @param[in] hex_str_len The length of the hex string to convert, excluding the null terminator.
 * @param[out] bytes The bytes to store the result in.
 * @param[in] bytes_size The size of the bytes to store the result in.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if hex_str or bytes is NULL, bytes_size is too small, or
 *         hex_str is not valid hex.
 */
esp_rmaker_error_t esp_rmaker_convert_hex_to_bytes(const char *hex_str, size_t hex_str_len, uint8_t *bytes, size_t bytes_size);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_CONVERT_HEX_H__ */
