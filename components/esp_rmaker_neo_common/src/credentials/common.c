/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file common.c
 * @brief Common credentials implementation.
 */

/* Includes **********************************************************************/

/* Declarations */
#include "esp_rmaker_credentials.h"

/* Factory part includes */
#include "credentials/factory_part.h"

/* Platform includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* mbedTLS */
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

/* Util */
#include "util/esp_rmaker_crypto.h"

/* Configuration includes */
#include "sdkconfig.h"

/* Preprocessor definitions *******************************************************/

#define __CREDENTIALS_PROVIDER_DEFAULT() (esp_rmaker_credentials_providers_t) { \
    .mqtt_host = esp_rmaker_factory_part_get_mqtt_host, \
    .client_cert = esp_rmaker_factory_part_get_client_certificate, \
    .client_key = esp_rmaker_factory_part_get_client_key, \
    .client_id = esp_rmaker_factory_part_get_client_id, \
    .client_username = esp_rmaker_factory_part_get_client_username, \
    .client_password = esp_rmaker_factory_part_get_client_password, \
    .random = esp_rmaker_factory_part_get_random, \
    .codesign_cert = esp_rmaker_factory_part_get_codesign_cert, \
}

/* Constants **********************************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "rmng_credentials";

#if CONFIG_ESP_RMAKER_MQTT_PORT_443
/**
 * @brief ALPN protocols for port 443.
 */
static const char *__alpn_protocols[] = { "x-amzn-mqtt-ca", NULL };
#endif /* CONFIG_ESP_RMAKER_MQTT_PORT_443 */

/* Variables **********************************************************************/

/**
 * @brief Credentials providers in use.
 * Defaults to factory partition reading.
 * If provider overrides are provided, the credentials will be read from the overrides.
 */
static esp_rmaker_credentials_providers_t __credentials_providers = __CREDENTIALS_PROVIDER_DEFAULT();

/* Private function declarations ****************************************************/

/**
 * @brief Get a valid certificate from a provider
 * @param[in] provider The provider to get the certificate
 * @param[out] p_cert The certificate. Must be freed by the caller if success.
 * @return ESP_RMAKER_OK on success, otherwise error code
 */
static esp_rmaker_error_t __get_valid_cert(esp_rmaker_credentials_provider_credential_t provider, esp_rmaker_credential_t *p_cert);

/**
 * @brief Get a valid key from a provider
 * @param[in] provider The provider to get the key
 * @param[out] p_key The key. Must be freed by the caller if success.
 * @return ESP_RMAKER_OK on success, otherwise error code
 */
static esp_rmaker_error_t __get_valid_key(esp_rmaker_credentials_provider_credential_t provider, esp_rmaker_credential_t *p_key);

/* Private function definitions ****************************************************/

#if CONFIG_RMAKER_MQTT_SKIP_CREDENTIALS_CHECK
static esp_rmaker_error_t __get_valid_cert(esp_rmaker_credentials_provider_credential_t provider, esp_rmaker_credential_t *p_cert)
{
    if (!provider || !p_cert) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return provider(p_cert);
}

