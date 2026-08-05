/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file standard_creation.h
 * @brief Standard creation service for RainMaker Neo.
 */

#ifndef __SERVICES_STANDARD_CREATION_H__
#define __SERVICES_STANDARD_CREATION_H__

/* Includes *******************************************************/

/* Error includes. */
#include "esp_rmaker_error_types.h"

/* Standard services includes. */
#include "esp_rmaker_standard_services.h"

/**
 * @brief Local control session security versions.
 */
typedef enum {
    ESP_RMAKER_LOCAL_CTRL_SEC1 = 1,
    ESP_RMAKER_LOCAL_CTRL_SEC2 = 2,
} esp_rmaker_local_ctrl_sec_t;

/* Configuration includes. */
#include "sdkconfig.h"

/* Types *******************************************************/

typedef void (*esp_rmaker_timezone_local_change_cb_t)(const char *timezone);

typedef void (*esp_rmaker_timezone_posix_local_change_cb_t)(const char *timezone_posix);

typedef struct {
    esp_rmaker_timezone_local_change_cb_t timezone_local_change_cb;
    esp_rmaker_timezone_posix_local_change_cb_t timezone_posix_local_change_cb;
} esp_rmaker_timezone_service_callbacks_t;

/* Public functions *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add a local control service to the node.
 *
 * @param[in] version The version of the local control service.
 * @param[in] pop The PoP of the local control service. Required for security versions 1 and 2.
 * @param[in] username The SRP6a username. Required for security version 2; ignored otherwise.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_local_ctrl_service_add_to_node(esp_rmaker_local_ctrl_sec_t version, const char *pop, const char *username);

/**
 * @brief Remove a local control service from the node.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_local_ctrl_service_remove_from_node(void);

/**
 * @brief Add a timezone service to the node.
 *
 * @param[in] timezone Timezone string
 * @param[in] timezone_posix POSIX timezone string
 * @param[out] callbacks Timezone service callbacks to be called when the timezone or POSIX timezone is changed locally
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_timezone_service_add_to_node(const char *timezone, const char *timezone_posix, esp_rmaker_timezone_service_callbacks_t *callbacks);

/**
 * @brief Remove a timezone service from the node.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_timezone_service_remove_from_node(void);

/**
 * @brief Add a system service to the node.
 *
 * @param[in] config System service configuration. Must not be NULL and must have at least one flag set.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_system_service_add_to_node(const esp_rmaker_system_serv_config_t *config);

/**
 * @brief Remove the system service from the node.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_system_service_remove_from_node(void);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICES_STANDARD_CREATION_H__ */
