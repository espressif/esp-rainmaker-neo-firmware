/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_http_config.h
 * @brief Configuration for the HTTP common component.
 */

#ifndef OSAL_HTTP_CONFIG_H
#define OSAL_HTTP_CONFIG_H

/* ESP-IDF sdkconfig include. */
#include "sdkconfig.h"

/**
 * @brief The default HTTP buffer size.
 */
#define configHTTP_COMMON_BUFFER_SIZE                    ( CONFIG_HTTP_COMMON_BUFFER_SIZE )

/**
 * @brief The default HTTP timeout in milliseconds.
 */
#define configHTTP_COMMON_TIMEOUT_MS                      ( CONFIG_HTTP_COMMON_TIMEOUT_MS )

#endif /* OSAL_HTTP_CONFIG_H */
