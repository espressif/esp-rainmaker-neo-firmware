/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_aws_sign_port_osal_priv.h
 * @brief Internal interface between the port's per-concern sources.
 */

#ifndef __ESP_RMAKER_AWS_SIGN_PORT_OSAL_PRIV_H__
#define __ESP_RMAKER_AWS_SIGN_PORT_OSAL_PRIV_H__

#include "esp_rmaker_aws_sign.h"

#include "osal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** jsmn-backed, via osal's json_parser. */
extern const esp_rmaker_aws_sign_json_ops_t esp_rmaker_aws_sign_port_osal_json_ops;

extern const esp_rmaker_aws_sign_http_ops_t esp_rmaker_aws_sign_port_osal_http_ops;

/**
 * @brief Fetch the osal HTTP implementation.
 *
 * @note Call once, from single-threaded init. The ops above fail with
 *       ESP_ERR_INVALID_STATE until it has succeeded.
 *
 * @return ESP_OK on success, an error code otherwise.
 */
esp_err_t esp_rmaker_aws_sign_port_osal_http_setup(void);

/** Map an osal error onto the esp_err_t the port ops return. */
static inline esp_err_t esp_rmaker_aws_sign_port_err(osal_err_t err)
{
    switch (err) {
    case OSAL_ERR_OK:
        return ESP_OK;
    case OSAL_ERR_NO_MEM:
        return ESP_ERR_NO_MEM;
    case OSAL_ERR_INVALID_ARG:
        return ESP_ERR_INVALID_ARG;
    case OSAL_ERR_INVALID_STATE:
        return ESP_ERR_INVALID_STATE;
    case OSAL_ERR_TIMEOUT:
        return ESP_ERR_TIMEOUT;
    case OSAL_ERR_NOT_SUPPORTED:
        return ESP_ERR_NOT_SUPPORTED;
    default:
        return ESP_FAIL;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_AWS_SIGN_PORT_OSAL_PRIV_H__ */
