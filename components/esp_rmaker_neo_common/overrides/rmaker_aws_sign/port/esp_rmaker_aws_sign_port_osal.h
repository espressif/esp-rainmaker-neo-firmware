/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_aws_sign_port_osal.h
 * @brief osal-backed port for the rmaker_aws_sign component.
 *
 * Routes the component's heap, clock, mutexes, HTTP and log at osal, so firmware
 * tests drive the clock from the virtual scheduler and the credential fetch
 * through osal's HTTP client. Crypto is the component's own ``port/esp/crypto.c``,
 * reused verbatim; JSON reads through osal's jsmn-backed ``json_parser``.
 *
 * Used on ESP-IDF as well as POSIX: passing this to
 * ``esp_rmaker_aws_sign_port_install`` also keeps the component's built-in
 * ESP-IDF port, with its esp_http_client / esp-tls / esp_log dependencies, out
 * of the link.
 */

#ifndef __ESP_RMAKER_AWS_SIGN_PORT_OSAL_H__
#define __ESP_RMAKER_AWS_SIGN_PORT_OSAL_H__

#include "esp_rmaker_aws_sign.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The osal-backed port configuration.
 *
 * @note Needs an osal built with its HTTP client (``OSAL_INCLUDE_HTTP``); the
 *       port fails to compile otherwise.
 *
 * @return Pointer to a static config, valid for the lifetime of the program.
 * @return NULL when the osal HTTP setup fails. A later call tries again.
 */
const esp_rmaker_aws_sign_port_config_t *esp_rmaker_aws_sign_port_osal_get(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_AWS_SIGN_PORT_OSAL_H__ */
