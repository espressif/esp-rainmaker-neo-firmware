/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_system_ctrl.h
 * @brief System control functions.
 */

#ifndef __ESP_RMAKER_SYSTEM_CTRL_H__
#define __ESP_RMAKER_SYSTEM_CTRL_H__

/* Includes **************************************************************/

/* Standard includes */
#include <stdint.h>

/* Error types */
#include "esp_rmaker_error_types.h"

/* Types **************************************************************/

/**
 * @brief Function to reset the network credentials.
 */
typedef esp_rmaker_error_t (* esp_rmaker_system_ctrl_network_reset_fn_t)(void);

/* Function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reboot the system after a given timeout.
 *
 * @param[in] timeout_s The timeout in seconds. 0 reboots immediately, from the calling context.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_system_ctrl_reboot(uint8_t timeout_s);

/**
 * @brief Register a default network-credential reset function.
 *
 * Once registered, esp_rmaker_system_ctrl_network_reset() and esp_rmaker_system_ctrl_factory_reset()
 * may be called with a NULL network_reset_fn to fall back to this registered function. This lets
 * generic callers (e.g. the serial console's reset-network command) trigger a network reset without
 * knowing the application-specific reset routine.
 *
 * @param[in] network_reset_fn Function to reset the network credentials. NULL clears the registration.
 * @return ESP_RMAKER_OK on success.
 */
esp_rmaker_error_t esp_rmaker_system_ctrl_register_network_reset_fn(esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn);

/**
 * @brief Reset only the RainMaker Neo data namespaces after a given timeout.
 *
 * Clears the NVS namespaces owned by RainMaker Neo without erasing the entire NVS partition or
 * touching the network credentials.
 *
 * @param[in] reset_s The timeout in seconds to perform the data reset. 0 means perform it
 *                    synchronously, with no timeout.
 * @param[in] reset_reboot_s The timeout in seconds to reboot the system after the data reset.
 *                           0 means reboot immediately; a negative value means do not reboot.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_system_ctrl_data_reset(uint8_t reset_s, int8_t reset_reboot_s);

/**
 * @brief Reset the network credentials after a given timeout, using the provided function.
 *
 * The SDK has no notion of the underlying network (Wi-Fi, Thread, ...): clearing the credentials is
 * entirely up to `network_reset_fn`.
 *
 * @param[in] reset_s The timeout in seconds to reset the network credentials. 0 means reset
 *                    synchronously, with no timeout.
 * @param[in] reset_reboot_s The timeout in seconds to reboot the system after resetting the network
 *                           credentials. 0 means reboot immediately; a negative value means do not
 *                           reboot.
 * @param[in] network_reset_fn Function to reset the network credentials. NULL means use the function registered via
 *            esp_rmaker_system_ctrl_register_network_reset_fn().
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG if network_reset_fn is NULL and no reset function has been
 *         registered, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_system_ctrl_network_reset(uint8_t reset_s, int8_t reset_reboot_s, esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn);

/**
 * @brief Factory reset the system after a given timeout.
 *
 * This does both of the following:
 *
 * - Clears the NVS namespaces owned by RainMaker Neo, in the same way as
 *   esp_rmaker_system_ctrl_data_reset() does.
 * - Resets the network credentials using the provided function.
 *
 * @param[in] reset_s The timeout in seconds to factory reset the system. 0 means reset
 *                    synchronously, with no timeout.
 * @param[in] reset_reboot_s The timeout in seconds to reboot the system after factory reset.
 *                           0 means reboot immediately; a negative value means do not reboot.
 * @param[in] network_reset_fn Function to reset the network credentials. NULL means use the function registered via
 *            esp_rmaker_system_ctrl_register_network_reset_fn().
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG if network_reset_fn is NULL and no reset function has been
 *         registered, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_system_ctrl_factory_reset(uint8_t reset_s, int8_t reset_reboot_s, esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_SYSTEM_CTRL_H__ */
