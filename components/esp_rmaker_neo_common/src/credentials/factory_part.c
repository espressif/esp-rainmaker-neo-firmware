/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file factory_part.c
 * @brief Factory partition implementation
 */

/* Includes **********************************************************************/

/* Declarations */
#include "credentials/factory_part.h"

/* Standard includes */
#include <string.h>
#include <stdbool.h>

/* Platform includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* NVS includes */
#include "util/esp_rmaker_nvs.h"

/* Constants **********************************************************************/

/**
 * @brief The tag for logging.
 */
static const char *TAG = "rmng_factory_part";

/* Variables **********************************************************************/

/**
 * @brief NVS handle for the factory partition.
 */
static osal_storage_handle_t esp_rmaker_factory_nvs_handle = NULL;

/* Private function declarations ****************************************************/

/**
 * @brief Initialize the factory partition if not already initialized
 * @return ESP_RMAKER_OK on success, otherwise error code
 */
static esp_rmaker_error_t __init_if_not_initialized(void);

/**
 * @brief Get a PEM adjusted credential
 * @param[in] nvs_key The key of the credential
 * @param[in] p_credential The credential. Must be freed by the caller if success.
 * @return ESP_RMAKER_OK on success, otherwise error code
 */
static esp_rmaker_error_t __get_pem_adjusted_credential(const char *nvs_key, esp_rmaker_credential_t *p_credential);

/**
 * @brief Write a blob to the factory partition, initialising it first if needed.
 * @param[in] nvs_key The key to write.
 * @param[in] data The bytes to write.
 * @param[in] data_len The number of bytes to write.
 * @return ESP_RMAKER_OK on success, otherwise error code
 */
static esp_rmaker_error_t __set_credential(const char *nvs_key, const void *data, size_t data_len);

/* Private function definitions ****************************************************/

static esp_rmaker_error_t __init_if_not_initialized(void)
{
    if (esp_rmaker_factory_nvs_handle != NULL) {
        return ESP_RMAKER_OK;
    }
    return esp_rmaker_factory_part_init();
}

