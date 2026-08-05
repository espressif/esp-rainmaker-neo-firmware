/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_signature_verify.h
 * @brief OTA signature verification
 */

#ifndef __OTA_SIGNATURE_VERIFY_H__
#define __OTA_SIGNATURE_VERIFY_H__

/* Includes *********************************************************************/

/* Standard includes */
#include <stddef.h>
#include <stdint.h>

/* Error includes */
#include "esp_rmaker_error_types.h"

/* OTA common includes */
#include "osal_ota.h"

/* Types **********************************************************************/

/**
 * @brief Data buffer type relying on length
 */
typedef struct {
    uint8_t *data;
    size_t len;
} ota_signature_verify_buffer_t;

/**
 * @brief Context for signature verification
 */
typedef struct {
    /**
     * @brief The signature to verify.
     */
    ota_signature_verify_buffer_t signature;
    /**
     * @brief The hash of the data to verify.
     */
    ota_signature_verify_buffer_t hash;
} ota_signature_verify_context_t;

/* Public function declarations *************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Verify the given context, i.e., the signature matches the hash using the given algorithm.
 * @param[in] ctx The context for the signature verification.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t rmaker_ota_signature_verify_context(const ota_signature_verify_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_SIGNATURE_VERIFY_H__ */