static esp_rmaker_error_t __get_valid_key(esp_rmaker_credentials_provider_credential_t provider, esp_rmaker_credential_t *p_key)
{
    if (!provider || !p_key) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return provider(p_key);
}
#else
static esp_rmaker_error_t __get_valid_cert(esp_rmaker_credentials_provider_credential_t provider, esp_rmaker_credential_t *p_cert)
{
    if (!provider || !p_cert) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_credential_t cert;
    esp_rmaker_error_t err = provider(&cert);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    /* Try parsing the certificate */
    if (psa_crypto_init() != PSA_SUCCESS) {
        OSAL_LOGE(TAG, "Failed to initialize PSA crypto");
        return ESP_RMAKER_INVALID_STATE;
    }
    mbedtls_x509_crt cert_crt;
    mbedtls_x509_crt_init(&cert_crt);
    int ret = mbedtls_x509_crt_parse(&cert_crt,
                                     cert.credential,
                                     cert.len);
    mbedtls_x509_crt_free(&cert_crt);
    if (ret != 0) {
        OSAL_LOGE(TAG, "Certificate parse failed: -0x%04X", -ret);
        esp_rmaker_credentials_free_credential(&cert);
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Return the certificate */
    p_cert->credential = cert.credential;
    p_cert->len = cert.len;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __get_valid_key(esp_rmaker_credentials_provider_credential_t provider, esp_rmaker_credential_t *p_key)
{
    if (!provider || !p_key) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_credential_t key;
    esp_rmaker_error_t err = provider(&key);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    /* Check the key */
    if (esp_rmaker_crypto_is_key_supported_tls(key.credential, key.len)) {
        /* Return the key */
        p_key->credential = key.credential;
        p_key->len = key.len;
        return ESP_RMAKER_OK;
    }

    if (key.len != 32) {
        OSAL_LOGE(TAG, "Invalid key type! Only NIST P-256 keys are supported, got length: %d", (int)key.len);
        esp_rmaker_credentials_free_credential(&key);
        return ESP_RMAKER_INVALID_STATE;
    }

    OSAL_LOGW(TAG, "Suspected P-256 key, converting to DER format");

    /* Convert the key to DER */
    uint8_t *der = NULL;
    size_t der_len = 0;
    err = esp_rmaker_crypto_esp_key_bin_to_der(key.credential, key.len, &der, &der_len);
    esp_rmaker_credentials_free_credential(&key);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to convert key to DER");
        return err;
    }

    /* Return the key */
    p_key->credential = der;
    p_key->len = der_len;
    return ESP_RMAKER_OK;
}
#endif /* CONFIG_RMAKER_MQTT_SKIP_CREDENTIALS_CHECK */

/* Public function definitions ****************************************************/

esp_rmaker_error_t esp_rmaker_credentials_override(const esp_rmaker_credentials_providers_t *p_provider_overrides)
{
    if (!p_provider_overrides) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (p_provider_overrides->mqtt_host) {
        OSAL_LOGD(TAG, "Overriding MQTT host provider");
        __credentials_providers.mqtt_host = p_provider_overrides->mqtt_host;
    }
    if (p_provider_overrides->client_cert) {
        OSAL_LOGD(TAG, "Overriding client certificate provider");
        __credentials_providers.client_cert = p_provider_overrides->client_cert;
    }
    if (p_provider_overrides->client_key) {
        OSAL_LOGD(TAG, "Overriding client key provider");
        __credentials_providers.client_key = p_provider_overrides->client_key;
    }
    if (p_provider_overrides->client_id) {
        OSAL_LOGD(TAG, "Overriding client ID provider");
        __credentials_providers.client_id = p_provider_overrides->client_id;
    }
    if (p_provider_overrides->client_username) {
        OSAL_LOGD(TAG, "Overriding client username provider");
        __credentials_providers.client_username = p_provider_overrides->client_username;
    }
    if (p_provider_overrides->client_password) {
        OSAL_LOGD(TAG, "Overriding client password provider");
        __credentials_providers.client_password = p_provider_overrides->client_password;
    }
    if (p_provider_overrides->random) {
        OSAL_LOGD(TAG, "Overriding random provider");
        __credentials_providers.random = p_provider_overrides->random;
    }
    if (p_provider_overrides->codesign_cert) {
        OSAL_LOGD(TAG, "Overriding codesign certificate provider");
        __credentials_providers.codesign_cert = p_provider_overrides->codesign_cert;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_credentials_reset_to_default(void)
{
    __credentials_providers = __CREDENTIALS_PROVIDER_DEFAULT();
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_credentials_get_mqtt_conn_params(osal_mqtt_conn_params_t **p_mqtt_conn_params)
{
    if (!p_mqtt_conn_params) {
        return ESP_RMAKER_INVALID_ARG;
    }
    *p_mqtt_conn_params = NULL;
    osal_mqtt_conn_params_t *conn_params = (osal_mqtt_conn_params_t *)OSAL_CALLOC_EXTRAM(1, sizeof(osal_mqtt_conn_params_t));
    if (!conn_params) {
        return ESP_RMAKER_NO_MEM;
    }
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    err = __credentials_providers.mqtt_host(&conn_params->hostname);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get MQTT host: %d", err);
        goto esp_rmaker_credentials_get_mqtt_conn_params_fail;
    }
#if CONFIG_ESP_RMAKER_MQTT_PORT_443
    conn_params->port = 443;
    conn_params->alpn_protocols = __alpn_protocols;
#elif CONFIG_ESP_RMAKER_MQTT_PORT_8883
    conn_params->port = 8883;
    conn_params->alpn_protocols = NULL;
#else
#error "Invalid MQTT port configuration"
#endif /* CONFIG_ESP_RMAKER_MQTT_PORT_443 */
    err = __credentials_providers.client_id(&conn_params->client_id);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get client ID: %d", err);
        goto esp_rmaker_credentials_get_mqtt_conn_params_fail;
    }
    esp_rmaker_credential_t temp_credential;
    err = __get_valid_cert(__credentials_providers.client_cert, &temp_credential);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get client certificate: %d", err);
        goto esp_rmaker_credentials_get_mqtt_conn_params_fail;
    }
    conn_params->client_cert = (char *)temp_credential.credential;
    conn_params->client_cert_len = temp_credential.len;
    err = __get_valid_key(__credentials_providers.client_key, &temp_credential);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get client key: %d", err);
        goto esp_rmaker_credentials_get_mqtt_conn_params_fail;
    }
    conn_params->client_key = (char *)temp_credential.credential;
    conn_params->client_key_len = temp_credential.len;

    /* Optional credentials */
    err = __credentials_providers.client_username(&conn_params->username);
    if (err != ESP_RMAKER_OK) {
        conn_params->username = NULL;
    }
    err = __credentials_providers.client_password(&conn_params->password);
    if (err != ESP_RMAKER_OK) {
        conn_params->password = NULL;
    }

    /* Set the clean session flag */
#if CONFIG_ESP_RMAKER_MQTT_USE_PERSISTENT_SESSION
    conn_params->clean_session = false;
#else
    conn_params->clean_session = true;
#endif /* CONFIG_ESP_RMAKER_MQTT_USE_PERSISTENT_SESSION */

    /* Set the LWT settings */
    conn_params->lwt.enabled = false;

    *p_mqtt_conn_params = conn_params;
    return ESP_RMAKER_OK;

esp_rmaker_credentials_get_mqtt_conn_params_fail:
    esp_rmaker_credentials_free_mqtt_conn_params(conn_params);
    return err;
}

