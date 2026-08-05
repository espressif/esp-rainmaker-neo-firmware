/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file system_ctrl_internal.h
 * @brief Internal system control helpers shared within the RainMaker Neo SDK.
 */

#ifndef __SYSTEM_CTRL_INTERNAL_H__
#define __SYSTEM_CTRL_INTERNAL_H__

#include "esp_rmaker_error_types.h"
#include "esp_rmaker_system_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Perform the factory-reset wipe immediately, without rebooting.
 *
 * Notifies the cloud that the node is resetting itself (best-effort, bounded wait), resets the
 * network credentials and clears the NVS namespaces owned by RainMaker Neo - everything
 * esp_rmaker_system_ctrl_factory_reset() does except the reboot, which is left to the caller.
 *
 * Shared by the public factory reset and by the clear-claim-data console command: erasing the claim
 * credentials invalidates the node's cloud identity, so all the data keyed to it must go too.
 *
 * Best-effort: every step runs even if an earlier one failed, and the first error is returned.
 *
 * @param[in] network_reset_fn Function to reset the network credentials. NULL means use the function
 *            registered via esp_rmaker_system_ctrl_register_network_reset_fn(); if nothing is
 *            registered either, the network reset is skipped (logged) and the data is still cleared.
 *
 * @return ESP_RMAKER_OK if everything succeeded, otherwise the first error encountered.
 */
esp_rmaker_error_t esp_rmaker_system_ctrl_factory_reset_no_reboot(esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn);

/**
 * @brief Clear the RainMaker Neo-owned data NVS namespaces.
 *
 * Erases only the RainMaker Neo data namespaces without touching the rest of the NVS partition (e.g. network
 * credentials) and without rebooting. Shared by the public data reset (esp_rmaker_system_ctrl_data_reset),
 * public factory reset (esp_rmaker_system_ctrl_factory_reset) and the remote-control reset flow.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_system_ctrl_clear_data_namespaces(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYSTEM_CTRL_INTERNAL_H__ */