static esp_rmaker_error_t __get_pem_adjusted_credential(const char *nvs_key, esp_rmaker_credential_t *p_credential)
{
    if (!p_credential) {
        return ESP_RMAKER_INVALID_ARG;
    }

    size_t credential_len = 0;
    uint8_t *credential = esp_rmaker_nvs_get_binary_with_handle(esp_rmaker_factory_nvs_handle, nvs_key, &credential_len);
    if (!credential) {
        return ESP_RMAKER_NOT_FOUND;
    }

    /* Check if the credential is a PEM credential */
    static const char *pem_prefix = "-----BEGIN";
    static const char *pem_suffix = "-----END";
    bool is_pem = (strstr((char *)credential, pem_prefix) != NULL) && (strstr((char *)credential, pem_suffix) != NULL);
    if (is_pem) {
        /* Add the NULL terminator character */
        credential_len += 1;
    }

    /* Return the credential */
    p_credential->credential = credential;
    p_credential->len = credential_len;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __set_credential(const char *nvs_key, const void *data, size_t data_len)
{
    /* Validate before initialising: a bad call should not be the reason the partition gets
     * opened. */
    if (!nvs_key || (!data && data_len > 0)) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t err = __init_if_not_initialized();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    return esp_rmaker_nvs_update_binary_with_handle(esp_rmaker_factory_nvs_handle, nvs_key, data, data_len);
}

/* Public function definitions *************************************************/

esp_rmaker_error_t esp_rmaker_factory_part_init(void)
{
    /* Init the factory partition */
    if (osal_storage_init(RMAKER_NVS_FACTORY_PART_NAME) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to init the factory partition");
        return ESP_RMAKER_FAIL;
    }

    /* Load the factory partition handle */
    if (esp_rmaker_factory_nvs_handle == NULL) {
        esp_rmaker_error_t err = esp_rmaker_load_nvs_handle(RMAKER_NVS_FACTORY_PART_NAME, RMAKER_NVS_FACTORY_NAMESPACE, &esp_rmaker_factory_nvs_handle);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to load the factory partition handle");
            return err;
        }
    }

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_factory_part_deinit(void)
{
    osal_err_t nvs_err;

    /* Close the factory partition */
    if (esp_rmaker_factory_nvs_handle != NULL) {
        nvs_err = osal_storage_close(esp_rmaker_factory_nvs_handle);
        if (nvs_err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to close the factory partition");
            return ESP_RMAKER_FAIL;
        }
        esp_rmaker_factory_nvs_handle = NULL;
    }

    /* Deinit the factory partition */
    nvs_err = osal_storage_deinit(RMAKER_NVS_FACTORY_PART_NAME);
    if (nvs_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to deinit the factory partition");
        return ESP_RMAKER_FAIL;
    }

    return ESP_RMAKER_OK;
}

/* --- MQTT credentials ---------------------------------------------- */

esp_rmaker_error_t esp_rmaker_factory_part_get_mqtt_host(char **p_mqtt_host)
{
    esp_rmaker_error_t err = __init_if_not_initialized();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (!p_mqtt_host) {
        return ESP_RMAKER_INVALID_ARG;
    }
    char *mqtt_host = esp_rmaker_nvs_get_string_with_handle(esp_rmaker_factory_nvs_handle, RMAKER_NVS_FACTORY_KEY_MQTT_HOST);
    if (!mqtt_host) {
        return ESP_RMAKER_NOT_FOUND;
    }
    *p_mqtt_host = mqtt_host;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_factory_part_get_client_key(esp_rmaker_credential_t *p_client_key)
{
    esp_rmaker_error_t err = __init_if_not_initialized();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (!p_client_key) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __get_pem_adjusted_credential(RMAKER_NVS_FACTORY_KEY_CLIENT_KEY, p_client_key);
}

esp_rmaker_error_t esp_rmaker_factory_part_get_client_certificate(esp_rmaker_credential_t *p_client_certificate)
{
    esp_rmaker_error_t err = __init_if_not_initialized();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (!p_client_certificate) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __get_pem_adjusted_credential(RMAKER_NVS_FACTORY_KEY_CLIENT_CERT, p_client_certificate);
}

esp_rmaker_error_t esp_rmaker_factory_part_get_client_id(char **p_client_id)
{
    esp_rmaker_error_t err = __init_if_not_initialized();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (!p_client_id) {
        return ESP_RMAKER_INVALID_ARG;
    }
    char *client_id = esp_rmaker_nvs_get_string_with_handle(esp_rmaker_factory_nvs_handle, RMAKER_NVS_FACTORY_KEY_CLIENT_ID);
    if (!client_id) {
        return ESP_RMAKER_NOT_FOUND;
    }
    *p_client_id = client_id;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_factory_part_get_client_username(char **p_client_username)
{
    esp_rmaker_error_t err = __init_if_not_initialized();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (!p_client_username) {
        return ESP_RMAKER_INVALID_ARG;
    }
    char *client_username = esp_rmaker_nvs_get_string_with_handle(esp_rmaker_factory_nvs_handle, RMAKER_NVS_FACTORY_KEY_CLIENT_USERNAME);
    if (!client_username) {
        return ESP_RMAKER_NOT_FOUND;
    }
    *p_client_username = client_username;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_factory_part_get_client_password(char **p_client_password)
{
    esp_rmaker_error_t err = __init_if_not_initialized();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (!p_client_password) {
        return ESP_RMAKER_INVALID_ARG;
    }
    char *client_password = esp_rmaker_nvs_get_string_with_handle(esp_rmaker_factory_nvs_handle, RMAKER_NVS_FACTORY_KEY_CLIENT_PASSWORD);
    if (!client_password) {
        return ESP_RMAKER_NOT_FOUND;
    }
    *p_client_password = client_password;
    return ESP_RMAKER_OK;
}

/* --- Other credentials ----------------------------------------------------- */

esp_rmaker_error_t esp_rmaker_factory_part_get_random(esp_rmaker_credential_t *p_random)
{
    esp_rmaker_error_t err = __init_if_not_initialized();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (!p_random) {
        return ESP_RMAKER_INVALID_ARG;
    }
    size_t random_len = 0;
    uint8_t *random = esp_rmaker_nvs_get_binary_with_handle(esp_rmaker_factory_nvs_handle, RMAKER_NVS_FACTORY_KEY_RANDOM, &random_len);
    if (!random) {
        return ESP_RMAKER_NOT_FOUND;
    }
    p_random->credential = random;
    p_random->len = random_len;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_factory_part_get_codesign_cert(esp_rmaker_credential_t *p_codesign_cert)
{
    esp_rmaker_error_t err = __init_if_not_initialized();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (!p_codesign_cert) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __get_pem_adjusted_credential(RMAKER_NVS_FACTORY_KEY_CODESIGN_CERT, p_codesign_cert);
}

/* --- Writing credentials --------------------------------------------------- */

esp_rmaker_error_t esp_rmaker_factory_part_set_client_key(const uint8_t *client_key, size_t client_key_len)
{
    if (!client_key || client_key_len == 0) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __set_credential(RMAKER_NVS_FACTORY_KEY_CLIENT_KEY, client_key, client_key_len);
}

esp_rmaker_error_t esp_rmaker_factory_part_set_client_certificate(const uint8_t *client_certificate, size_t client_certificate_len)
{
    if (!client_certificate || client_certificate_len == 0) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __set_credential(RMAKER_NVS_FACTORY_KEY_CLIENT_CERT, client_certificate, client_certificate_len);
}

esp_rmaker_error_t esp_rmaker_factory_part_set_client_id(const char *client_id)
{
    if (!client_id || client_id[0] == '\0') {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __set_credential(RMAKER_NVS_FACTORY_KEY_CLIENT_ID, client_id, strlen(client_id));
}

esp_rmaker_error_t esp_rmaker_factory_part_set_mqtt_host(const char *mqtt_host)
{
    if (!mqtt_host || mqtt_host[0] == '\0') {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __set_credential(RMAKER_NVS_FACTORY_KEY_MQTT_HOST, mqtt_host, strlen(mqtt_host));
}

esp_rmaker_error_t esp_rmaker_factory_part_set_random(const uint8_t *random, size_t random_len)
{
    if (!random || random_len == 0) {
        return ESP_RMAKER_INVALID_ARG;
    }
    return __set_credential(RMAKER_NVS_FACTORY_KEY_RANDOM, random, random_len);
}

/* --- Erasing --------------------------------------------------------------- */

esp_rmaker_error_t esp_rmaker_factory_part_erase_claim_data(void)
{
    esp_rmaker_error_t err = __init_if_not_initialized();
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    /* Exactly the keys claiming writes. Everything else in the namespace is pre-flashed and
     * cannot be reproduced on the node, so it is not ours to erase. */
    static const char *const claim_owned_keys[] = {
        RMAKER_NVS_FACTORY_KEY_CLIENT_KEY,
        RMAKER_NVS_FACTORY_KEY_CLIENT_CERT,
        RMAKER_NVS_FACTORY_KEY_CLIENT_ID,
        RMAKER_NVS_FACTORY_KEY_MQTT_HOST,
    };

    esp_rmaker_error_t first_err = ESP_RMAKER_OK;
    for (size_t i = 0; i < sizeof(claim_owned_keys) / sizeof(claim_owned_keys[0]); i++) {
        osal_err_t erase_err = osal_storage_erase(esp_rmaker_factory_nvs_handle, claim_owned_keys[i]);
        /* A key that was never written is already in the desired state. */
        if (erase_err != OSAL_ERR_OK && erase_err != OSAL_ERR_NVS_KEY_NOT_FOUND) {
            OSAL_LOGE(TAG, "Failed to erase '%s': %d", claim_owned_keys[i], (int)erase_err);
            if (first_err == ESP_RMAKER_OK) {
                first_err = ESP_RMAKER_FAIL;
            }
        }
    }

    if (osal_storage_commit(esp_rmaker_factory_nvs_handle) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to commit the claim data erase");
        if (first_err == ESP_RMAKER_OK) {
            first_err = ESP_RMAKER_FAIL;
        }
    }

    if (first_err == ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Erased the claim credentials from the factory partition");
    }
    return first_err;
}
