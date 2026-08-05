/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_check.h
 * @brief Lightweight POSIX shim for ESP-IDF check macros (vendored code).
 */

#ifndef __ESP_CHECK_H__
#define __ESP_CHECK_H__

#include "esp_err.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef unlikely
#if defined(__GNUC__) || defined(__clang__)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define unlikely(x) (x)
#endif
#endif

#define ESP_RETURN_ON_FALSE(a, err_code, log_tag, format, ...)                         \
    do {                                                                               \
        if (unlikely(!(a))) {                                                          \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            return err_code;                                                           \
        }                                                                              \
    } while (0)

#define ESP_GOTO_ON_FALSE(a, err_code, goto_tag, log_tag, format, ...)                 \
    do {                                                                               \
        if (unlikely(!(a))) {                                                          \
            ESP_LOGE(log_tag, "%s(%d): " format, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            ret = err_code;                                                            \
            goto goto_tag;                                                             \
        }                                                                              \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* __ESP_CHECK_H__ */
