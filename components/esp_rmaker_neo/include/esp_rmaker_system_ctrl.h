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
#include <stdbool.h>
#include <stdint.h>

/* Error types */
#include "esp_rmaker_error_types.h"

/* Types **************************************************************/

/**
 * @brief Function to reset the network credentials.
 */
typedef esp_rmaker_error_t (* esp_rmaker_system_ctrl_network_reset_fn_t)(void);

/**
 * @brief Function to erase a factory reset participant's own persistent state.
 *
 * @param[in] priv Private data supplied at registration.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code. A failure is logged and does not stop the
 *         remaining participants.
 */
typedef esp_rmaker_error_t (* esp_rmaker_system_ctrl_factory_reset_wipe_fn_t)(void *priv);

/**
 * @brief A component holding persistent state outside the RainMaker Neo data model, to be wiped
 *        along with it on factory reset.
 *
 * Registered by components that mirror or reflect the data model onto another protocol and keep
 * their own persistent state - pairings, credentials, access control - which a factory reset must
 * erase too, or the node comes back up factory-new to the cloud while still owned by the other
 * protocol's controllers.
 *
 * Every factory reset runs every participant exactly once, whichever side started it: a reset
 * originating in the external model goes through esp_rmaker_system_ctrl_factory_reset_from_participant(),
 * which skips only the caller's own wipe.
 */
typedef struct {
    /** Name, used in logs. Must stay valid for the lifetime of the registration. */
    const char *name;

    /** true if @c wipe restarts the system instead of returning. Such a participant is wiped last,
     * after every other participant: an earlier restart would strand their wipes. */
    bool reboots_on_wipe;

    /** Erase this participant's persistent state. Must be non-NULL. Must not return if
     * @c reboots_on_wipe is set; a participant that delegates the restart to another task has to
     * bound that wait itself and restart the system when it elapses. */
    esp_rmaker_system_ctrl_factory_reset_wipe_fn_t wipe;

    /** Passed through to @c wipe. */
    void *priv;
} esp_rmaker_system_ctrl_factory_reset_participant_t;

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
 *                    A value above 0 starts the reset worker task on first use, and fails
 *                    with ESP_RMAKER_NO_MEM if it cannot be created.
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
 *                    A value above 0 starts the reset worker task on first use, and fails
 *                    with ESP_RMAKER_NO_MEM if it cannot be created.
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
 * This does all of the following, in order:
 *
 * - Notifies the cloud that the node is resetting itself (best-effort, bounded wait).
 * - Resets the network credentials using the provided function.
 * - Clears the NVS namespaces owned by RainMaker Neo, in the same way as
 *   esp_rmaker_system_ctrl_data_reset() does.
 * - Wipes every registered factory reset participant (see
 *   esp_rmaker_system_ctrl_factory_reset_participant_register()).
 *
 * @param[in] reset_s The timeout in seconds to factory reset the system. 0 means reset
 *                    synchronously, with no timeout.
 *                    A value above 0 starts the reset worker task on first use, and fails
 *                    with ESP_RMAKER_NO_MEM if it cannot be created.
 * @param[in] reset_reboot_s The timeout in seconds to reboot the system after factory reset.
 *                           0 means reboot immediately; a negative value means do not reboot.
 * @param[in] network_reset_fn Function to reset the network credentials. NULL means use the function registered via
 *            esp_rmaker_system_ctrl_register_network_reset_fn().
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG if network_reset_fn is NULL and no reset function has been
 *         registered, otherwise error code. With @p reset_s 0, ESP_RMAKER_INVALID_STATE if a factory reset is
 *         already running or its reboot is scheduled; nothing is run. A delayed reset that finds one running
 *         is dropped.
 */
esp_rmaker_error_t esp_rmaker_system_ctrl_factory_reset(uint8_t reset_s, int8_t reset_reboot_s, esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn);

/**
 * @brief Run a factory reset started by a registered participant, immediately and skipping only
 *        that participant's own wipe.
 *
 * Notifies the cloud that the node is resetting itself (best-effort, bounded wait), resets the
 * network credentials, clears the NVS namespaces owned by RainMaker Neo, then wipes every
 * registered participant except @p self, which is already wiping its own state.
 *
 * This is the entry point for a factory reset that originates outside RainMaker Neo, in a component
 * that mirrors the data model onto another protocol. A factory reset requested while another is
 * running, or after its reboot has been scheduled, runs nothing and returns ESP_RMAKER_INVALID_STATE,
 * whether a participant's own wipe triggers it in turn or another task asks for one at the same time.
 * A participant that gets this from inside its own wipe can ignore it: the running reset already
 * includes that wipe.
 *
 * Runs synchronously on the calling thread, wipes included; the caller must have the stack for them.
 *
 * Posts RMAKER_EVENT_FACTORY_RESET when called, as esp_rmaker_system_ctrl_factory_reset() does.
 *
 * Best-effort: every step runs even if an earlier one failed, and the first error is returned.
 *
 * @param[in] self The caller's own wipe function, skipped in the participant sweep. NULL wipes every
 *            participant.
 * @param[in] network_reset_fn Function to reset the network credentials. NULL means use the function
 *            registered via esp_rmaker_system_ctrl_register_network_reset_fn(); if nothing is
 *            registered either, the network reset is skipped (logged) and the data is still cleared.
 *            Pass a no-op if the caller's own reset already clears them.
 * @param[in] reset_reboot_s The timeout in seconds to reboot after the wipe. 0 means reboot
 *            immediately; a negative value means do not reboot, leaving the restart to the caller.
 *
 * @return ESP_RMAKER_OK if everything succeeded, ESP_RMAKER_INVALID_STATE if a factory reset is already
 *         running or its reboot is scheduled (nothing is run), otherwise the first error encountered.
 */
esp_rmaker_error_t esp_rmaker_system_ctrl_factory_reset_from_participant(esp_rmaker_system_ctrl_factory_reset_wipe_fn_t self,
        esp_rmaker_system_ctrl_network_reset_fn_t network_reset_fn, int8_t reset_reboot_s);

/**
 * @brief Register a factory reset participant.
 *
 * Registering the same @c wipe function twice updates the existing entry instead of adding another.
 *
 * Not thread-safe: call from one task, while no factory reset can run - before esp_rmaker_start(), or
 * after esp_rmaker_stop(). A reset reads the participant table without a lock.
 *
 * @param[in] participant Participant description. Copied; the struct itself need not outlive the
 *            call, but @c name and @c priv must.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG if @p participant or its @c wipe is NULL,
 *         ESP_RMAKER_INVALID_STATE if another participant has already claimed @c reboots_on_wipe,
 *         ESP_RMAKER_NO_MEM if the participant table is full
 *         (CONFIG_ESP_RMAKER_NEO_SYSTEM_CTRL_RESET_PARTICIPANTS_MAX entries).
 */
esp_rmaker_error_t esp_rmaker_system_ctrl_factory_reset_participant_register(const esp_rmaker_system_ctrl_factory_reset_participant_t *participant);

/**
 * @brief Unregister a factory reset participant.
 *
 * Not thread-safe: call from one task, while no factory reset can run - before esp_rmaker_start(), or
 * after esp_rmaker_stop(). A reset reads the participant table without a lock.
 *
 * @param[in] wipe The wipe function used at registration.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG if @p wipe is NULL,
 *         ESP_RMAKER_NOT_FOUND if it is not registered.
 */
esp_rmaker_error_t esp_rmaker_system_ctrl_factory_reset_participant_unregister(esp_rmaker_system_ctrl_factory_reset_wipe_fn_t wipe);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_SYSTEM_CTRL_H__ */