esp_rmaker_error_t esp_rmaker_credentials_free_mqtt_conn_params(osal_mqtt_conn_params_t *p_mqtt_conn_params)
{
    if (!p_mqtt_conn_params) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (p_mqtt_conn_params->hostname) {
        free(p_mqtt_conn_params->hostname);
        p_mqtt_conn_params->hostname = NULL;
    }
    if (p_mqtt_conn_params->client_id) {
        free(p_mqtt_conn_params->client_id);
        p_mqtt_conn_params->client_id = NULL;
    }
    if (p_mqtt_conn_params->client_cert) {
        free(p_mqtt_conn_params->client_cert);
        p_mqtt_conn_params->client_cert = NULL;
        p_mqtt_conn_params->client_cert_len = 0;
    }
    if (p_mqtt_conn_params->client_key) {
        free(p_mqtt_conn_params->client_key);
        p_mqtt_conn_params->client_key = NULL;
        p_mqtt_conn_params->client_key_len = 0;
    }
    if (p_mqtt_conn_params->username) {
        free(p_mqtt_conn_params->username);
        p_mqtt_conn_params->username = NULL;
    }
    if (p_mqtt_conn_params->password) {
        free(p_mqtt_conn_params->password);
        p_mqtt_conn_params->password = NULL;
    }
    free(p_mqtt_conn_params);
    p_mqtt_conn_params = NULL;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_credentials_get_private_key(esp_rmaker_credential_t *p_private_key)
{
    if (!p_private_key) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __credentials_providers.client_key(p_private_key);
}

esp_rmaker_error_t esp_rmaker_credentials_get_thing_name(char **p_thing_name)
{
    if (!p_thing_name) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __credentials_providers.client_id(p_thing_name);
}

esp_rmaker_error_t esp_rmaker_credentials_get_mqtt_host(char **p_mqtt_host)
{
    if (!p_mqtt_host) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __credentials_providers.mqtt_host(p_mqtt_host);
}

esp_rmaker_error_t esp_rmaker_credentials_get_client_cert(esp_rmaker_credential_t *p_client_cert)
{
    if (!p_client_cert) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __credentials_providers.client_cert(p_client_cert);
}

esp_rmaker_error_t esp_rmaker_credentials_get_codesign_cert(esp_rmaker_credential_t *p_codesign_cert)
{
    if (!p_codesign_cert) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __get_valid_cert(__credentials_providers.codesign_cert, p_codesign_cert);
}

esp_rmaker_error_t esp_rmaker_credentials_get_random(esp_rmaker_credential_t *p_random)
{
    if (!p_random) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __credentials_providers.random(p_random);
}

/* --- Storing credentials --------------------------------------------------- */

/* These deliberately bypass __credentials_providers: the providers are read-only, and
 * claimed credentials must land in the factory partition to survive a reboot. */

esp_rmaker_error_t esp_rmaker_credentials_set_client_key(const uint8_t *client_key, size_t client_key_len)
{
    return esp_rmaker_factory_part_set_client_key(client_key, client_key_len);
}

esp_rmaker_error_t esp_rmaker_credentials_set_client_cert(const uint8_t *client_cert, size_t client_cert_len)
{
    return esp_rmaker_factory_part_set_client_certificate(client_cert, client_cert_len);
}

esp_rmaker_error_t esp_rmaker_credentials_set_client_id(const char *client_id)
{
    return esp_rmaker_factory_part_set_client_id(client_id);
}

esp_rmaker_error_t esp_rmaker_credentials_set_mqtt_host(const char *mqtt_host)
{
    return esp_rmaker_factory_part_set_mqtt_host(mqtt_host);
}

esp_rmaker_error_t esp_rmaker_credentials_set_random(const uint8_t *random, size_t random_len)
{
    return esp_rmaker_factory_part_set_random(random, random_len);
}

esp_rmaker_error_t esp_rmaker_credentials_erase_claim_data(void)
{
    return esp_rmaker_factory_part_erase_claim_data();
}
