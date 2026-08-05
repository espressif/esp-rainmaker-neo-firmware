/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file impl.h
 * @brief Challenge-response internal interface.
 */


#ifndef __ESP_RMAKER_CHAL_RESP_H__
#define __ESP_RMAKER_CHAL_RESP_H__

#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>

#include "esp_rmaker_error_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Challenge-response endpoint name. Frozen wire value; also the `cap` token. */
#define RMAKER_CHAL_RESP_ENDPOINT_NAME "ch_resp"

/** Initialize Challenge Response
 *
 * This initializes the challenge response module.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_chal_resp_init(void);

/** Deinitialize Challenge Response
 *
 * This deinitializes the challenge response module.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_chal_resp_deinit(void);

/** Disable Challenge Response
 *
 * This disables the challenge response module.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_chal_resp_disable(void);

/** Enable Challenge Response
 *
 * This enables the challenge response module.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_chal_resp_enable(void);

/** Check if Challenge Response is disabled
 *
 * This checks if the challenge response module is disabled.
 *
 * @return true if disabled, false otherwise
 */
bool esp_rmaker_chal_resp_is_disabled(void);

/** Handle Challenge Response
 *
 * This handles the challenge response.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_chal_resp_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf, ssize_t *outlen, void *priv_data);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_CHAL_RESP_H__ */
