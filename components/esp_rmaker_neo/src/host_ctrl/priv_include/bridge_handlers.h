/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bridge_handlers.h
 * @brief Host-control bridge sub-command dispatch.
 */

#ifndef __RMNG_HOST_CTRL_BRIDGE_HANDLERS_H__
#define __RMNG_HOST_CTRL_BRIDGE_HANDLERS_H__

#include "sdkconfig.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Dispatch a BRIDGE-prefixed host control command.
 *
 * Called from the top-level command switch in handler.c. The caller has
 * already consumed the leading command char; @p sub_char is the
 * second byte and @p payload points just past it (length includes the
 * trailing delimiter as usual).
 *
 * Sends a response on its own via ::esp_rmaker_host_ctrl_send_response or
 * ::esp_rmaker_host_ctrl_send_response_with_payload.
 */
void esp_rmaker_host_ctrl_bridge_handle(char sub_char, uint8_t *payload, size_t payload_length);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

#endif /* __RMNG_HOST_CTRL_BRIDGE_HANDLERS_H__ */
