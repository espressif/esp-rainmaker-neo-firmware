/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_log.h
 * @brief POSIX logging macros for vendored ESP-IDF components on POSIX.
 */

#pragma once

#include "osal_log.h"

#define ESP_LOGE(tag, fmt, ...) OSAL_LOGE(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) OSAL_LOGW(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) OSAL_LOGD(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) OSAL_LOGI(tag, fmt, ##__VA_ARGS__)

#define ESP_LOG_DEBUG OSAL_LOG_LEVEL_DEBUG
#define ESP_LOG_INFO OSAL_LOG_LEVEL_INFO
#define ESP_LOG_WARN OSAL_LOG_LEVEL_WARN
#define ESP_LOG_ERROR OSAL_LOG_LEVEL_ERROR
#define ESP_LOG_VERBOSE OSAL_LOG_LEVEL_VERBOSE

#define ESP_LOG_BUFFER_HEX_LEVEL(tag, buf, len, level) OSAL_LOG_BUFFER_HEX(tag, buf, len, level)
