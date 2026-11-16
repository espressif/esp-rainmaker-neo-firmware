/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up Wi-Fi station mode using the credentials from Kconfig and block until an IP is acquired.
 *
 * Idempotent: subsequent calls return ESP_OK immediately once connected.
 *
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if no IP was acquired within specified timeout, or the underlying esp_wifi error.
 */
esp_err_t test_wifi_connect(void);

#ifdef __cplusplus
}
#endif
