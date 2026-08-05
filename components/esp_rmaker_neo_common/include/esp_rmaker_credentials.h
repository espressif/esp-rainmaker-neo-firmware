/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_credentials.h
 * @brief Common functions for reading the ESP RainMaker Neo credentials
 */

#ifndef __ESP_RMAKER_CREDENTIALS_H__
#define __ESP_RMAKER_CREDENTIALS_H__

/* Includes **********************************************************************/

/* Credentials provider includes */
#include "esp_rmaker_credentials_provider.h"

/* MQTT includes */
#include "osal_mqtt_prototypes.h"

/* Standard includes */
#include <stddef.h>

/* Public functions **************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Override some/all of the credentials providers.
 *
 * By default, the credentials are read from the factory partition.
 * If provider overrides are provided, the credentials will be read from the overrides.
 *
 * @param[in] p_provider_overrides The provider overrides.
 * You can partially override by setting the only the required provider overrides to non-NULL values.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_credentials_override(const esp_rmaker_credentials_providers_t *p_provider_overrides);

/**
 * @brief Reset the credentials to the default providers.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_credentials_reset_to_default(void);

/**
 * @brief Get the MQTT connection parameters.
 *
 * @param[out] p_mqtt_conn_params The MQTT connection parameters.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_credentials_get_mqtt_conn_params(osal_mqtt_conn_params_t **p_mqtt_conn_params);

/**
 * @brief Free the MQTT connection parameters.
 *
 * @param[in] p_mqtt_conn_params The MQTT connection parameters.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_credentials_free_mqtt_conn_params(osal_mqtt_conn_params_t *p_mqtt_conn_params);

/**
 * @brief Get the private key.
 *
 * @param[out] p_private_key The private key.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_credentials_get_private_key(esp_rmaker_credential_t *p_private_key);

/**
 * @brief Get the thing name.
 *
 * @param[out] p_thing_name The thing name.
 * Must be freed by the caller if not NULL.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_credentials_get_thing_name(char **p_thing_name);

/**
 * @brief Get the codesign certificate.
 *
 * @param[out] p_codesign_cert The codesign certificate.
 * Must be freed by the caller if not NULL.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_credentials_get_codesign_cert(esp_rmaker_credential_t *p_codesign_cert);

/**
 * @brief Get the MQTT host.
 *
 * @param[out] p_mqtt_host The MQTT host. Must be freed by the caller on success.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_NOT_FOUND if no MQTT host is stored.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_credentials_get_mqtt_host(char **p_mqtt_host);

/**
 * @brief Get the client certificate.
 *
 * Mainly useful as an "is this node provisioned with cloud credentials" check: claiming
 * uses the presence of a certificate to decide whether it has anything to do.
 *
 * @param[out] p_client_cert The certificate. Must be freed by the caller on success with
 * ::esp_rmaker_credentials_free_credential.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_NOT_FOUND if no certificate is stored.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_credentials_get_client_cert(esp_rmaker_credential_t *p_client_cert);

/**
 * @brief Get the random value as a hex string.
 *
 * @param[out] p_random The random value.
 * Must be freed by the caller if not NULL.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_credentials_get_random(esp_rmaker_credential_t *p_random);

/**
 * @name Storing credentials
 *
 * Used by claiming, which obtains credentials at runtime rather than relying on them
 * having been pre-flashed by the host-side factory tooling.
 *
 * The values are stored byte-for-byte as given, matching what the factory tooling writes,
 * so the corresponding getters read them back unchanged.
 *
 * @note Unlike the getters above, these always write to the **factory partition**, and
 *       are unaffected by ::esp_rmaker_credentials_override. Credentials have to be
 *       persisted somewhere concrete to survive a reboot, and the factory partition is
 *       where the default providers read them back from. A deployment that overrides the
 *       providers to read from elsewhere is responsible for its own persistence and
 *       should not use claiming.
 * @{
 */

/**
 * @brief Store the client private key.
 *
 * @param[in] client_key The key, typically PEM.
 * @param[in] client_key_len Length in bytes, excluding any NUL terminator.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_credentials_set_client_key(const uint8_t *client_key, size_t client_key_len);

/**
 * @brief Store the client certificate.
 *
 * @param[in] client_cert The certificate, typically PEM.
 * @param[in] client_cert_len Length in bytes, excluding any NUL terminator.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_credentials_set_client_cert(const uint8_t *client_cert, size_t client_cert_len);

/**
 * @brief Store the client ID (node ID).
 *
 * @param[in] client_id The NUL-terminated client ID.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_credentials_set_client_id(const char *client_id);

/**
 * @brief Store the MQTT host.
 *
 * @param[in] mqtt_host The NUL-terminated MQTT host.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_credentials_set_mqtt_host(const char *mqtt_host);

/**
 * @brief Store the general-purpose random bytes.
 *
 * @param[in] random The random bytes.
 * @param[in] random_len Length of the random bytes.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_credentials_set_random(const uint8_t *random, size_t random_len);

/**
 * @brief Erase the credentials that claiming writes, so the node claims again.
 *
 * Removes the node ID, client certificate, private key and MQTT host, and nothing else. A
 * claimed node claims again from scratch on the next boot; a node whose credentials were
 * pre-flashed will need them re-flashed.
 *
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_credentials_erase_claim_data(void);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_CREDENTIALS_H__ */
