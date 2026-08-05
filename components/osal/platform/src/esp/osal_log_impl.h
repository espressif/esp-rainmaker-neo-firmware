/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __OSAL_LOG_IMPL_H__
#define __OSAL_LOG_IMPL_H__

/**
 * @file osal_log_impl.h
 * @brief Platform common log implementation for ESP-IDF.
 */

#include "esp_log.h"

/* Define the LOG_INTERNAL macro that uses the ESP_LOG_LEVEL_LOCAL macro */
#define LOG_INTERNAL(level, tag, fmt, ...) ESP_LOG_LEVEL_LOCAL((esp_log_level_t) level, tag, fmt, ##__VA_ARGS__)
#define LOG_BUFFER_HEX_INTERNAL(tag, buf, len, level) ESP_LOG_BUFFER_HEX_LEVEL(tag, buf, len, (esp_log_level_t) level)

#endif /* __OSAL_LOG_IMPL_H__ */
