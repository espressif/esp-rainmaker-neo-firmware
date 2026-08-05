/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file schedules.h
 * @brief Schedule service declarations (per-node).
 *
 * Schedules are per-node: each node owns its esp_schedule handle list in
 * ``node->schedule`` (see node_internal.h). A getSchedDetails for a node
 * replaces only that node's schedules; others are untouched. Per-node
 * handle lists are guarded by the per-node lock (::esp_rmaker_node_lock).
 *
 * Persistence is owned by this service, not by esp_schedule: esp_schedule
 * runs with NVS disabled and we store the raw details JSON per node
 * instead (self -> ``local_config``; child -> the ``bridge_scheds``
 * namespace). esp_schedule's own NVS keeps only the trigger config, so a
 * fired schedule's action payload would not survive a reboot; replaying
 * the stored JSON reproduces both through the normal build path.
 *
 * Each schedule's esp_schedule name is a 14-char hex-encoded
 * SHA-256(local_id ":" cloud_id) prefix, so child schedules are
 * namespaced and self/child cloud ids cannot collide.
 *
 * What is stored is re-serialized from the live schedules rather than the
 * cloud payload verbatim, so it carries each trigger's computed next-fire
 * time and omits anything that was refused. Spent schedules are removed: a
 * one-shot when it fires, and any schedule found to have no remaining
 * occurrence at the moment we arm it. Pruning deliberately leaves the
 * persisted schedule *version* untouched, so the device's details
 * intentionally diverge from the cloud's copy for expired one-shots --
 * voiding the version would make the cloud re-push them.
 */

#ifndef __SERVICES_SCHEDULES_H__
#define __SERVICES_SCHEDULES_H__

#include "esp_rmaker_node.h"
#include "esp_rmaker_error_types.h"
#include "sdkconfig.h"
#include "time_sync_flow.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "esp_rmaker_bridge.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the schedule service
 * @note Requires timesync to be initialized. If timesync is not initialized, this function will fail.
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_FAIL if timesync is not initialized or if registering the timezone change event handler fails.
 * @return ESP_RMAKER_NO_MEM if memory allocation fails.
 */
esp_rmaker_error_t esp_rmaker_schedule_service_init(void);

/**
 * @brief Deinitialize the schedule service
 * @note If the service is not initialized, this function returns ESP_RMAKER_OK without doing anything.
 * @return ESP_RMAKER_OK on success (including if not initialized).
 * @return Error code if clearing the schedule list fails or if unregistering the timezone change event handler fails.
 */
esp_rmaker_error_t esp_rmaker_schedule_service_deinit(void);

/**
 * @brief On start, rebuild the self node's schedules from NVS.
 *
 * Initializes esp_schedule with NVS disabled, then replays the self node's
 * persisted details JSON through the normal build path. Bridge children are
 * not in the pool yet at this point (they arrive only after MQTT is up), so
 * each child is rebuilt from its own stored payload by
 * ::esp_rmaker_schedule_service_on_child_added once
 * ::esp_rmaker_bridge_add_child registers the slot. Nothing is materialized
 * before its owning node exists, so no orphan parking is needed.
 *
 * A stored payload that no longer builds voids that node's persisted
 * schedule version so the cloud re-pushes; it is not a fatal boot error.
 *
 * @note If the service is not initialized, returns ESP_RMAKER_OK and logs a warning.
 */
esp_rmaker_error_t esp_rmaker_schedule_service_on_start(void);

#if TIME_SYNC_DECOUPLED_FLOW
/**
 * @brief Arm every enabled schedule across all nodes now that wall-clock
 *        time has synced.
 *
 * Until this is called, schedule arming is deferred (see the decoupled
 * time-sync flow in core.c): handles are created/rehydrated and kept in
 * the node's RAM list + NVS but ``esp_schedule_enable`` is not called, so
 * no next-fire is computed off an unsynced (~1970) clock - they simply do
 * not fire yet. This latches an internal "time ready" flag - so schedules
 * created afterwards arm inline - and re-arms every handle currently held
 * by any node.
 *
 * Invoked from the core time-sync poll's success path. Idempotent; a
 * no-op if the service is not initialized.
 *
 * Decoupled flow only - in the synchronous MBEDTLS_HAVE_TIME_DATE flow the
 * clock is already valid before any schedule work, so arming is inline and
 * this entry point is compiled out.
 */
void esp_rmaker_schedule_service_arm_all(void);
#endif

/**
 * @brief Update schedule details for ``node``.
 *
 * Replaces only that node's schedules (other nodes untouched). Each new
 * schedule is created with a namespaced NVS-safe name and priv_data
 * carrying the node's owner key. Queued to the work queue; the build runs
 * under the node lock. On a successful build the payload is persisted for
 * that node so the next boot can replay it.
 *
 * @param[in] node Owning node (self or a bridge child).
 * @param[in] data Schedule details as a NUL-terminated JSON array string.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_schedule_service_update_details_for_node(const esp_rmaker_node_t *node, const char *data);

/**
 * @brief Update self-node schedule details (back-compat shim around
 *        ::esp_rmaker_schedule_service_update_details_for_node).
 *
 * @param[in] data Schedule details as a NUL-terminated JSON array string.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_schedule_service_update_details(const char *data);

/**
 * @brief Release a node's embedded schedule list from memory only - the
 *        node's persisted details JSON survives, so a later on_start /
 *        on_child_added rebuilds the schedules.
 *
 *        For a permanent removal that also drops the stored JSON (cloud-
 *        confirmed bridge child remove, add-child rollback), call
 *        ::esp_rmaker_schedule_service_erase_node instead.
 *
 * @param[in] node Node handle. Safe on a node with no schedules.
 */
void esp_rmaker_schedule_service_unload_node(const esp_rmaker_node_t *node);

/**
 * @brief Permanently delete every schedule owned by ``node`` - frees
 *        the RAM list AND erases the node's persisted details JSON.
 *
 * @param[in] node Node handle. Safe on a node with no schedules.
 */
void esp_rmaker_schedule_service_erase_node(const esp_rmaker_node_t *node);

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
/**
 * @brief Rebuild ``child``'s schedules from its persisted details JSON,
 *        now that the child slot is registered and its node resolvable.
 * @param[in] child Newly added child handle.
 */
void esp_rmaker_schedule_service_on_child_added(esp_rmaker_bridge_child_handle_t child);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __SERVICES_SCHEDULES_H__ */
