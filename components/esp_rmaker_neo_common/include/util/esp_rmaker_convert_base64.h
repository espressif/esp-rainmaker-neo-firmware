/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_convert_base64.h
 * @brief Utility functions for converting between different data types.
 */

#ifndef __ESP_RMAKER_CONVERT_BASE64_H__
#define __ESP_RMAKER_CONVERT_BASE64_H__

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
 * @brief Convert bytes to a base64 string.
 * @param[in] bytes The bytes to convert.
 * @param[in] bytes_len The length of the bytes to convert.
 * @param[out] base64_str_len The length of the base64 string to store the result in.
 * @return The base64 string on success, NULL otherwise.
 * @note The caller is responsible for freeing the base64 string using free().
 */
char *esp_rmaker_convert_bytes_to_base64(const uint8_t *bytes, size_t bytes_len, size_t *base64_str_len);

/**
 * @brief Convert a base64 string to bytes.
 * @param[in] base64_str The base64 string to convert.
 * @param[in] base64_str_len The length of the base64 string to convert, excluding the null terminator.
 * @param[out] bytes_len The length of the bytes to store the result in.
 * @return The bytes on success, NULL otherwise.
 * @note The caller is responsible for freeing the bytes using free().
 */
uint8_t *esp_rmaker_convert_base64_to_bytes(const char *base64_str, size_t base64_str_len, size_t *bytes_len);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_CONVERT_BASE64_H__ */
