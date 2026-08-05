/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file node_config_pending.h
 * @brief Per-node pending list for the shared node-config retry context.
 *
 * The node-config retry context drains a list of nodes each tick:
 *  - self node is added at init,
 *  - each bridge child node is added on add_child and on commit_devices,
 *  - removed on clean ack (checksum persisted) or on remove_child.
 *
 * Each entry carries an ``inflight`` flag. On retry-tick fire, the
 * drain iterates all entries and publishes for every non-inflight node
 * sequentially, then awaits the ack callback (which removes the entry
 * on success or clears inflight on failure / stale-ack).
 */

#ifndef __NODE_CONFIG_PENDING_H__
#define __NODE_CONFIG_PENDING_H__

#include "sdkconfig.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

#include <stdbool.h>

#include "esp_rmaker_error_types.h"
#include "esp_rmaker_node.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One-time init (mutex, slot table). Idempotent; populates the
 *        self node entry on first call.
 */
esp_rmaker_error_t node_config_pending_init(void);

/**
 * @brief Idempotently insert ``node`` into the pending list. ``inflight``
 *        is cleared on (re-)insert. Safe to call from any task.
 */
esp_rmaker_error_t node_config_pending_add(const esp_rmaker_node_t *node);

/**
 * @brief Remove ``node`` from the pending list. Called from the ack
 *        success path and on remove_child.
 */
void node_config_pending_remove(const esp_rmaker_node_t *node);

/**
 * @brief Clear the inflight flag for ``node`` while leaving the entry
 *        in the list. Called from the ack failure / stale-ack path so
 *        the next retry tick republishes.
 */
void node_config_pending_clear_inflight(const esp_rmaker_node_t *node);

/**
 * @brief Clear inflight on every entry. Called on cloud-manager
 *        disconnect so a reconnect refires all pending entries.
 */
void node_config_pending_clear_all_inflight(void);

/**
 * @brief Drain-all: iterate every entry and publish for each that is
 *        not currently inflight and whose checksum differs from the
 *        persisted one. Sequential within a tick to bound JSON-buffer
 *        memory.
 *
 * Invoked by the node-config retry context's work fn.
 */
esp_rmaker_error_t node_config_pending_fire_all(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

#endif /* __NODE_CONFIG_PENDING_H__ */
