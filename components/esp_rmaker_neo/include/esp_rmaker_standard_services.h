/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_standard_services.h
 * @brief ESP RainMaker standard services
 */

#ifndef __ESP_RMAKER_STANDARD_SERVICES_H__
#define __ESP_RMAKER_STANDARD_SERVICES_H__

/* Includes *******************************************************/

/* Standard includes. */
#include <stdbool.h>
#include <stdint.h>

/* Error includes. */
#include "esp_rmaker_error_types.h"

/* System control includes */
#include "esp_rmaker_system_ctrl.h"

/* Configuration includes */
#include "sdkconfig.h"

/* Pre-processor definitions *******************************************************/

/* Standard service count */
#define RMAKER_STANDARD_SERVICE_COUNT 2

/* Type definitions *******************************************************/

/** Standard service disable function type */
typedef esp_rmaker_error_t (*esp_rmaker_standard_service_disable_func)(void);

/** System service flags. Selects which params the System service exposes. */
typedef enum {
    SYSTEM_SERV_FLAG_REBOOT        = (1 << 0),
    SYSTEM_SERV_FLAG_NETWORK_RESET = (1 << 1),
    SYSTEM_SERV_FLAG_FACTORY_RESET = (1 << 2),
} esp_rmaker_system_serv_flag_t;

/** All System service flags. */
#define SYSTEM_SERV_FLAGS_ALL (SYSTEM_SERV_FLAG_REBOOT | \
        SYSTEM_SERV_FLAG_NETWORK_RESET | SYSTEM_SERV_FLAG_FACTORY_RESET)

/** System service configuration */
typedef struct {
    uint16_t flags;                 /*!< OR of esp_rmaker_system_serv_flag_t; at least one required */
    uint8_t  reboot_seconds;        /*!< Reboot delay (s); passed to esp_rmaker_system_ctrl_reboot */
    uint8_t  reset_seconds;         /*!< Network/factory reset delay (s) */
    int8_t   reset_reboot_seconds;  /*!< Reboot delay (s) after reset; negative means no reboot */
    esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn; /*!< Network credentials reset fn; required (non-NULL) if NETWORK_RESET or FACTORY_RESET flag is set */
} esp_rmaker_system_serv_config_t;


/* Public function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enable Timezone Service
 *
 * This enables the ESP RainMaker standard timezone service which can be used to set
 * timezone, either in POSIX or location string format. Please refer the specifications
 * for additional details.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_timezone_service_enable(void);

/**
 * @brief Disable Timezone Service
 *
 * This disables the ESP RainMaker standard timezone service.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_timezone_service_disable(void);

/**
 * @brief Enable System Service
 *
 * This enables the ESP RainMaker standard System service which exposes Reboot,
 * Network-Reset and/or Factory-Reset params (selected via config->flags). Writing
 * true to a param triggers the corresponding esp_rmaker_system_ctrl_* action.
 *
 * @param[in] config System service configuration. Must not be NULL and must have
 *                    at least one flag set.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_system_service_enable(const esp_rmaker_system_serv_config_t *config);

/**
 * @brief Disable System Service
 *
 * This disables the ESP RainMaker standard System service.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_system_service_disable(void);

/* --- Standard Local Control Service --- */

/**
 * @brief Set custom PoP for Local Control Service.
 *
 * This allows setting a custom Proof of Possession (PoP) for the local control
 * service instead of the internally generated one. This is useful when you want
 * to use the same PoP that was used for provisioning.
 *
 * @note This must be called before esp_rmaker_local_ctrl_service_enable() for it to take effect.
 *       If not called, a random PoP will be generated and stored in NVS.
 *
 * @param[in] pop NULL terminated PoP string (typically 8 characters alphanumeric).
 *                Pass NULL to clear any previously set custom PoP.
 *
 * @return ESP_RMAKER_OK on success
 * @return ESP_RMAKER_INVALID_ARG if pop is empty string
 * @return ESP_RMAKER_NO_MEM if memory allocation fails
 */
esp_rmaker_error_t esp_rmaker_local_ctrl_set_pop(const char *pop);

#if CONFIG_RMNG_HOST_CTRL
/**
 * @brief Set the TCP port for the local control HTTP server before the service is enabled.
 *
 * Intended for POSIX device-sim and automated tests: the host assigns a free port per instance
 * and sends it over the remote protocol before services are added. If not called,
 * CONFIG_ESP_RMAKER_LOCAL_CTRL_HTTP_PORT is used.
 *
 * @note The port is applied when the service starts. If the local endpoints service is
 *       already running, this stops it first so the new port takes effect on the next
 *       enable; the service is not restarted for you.
 *
 * @param[in] port TCP port (1..65535).
 *
 * @return ESP_RMAKER_OK on success
 * @return ESP_RMAKER_INVALID_ARG if port is out of range
 * @return the error from esp_rmaker_chal_resp_service_disable() or
 *         esp_rmaker_local_ctrl_service_disable() if a running service could not be stopped
 */
esp_rmaker_error_t esp_rmaker_local_ctrl_set_http_port_from_host_ctrl(int port);
#endif /* CONFIG_RMNG_HOST_CTRL */

/**
 * @brief Enable Local Control Service
 *
 * This enables the ESP RainMaker standard local control service, which allows users to
 * control their device without internet connection.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_local_ctrl_service_enable(void);

/**
 * @brief Disable Local Control Service
 *
 * This disables the ESP RainMaker standard local control service.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_local_ctrl_service_disable(void);

/**
 * @brief Enable the Challenge-Response endpoint
 *
 * Registers the ch_resp endpoint on the local endpoints service instance
 * (starting the instance if needed) and reflects it in the advertised
 * capabilities. Independent of local control: any combination of the two
 * endpoint sets may be active.
 *
 * @note Refused (ESP_RMAKER_INVALID_STATE) while a client-issued disable is
 *       persisted; that state is cleared only by a factory reset.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_chal_resp_service_enable(void);

/**
 * @brief Disable the Challenge-Response endpoint
 *
 * Removes the ch_resp endpoint; the service instance is stopped when no
 * endpoint set remains active.
 *
 * @return ESP_RMAKER_OK on success
 * @return error on failure
 */
esp_rmaker_error_t esp_rmaker_chal_resp_service_disable(void);

/**
 * @brief Check whether the Challenge-Response endpoint is enabled
 *
 * @return true if the ch_resp endpoint is currently registered, false otherwise.
 */
bool esp_rmaker_chal_resp_service_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_STANDARD_SERVICES_H__ */
