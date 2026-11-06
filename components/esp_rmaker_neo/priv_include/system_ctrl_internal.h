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
 * @brief Factory reset now, with an extra wipe of SDK-internal state.
 *
 * Runs the same reset as esp_rmaker_system_ctrl_factory_reset_from_participant() with no originator,
 * using the registered network reset function, plus @p extra_wipe. The extra wipe runs after the
 * node_reset notification and the RainMaker Neo data clear, and before the participants, so it can
 * erase state the notification needs and still run before a participant that restarts the node.
 *
 * If no network reset function is registered, the network reset is skipped and reported as an error;
 * every other step still runs. Posts RMAKER_EVENT_FACTORY_RESET when called. If a factory reset is
 * already running or its reboot is scheduled, this runs nothing else, extra wipe included, and returns
 * ESP_RMAKER_INVALID_STATE.
 *
 * @param[in] extra_wipe The extra wipe. Must not be NULL.
 * @param[in] extra_priv Argument passed to @p extra_wipe.
 * @param[in] reset_reboot_s Reboot timeout in seconds; 0 means reboot immediately, a negative value
 *            means do not reboot.
 *
 * @return ESP_RMAKER_OK if every step succeeded, ESP_RMAKER_INVALID_STATE if a factory reset is already
 *         running or its reboot is scheduled (nothing is run), otherwise the first error encountered.
 */
esp_rmaker_error_t esp_rmaker_system_ctrl_factory_reset_with_wipe(esp_rmaker_system_ctrl_factory_reset_wipe_fn_t extra_wipe,
        void *extra_priv, int8_t reset_reboot_s);

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
