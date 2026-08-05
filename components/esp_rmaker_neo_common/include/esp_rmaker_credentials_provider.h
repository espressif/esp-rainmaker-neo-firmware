/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_credentials_provider.h
 * @brief Credentials provider interface.
 */

#ifndef __ESP_RMAKER_CREDENTIALS_PROVIDER_H__
#define __ESP_RMAKER_CREDENTIALS_PROVIDER_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stdint.h>
#include <stddef.h>

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Types **********************************************************************/

/**
 * @brief Credentials.
 */
typedef struct {
    uint8_t *credential;
    size_t len;
} esp_rmaker_credential_t;

/**
 * @brief Provider to get a credential.
 *
 * Credential contents should be dynamically allocated and freed later on with esp_rmaker_credentials_free_credential().
 *
 * @param[out] p_credential The credentials.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
typedef esp_rmaker_error_t (*esp_rmaker_credentials_provider_credential_t)(esp_rmaker_credential_t *p_credential);

/**
 * @brief Provider to get a string.
 *
 * String should be dynamically allocated and freed later on with free().
 *
 * @param[out] p_str The string.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
typedef esp_rmaker_error_t (*esp_rmaker_credentials_provider_string_t)(char **p_str);

/** Set of credential providers, one per credential the SDK needs */
typedef struct {
    /** MQTT host */
    esp_rmaker_credentials_provider_string_t mqtt_host;
    /**
     * Client certificate, only the following formats are supported:
     * - PEM
     * - ASN.1 DER
     */
    esp_rmaker_credentials_provider_credential_t client_cert;
    /**
     * Client key, only the following formats are supported:
     * - PEM
     * - ASN.1 DER
     * - NIST P-256 raw 32 bytes big-endian only
     */
    esp_rmaker_credentials_provider_credential_t client_key;
    /** Client ID */
    esp_rmaker_credentials_provider_string_t client_id;
    /** Client username */
    esp_rmaker_credentials_provider_string_t client_username;
    /** Client password */
    esp_rmaker_credentials_provider_string_t client_password;
    /** Random */
    esp_rmaker_credentials_provider_credential_t random;
    /**
     * Codesign certificate, only the following formats are supported:
     * - PEM
     * - ASN.1 DER
     */
    esp_rmaker_credentials_provider_credential_t codesign_cert;
} esp_rmaker_credentials_providers_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Free a credential.
 *
 * @param[in] p_credential The credential.
 */
void esp_rmaker_credentials_free_credential(esp_rmaker_credential_t *p_credential);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_CREDENTIALS_PROVIDER_H__ */
