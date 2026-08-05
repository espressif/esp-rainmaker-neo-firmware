/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file hex_str.c
 * @brief Utility functions for converting between byte arrays and hex strings.
 */

/* Declarations */
#include "util/esp_rmaker_convert_hex.h"

/* Standard C headers */
#include <stdio.h>

/* Logging */
#include "osal_log.h"

static const char *TAG = "rmng_hex_str";

/* Public functions **************************************************************/

esp_rmaker_error_t esp_rmaker_convert_bytes_to_hex(const uint8_t *bytes, size_t bytes_len, char *hex_str, size_t hex_str_size)
{
    if (bytes == NULL || hex_str == NULL || hex_str_size < bytes_len * 2 + 1) {
        OSAL_LOGE(TAG, "Invalid arguments: bytes=%p, hex_str=%p, hex_str_size=%d, bytes_len=%d", bytes, hex_str, (int)hex_str_size, (int)bytes_len);
        return ESP_RMAKER_INVALID_ARG;
    }

    for (size_t i = 0; i < bytes_len; i++) {
        snprintf(hex_str + i * 2, 3, "%.2x", bytes[i]);
    }

    hex_str[bytes_len * 2] = '\0';

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_convert_hex_to_bytes(const char *hex_str, size_t hex_str_len, uint8_t *bytes, size_t bytes_len)
{
    if (hex_str == NULL || bytes == NULL) {
        OSAL_LOGE(TAG, "Invalid arguments: hex_str=%p, bytes=%p", hex_str, bytes);
        return ESP_RMAKER_INVALID_ARG;
    }

    size_t out_len = (hex_str_len + 1) / 2;
    if (bytes_len < out_len) {
        OSAL_LOGE(TAG, "Invalid arguments: bytes_len=%d, out_len=%d", (int)bytes_len, (int)out_len);
        return ESP_RMAKER_INVALID_ARG;
    }

    const char *ptr = hex_str;
    size_t bytes_written = 0;

    /* If odd number of hex digits, parse the first nibble separately */
    if ((hex_str_len % 2) != 0) {
        unsigned int val = 0;
        if (sscanf(ptr, "%1x", &val) != 1) {
            OSAL_LOGE(TAG, "Invalid hex string: %s", ptr);
            return ESP_RMAKER_INVALID_ARG;
        }
        bytes[bytes_written++] = (uint8_t)val;
        ptr += 1;
    }

    /* Parse remaining bytes two hex digits at a time */
    while (bytes_written < out_len) {
        unsigned int val = 0;
        if (sscanf(ptr, "%2x", &val) != 1) {
            OSAL_LOGE(TAG, "Invalid hex string: %s", ptr);
            return ESP_RMAKER_INVALID_ARG;
        }
        bytes[bytes_written++] = (uint8_t)val;
        ptr += 2;
    }

    return ESP_RMAKER_OK;
}
