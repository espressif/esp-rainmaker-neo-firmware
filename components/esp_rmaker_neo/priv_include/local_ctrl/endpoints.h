/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file local_ctrl/endpoints.h
 * @brief Local control endpoint protocol handlers (get_params / set_params / get_config)
 *
 * Transport-agnostic protocomm endpoint handlers implementing the local
 * control endpoint protocol (see docs/en/specs/local_ctrl_endpoint_protocol.md):
 * - get_params / get_config: protobuf-framed (local_ctrl.proto), client-pull
 *   offset-based fragmentation (~200B fragments).
 * - set_params: raw JSON request, JSON status response.
 */

#ifndef __LOCAL_CTRL_ENDPOINTS_H__
#define __LOCAL_CTRL_ENDPOINTS_H__

#include <stdint.h>
#include <sys/types.h>

#include "esp_rmaker_error_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Endpoint names (wire contract - treat as frozen). */
#define RMAKER_LOCAL_CTRL_GET_PARAMS_ENDPOINT "get_params"
#define RMAKER_LOCAL_CTRL_SET_PARAMS_ENDPOINT "set_params"
#define RMAKER_LOCAL_CTRL_GET_CONFIG_ENDPOINT "get_config"

/**
 * @brief get_params endpoint handler (protobuf request/response, fragmented).
 */
esp_rmaker_error_t esp_rmaker_local_ctrl_get_params_ep_handler(
    uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
    uint8_t **outbuf, ssize_t *outlen, void *priv_data);

/**
 * @brief get_config endpoint handler (protobuf request/response, fragmented).
 */
esp_rmaker_error_t esp_rmaker_local_ctrl_get_config_ep_handler(
    uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
    uint8_t **outbuf, ssize_t *outlen, void *priv_data);

/**
 * @brief set_params endpoint handler (raw JSON request, JSON status response).
 *
 * Always returns ESP_RMAKER_OK with an error JSON payload on failure, so that
 * session-oriented transports (e.g. BLE) do not drop the connection.
 */
esp_rmaker_error_t esp_rmaker_local_ctrl_set_params_ep_handler(
    uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
    uint8_t **outbuf, ssize_t *outlen, void *priv_data);

/**
 * @brief Free the cached (fragmented) get-data payload, if any.
 *
 * Call on service stop/teardown; safe to call when nothing is cached.
 */
void esp_rmaker_local_ctrl_endpoints_free_data(void);

#ifdef __cplusplus
}
#endif

#endif /* __LOCAL_CTRL_ENDPOINTS_H__ */
