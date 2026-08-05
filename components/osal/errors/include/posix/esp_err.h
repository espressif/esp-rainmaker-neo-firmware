/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_err.h
 * @brief POSIX error type for vendored ESP-IDF components on POSIX.
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>

#include "osal_err.h"

typedef osal_err_t esp_err_t;

#define ESP_OK               OSAL_ERR_OK
#define ESP_FAIL             OSAL_ERR_FAIL
#define ESP_ERR_NO_MEM       OSAL_ERR_NO_MEM
#define ESP_ERR_INVALID_ARG  OSAL_ERR_INVALID_ARG
#define ESP_ERR_INVALID_STATE OSAL_ERR_INVALID_STATE
#define ESP_ERR_NOT_FOUND    OSAL_ERR_NOT_FOUND
#define ESP_ERR_NOT_SUPPORTED OSAL_ERR_NOT_SUPPORTED
#define ESP_ERR_INVALID_RESPONSE OSAL_ERR_INVALID_RESPONSE
#define ESP_ERR_TIMEOUT      OSAL_ERR_TIMEOUT

/**
 * @brief Abort if @p x is not ESP_OK, mirroring ESP-IDF's ESP_ERROR_CHECK for vendored code.
 */
#define ESP_ERROR_CHECK(x)                                                                    \
    do {                                                                                      \
        esp_err_t __err_rc = (x);                                                             \
        if (__err_rc != ESP_OK) {                                                             \
            fprintf(stderr, "ESP_ERROR_CHECK failed: esp_err_t 0x%x at %s:%d\n",              \
                    (int) __err_rc, __FILE__, __LINE__);                                      \
            abort();                                                                          \
        }                                                                                     \
    } while (0)
