/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file migration.h
 * @brief MQTT host migration for nodes with a RainMaker classic factory partition.
 */

#ifndef __CREDENTIALS_MIGRATION_H__
#define __CREDENTIALS_MIGRATION_H__

/* Includes *******************************************************/

/* Standard includes */
#include <stdint.h>

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Constants *******************************************************/

/**
 * @brief The node ID comes from the certificate CN.
 */
#define RMAKER_MIGRATION_WRITE_CLIENT_ID (1U << 0)

/**
 * @brief The target host is in use in memory only.
 */
#define RMAKER_MIGRATION_WRITE_MQTT_HOST (1U << 1)

/**
 * @brief All the credentials that the migration can write back.
 */
#define RMAKER_MIGRATION_WRITE_ANY (RMAKER_MIGRATION_WRITE_CLIENT_ID | RMAKER_MIGRATION_WRITE_MQTT_HOST)

/* Type definitions *******************************************************/

/**
 * @brief The credentials that the migration must write back, as RMAKER_MIGRATION_WRITE_* bits.
 */
typedef uint32_t esp_rmaker_migration_state_t;

/* Public functions *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Use the certificate CN if no node ID is stored, and use the target host in memory.
 * @param[in] target_host The MQTT host to move to. Must stay valid for the life of the program.
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if target_host is NULL or empty.
 * @return ESP_RMAKER_NOT_FOUND if the node has no node ID and no certificate CN.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_migration_check(const char *target_host);

/**
 * @brief Write back the migrated credentials after the first MQTT connection and when the image is valid.
 * @note Call after the event loop init. Does nothing if ::esp_rmaker_migration_check found nothing to write back.
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_migration_start(void);

/**
 * @brief Start the target host connection timeout. Call when the network first connects.
 * @param[in] timeout_s The timeout in seconds.
 * @return ESP_RMAKER_OK on success, also if ::esp_rmaker_migration_start did not start.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_migration_on_network_up(uint32_t timeout_s);

/**
 * @brief Stop the write-back that ::esp_rmaker_migration_start started. Keeps the migration state.
 */
void esp_rmaker_migration_stop(void);

/**
 * @brief Get the credentials that the migration must write back.
 * @return The migration state.
 */
esp_rmaker_migration_state_t esp_rmaker_migration_get_state(void);

/**
 * @brief Clear the migration state.
 * @note Does not restore the MQTT host and client ID providers. Call ::esp_rmaker_credentials_reset_to_default for that.
 */
void esp_rmaker_migration_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __CREDENTIALS_MIGRATION_H__ */
