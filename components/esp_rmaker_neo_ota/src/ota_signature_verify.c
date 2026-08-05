/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_signature_verify.c
 * @brief Signature verification implementation.
 */

/* Includes *********************************************************************/

/* Declarations */
#include "ota_signature_verify.h"

/* Standard includes */
#include <string.h>

/* Platform common includes */
#include "osal_log.h"

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Credentials includes */
#include "esp_rmaker_credentials.h"

/* mbedtls includes */
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"

/* Constants *********************************************************************/

/**
 * @brief The tag for logging.
 */
static const char *TAG = "rmng_ota_sigverify";

/* Public function definitions *************************************************/

esp_rmaker_error_t rmaker_ota_signature_verify_context(const ota_signature_verify_context_t *ctx)
{
    /* Validate context */
    if (ctx == NULL) {
        OSAL_LOGE(TAG, "Invalid argument: ctx is NULL");
        return ESP_RMAKER_INVALID_ARG;
    }
    if (ctx->signature.data == NULL || ctx->signature.len == 0) {
        OSAL_LOGE(TAG, "Invalid argument: signature is NULL or empty");
        return ESP_RMAKER_INVALID_ARG;
    }
    if (ctx->hash.data == NULL || ctx->hash.len == 0) {
        OSAL_LOGE(TAG, "Invalid argument: hash is NULL or empty");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Get the codesign certificate */
    esp_rmaker_credential_t codesign_cert;
    esp_rmaker_error_t ret = esp_rmaker_credentials_get_codesign_cert(&codesign_cert);
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get codesign certificate for signature verification: %d", ret);
        return ret;
    }

    /* Parse the codesign certificate */
    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);
    int mbedtls_ret = mbedtls_x509_crt_parse(&cert, codesign_cert.credential, codesign_cert.len);
    if (mbedtls_ret != 0) {
        OSAL_LOGE(TAG, "Failed to parse codesign certificate: %d", mbedtls_ret);
        ret = ESP_RMAKER_INVALID_STATE;
        goto rmaker_ota_signature_verify_context_end;
    }

    /* Verify signature with certificate */
    mbedtls_ret = mbedtls_pk_verify(&cert.pk, MBEDTLS_MD_SHA256, ctx->hash.data, ctx->hash.len, ctx->signature.data, ctx->signature.len);
    if (mbedtls_ret != 0) {
        OSAL_LOGE(TAG, "Failed to verify signature: %d", mbedtls_ret);
        ret = ESP_RMAKER_INVALID_STATE;
        goto rmaker_ota_signature_verify_context_end;
    }

rmaker_ota_signature_verify_context_end:
    mbedtls_x509_crt_free(&cert);
    esp_rmaker_credentials_free_credential(&codesign_cert);

    return ret;
}
