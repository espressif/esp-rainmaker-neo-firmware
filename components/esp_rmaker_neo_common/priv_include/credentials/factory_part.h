/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file factory_part.h
 * @brief Common functions for reading the ESP RainMaker Neo factory partition.
 */

#ifndef __ESP_RMAKER_CREDENTIALS_FACTORY_PART_H__
#define __ESP_RMAKER_CREDENTIALS_FACTORY_PART_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stdint.h>
#include <stddef.h>

/* Factory part includes */
#include "esp_rmaker_factory_part.h"

/* NVS includes */
#include "util/esp_rmaker_nvs.h"

/* Error types includes */
#include "esp_rmaker_error_types.h"

/* Credentials includes */
#include "esp_rmaker_credentials.h"

/* Configuration includes */
#include "sdkconfig.h"

/* Factory partition constants ******************************************************/

#define RMAKER_NVS_FACTORY_PART_NAME                     CONFIG_ESP_RMAKER_FACTORY_PARTITION_NAME
#define RMAKER_NVS_FACTORY_NAMESPACE                     CONFIG_ESP_RMAKER_FACTORY_NAMESPACE
#define RMAKER_NVS_FACTORY_KEY_CLIENT_KEY                "client_key"
#define RMAKER_NVS_FACTORY_KEY_CLIENT_CERT               "client_cert"
#define RMAKER_NVS_FACTORY_KEY_MQTT_HOST                 "mqtt_host"
#define RMAKER_NVS_FACTORY_KEY_CLIENT_ID                 "node_id"
#define RMAKER_NVS_FACTORY_KEY_CLIENT_USERNAME           "client_user"
#define RMAKER_NVS_FACTORY_KEY_CLIENT_PASSWORD           "client_pass"
#define RMAKER_NVS_FACTORY_KEY_RANDOM                    "random"
#define RMAKER_NVS_FACTORY_KEY_CODESIGN_CERT             "codesign_cert"

/* Public functions **************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/* --- Initialize and deinitialize ---------------------------------------------- */

/**
 * @brief Initialize the factory partition.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_init(void);

/**
 * @brief Deinitialize the factory partition.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_deinit(void);

/* --- MQTT credentials ----------------------------------------------------- */

/**
 * Get the MQTT host.
 * @param[out] p_mqtt_host The MQTT host.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_get_mqtt_host(char **p_mqtt_host);

/**
 * Get the client key.
 * @param[out] p_client_key The client key.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_get_client_key(esp_rmaker_credential_t *p_client_key);

/**
 * Get the client certificate.
 * @param[out] p_client_certificate The client certificate.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_get_client_certificate(esp_rmaker_credential_t *p_client_certificate);

/**
 * Get the client ID.
 * @param[out] p_client_id The client ID.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_get_client_id(char **p_client_id);

/**
 * Get the client username.
 * @param[out] p_client_username The client username.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_get_client_username(char **p_client_username);

/**
 * Get the client password.
 * @param[out] p_client_password The client password.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_get_client_password(char **p_client_password);

/* --- Other credentials ----------------------------------------------------- */

/**
 * Get the random.
 * @param[out] p_random The random.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_get_random(esp_rmaker_credential_t *p_random);

/**
 * Get the codesign certificate.
 * @param[out] p_codesign_cert The codesign certificate.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_get_codesign_cert(esp_rmaker_credential_t *p_codesign_cert);

/* --- Writing credentials --------------------------------------------------- */

/* Used by claiming, which obtains credentials at runtime instead of relying on them
 * having been pre-flashed by the host-side factory tooling.
 *
 * All values are stored as NVS blobs holding exactly the bytes given, with no trailing
 * NUL -- byte-identical to what the factory tooling writes, so the getters above (which
 * re-append the NUL for PEM data) read them back unchanged. For the string-valued
 * setters, pass a NUL-terminated string; strlen() bytes are stored. */

/**
 * Set the client key.
 * @param[in] client_key The client key, typically a PEM private key.
 * @param[in] client_key_len The length of the key in bytes, excluding any NUL terminator.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_set_client_key(const uint8_t *client_key, size_t client_key_len);

/**
 * Set the client certificate.
 * @param[in] client_certificate The client certificate, typically PEM.
 * @param[in] client_certificate_len The length in bytes, excluding any NUL terminator.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_set_client_certificate(const uint8_t *client_certificate, size_t client_certificate_len);

/**
 * Set the client ID (node ID).
 * @param[in] client_id The NUL-terminated client ID.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_set_client_id(const char *client_id);

/**
 * Set the MQTT host.
 * @param[in] mqtt_host The NUL-terminated MQTT host.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_set_mqtt_host(const char *mqtt_host);

/**
 * Set the general-purpose random bytes.
 * @param[in] random The random bytes.
 * @param[in] random_len The length of the random bytes.
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_set_random(const uint8_t *random, size_t random_len);

/* --- Erasing --------------------------------------------------------------- */

/**
 * @brief Erase the credentials that claiming writes.
 *
 * Removes ::RMAKER_NVS_FACTORY_KEY_CLIENT_KEY, ::RMAKER_NVS_FACTORY_KEY_CLIENT_CERT,
 * ::RMAKER_NVS_FACTORY_KEY_CLIENT_ID and ::RMAKER_NVS_FACTORY_KEY_MQTT_HOST, and nothing
 * else, so a claimed node claims again from scratch. Missing keys are not an error.
 *
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t esp_rmaker_factory_part_erase_claim_data(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_CREDENTIALS_FACTORY_PART_H__ */
