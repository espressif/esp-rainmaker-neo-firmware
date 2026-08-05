/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file state_changes.h
 * @brief Handle state changes.
 */

#ifndef __RMAKER_NETWORK_STATE_CHANGES_H__
#define __RMAKER_NETWORK_STATE_CHANGES_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Error types includes */
#include "esp_rmaker_error_types.h"

/* Public state includes */
#include "esp_rmaker_state.h"

/* Value includes */
#include "esp_rmaker_val.h"

/* Node includes */
#include "esp_rmaker_node.h"

/* Checksum includes (RMAKER_CHECKSUM_LEN for the ncfg_ver hash payload) */
#include "checksum_impl.h"

/* JSON includes */
#include "json_parser.h"
#include "json_generator.h"

/* Topic ctx includes */
#include "network/mqtt_topics.h"

/* Types **********************************************************************/

/**
 * @brief Per-entry kind / flag set for an update_info_t.
 *
 * Bitmask over orthogonal "special" properties an entry can carry. A
 * value of zero means the entry is a normal parameter update keyed by
 * ``update_id``. Any non-zero flag set means the entry is synthetic -
 * ``update_id`` is unused and the entry's value comes from a
 * flag-specific field in the union below.
 *
 * Synthetic entries dedupe against entries with the **exact same flag
 * bitmask**: re-inserting overrides the stored value in place rather
 * than appending a second entry.
 */
typedef enum {
    /** Normal parameter update - interpret update_id. */
    RMAKER_STATE_UPDATE_FLAG_NONE = 0,
    /** ``online`` shadow field - value carried in ``online_value``.
     *  Emitted as ``"online": <bool>`` into both the named (params) and
     *  indexed (iparams) shadow payloads for the entry's thing. */
    RMAKER_STATE_UPDATE_FLAG_ONLINE = (1 << 0),
    /** ``ncfg_ver`` shadow field - value carried in ``ncfg_ver_hash``.
     *  Emitted as ``"ncfg_ver": "<hex>"`` (the node-config SHA-256 checksum
     *  rendered as a lowercase hex string, no ``0x``) into both the named
     *  (params) and indexed (iparams) shadow payloads for the entry's thing.
     *  The checksum is a change-token: it differs iff the node config
     *  differs, so it needs no synced wall clock. */
    RMAKER_STATE_UPDATE_FLAG_NCFG_VER = (1 << 1),
} esp_rmaker_state_update_flag_t;

/**
 * @brief Flag-specific value bag. Active member is selected by ``flags``.
 */
typedef union {
    bool online_value;     /**< Used iff ``flags & RMAKER_STATE_UPDATE_FLAG_ONLINE``. */
    /** Used iff ``flags & RMAKER_STATE_UPDATE_FLAG_NCFG_VER``. Raw
     *  node-config SHA-256 checksum bytes; formatted to lowercase hex at
     *  JSON emit time. Carrying bytes (not the 64-char string) keeps the
     *  payload struct small. */
    uint8_t ncfg_ver_hash[RMAKER_CHECKSUM_LEN];
} esp_rmaker_state_update_flag_payload_t;

/** Update info */
typedef struct esp_rmaker_state_update_info_t {
    /** Parameter update id (NULL when ``flags != 0``). */
    esp_rmaker_state_update_id_t update_id;
    /** Bitmask of ::esp_rmaker_state_update_flag_t values. */
    uint8_t flags;
    /** Flag-specific value bag. Active member is selected by ``flags``. */
    esp_rmaker_state_update_flag_payload_t flag_payload;
    uint64_t timestamp_ms;
    struct esp_rmaker_state_update_info_t *next;
} esp_rmaker_state_update_info_t;

/* Function declarations **********************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the variables used by state change management.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_state_init(void);

/**
 * @brief De-initialize the variables used by state change management.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_state_deinit(void);

/**
 * @brief Lock the state. Should be called before any operation that modifies the state.
 * @note The 'state' here refers to anything that is updated to the shadows.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_state_lock(void);

/**
 * @brief Unlock the state. Should be called after any operation that modifies the state.
 * @note The 'state' here refers to anything that is updated to the shadows.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_state_unlock(void);

/**
 * @brief Attempt to start listening for state changes, i.e., subscribe to the 'params to node' topic.
 * @note This will continue to retry until the subscription is successful.
 * @note This function will not block.
 *
 * @param[in] timeout_ms Timeout in milliseconds.
 */
void esp_rmaker_state_attempt_start_listening(uint32_t timeout_ms);

/**
 * @brief Stop listening for state changes, i.e., unsubscribe from the 'params to node' topic.
 * @note This blocks until the unsubscription is complete or the timeout is reached.
 *
 * @param[in] timeout_ms Timeout in milliseconds.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_state_stop_listening(uint32_t timeout_ms);

/**
 * @brief Cancel any pending state-report retry.
 * @note Flow-stop only. Call from the stop path.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_state_stop_reporting(void);

/**
 * @brief Schedule a report of the state changes of the node.
 *
 * @note If there already is a scheduled report, this function will reset the timer for the existing scheduled task.
 * @param[in] report_all If true, report the entire state of the node. If false, only report the state changes.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_FAIL if scheduling the report task fails.
 */
esp_rmaker_error_t esp_rmaker_state_schedule_report(bool report_all);

/**
 * @brief Schedule a report of the state changes for a single node.
 *
 * When ``report_all`` is true, populates the full state for the given
 * node only (enumerates only that node's update IDs from the data
 * model) and triggers the scheduler. The scheduler itself is global by
 * design - it drains every per-node list in one tick - so this call
 * queues just one node's full state without disturbing other nodes'
 * pending lists.
 *
 * Synthetic ONLINE / NCFG_VER entries are auto-inserted for the self
 * node only. For child nodes the caller stages those via
 * ::esp_rmaker_state_mark_for_update_online_for_node and
 * ::esp_rmaker_state_mark_for_update_ncfg_ver_for_node before calling, since
 * their source-of-truth lives in the bridge layer (per-child NVS).
 *
 * @param[in] node       Node handle (self or a bridge child node).
 * @param[in] report_all If true, populate the node's full state.
 *
 * @return ESP_RMAKER_OK on success.
 */
esp_rmaker_error_t esp_rmaker_state_schedule_report_for_node(const esp_rmaker_node_t *node, bool report_all);

/**
 * @brief Report the state of the node.
 *
 * @note If there is a scheduled report, this function will cancel it and report the state immediately.
 * @note This function always returns ESP_RMAKER_OK as it executes the report synchronously and does not wait for completion.
 *
 * @param[in] report_all If true, report the entire state of the node. If false, only report the state changes.
 *
 * @return ESP_RMAKER_OK (always returns success).
 */
esp_rmaker_error_t esp_rmaker_state_report(bool report_all);

/**
 * @brief Delete the named shadow for ``node``.
 *
 * Builds the delete topic using the node's topic ctx ops, which read
 * the CURRENT in-memory group_info_str on that node. Callers performing
 * a migration must invoke this BEFORE updating the node's group_info_str
 * so that the OLD shadow is the one deleted.
 *
 * Pass ``NULL`` for self.
 */
esp_rmaker_error_t esp_rmaker_state_delete_named_shadow_for_node(const esp_rmaker_node_t *node);

/**
 * @brief Insert (or replace) an ::RMAKER_STATE_UPDATE_FLAG_ONLINE entry
 *        in the state ctx identified by ``node``.
 *
 * The state ctx is auto-allocated on first use, keyed by the node
 * pointer. Re-calling with a different value overwrites the entry's
 * stored value in place (no duplicate entry).
 *
 * The next published state report carries ``"online": <online>`` at the
 * top level of both the named (params) shadow and the indexed (iparams)
 * shadow for the node's Thing.
 *
 * @note Teardown is implicit: when a child node's topic ctx is later
 *       invalidated by its owner (slot freed), the state ctx and its
 *       pending update list are reaped on the next publish cycle.
 *
 * @param[in] node      Node handle. ``NULL`` -> self.
 * @param[in] online    New online value.
 */
esp_rmaker_error_t esp_rmaker_state_mark_for_update_online_for_node(const esp_rmaker_node_t *node, bool online);

/**
 * @brief Insert (or replace) an ::RMAKER_STATE_UPDATE_FLAG_NCFG_VER entry
 *        in the state ctx identified by ``node``.
 *
 * The next published state report carries ``"ncfg_ver": "<hex>"`` at the
 * top level of both the named (params) and indexed (iparams) shadow for
 * the node's Thing, where ``<hex>`` is ``hash`` rendered as lowercase hex.
 *
 * @param[in] node  Node handle (NULL -> self).
 * @param[in] hash  New node-config SHA-256 checksum (RMAKER_CHECKSUM_LEN bytes).
 */
esp_rmaker_error_t esp_rmaker_state_mark_for_update_ncfg_ver_for_node(const esp_rmaker_node_t *node, const uint8_t hash[RMAKER_CHECKSUM_LEN]);

/**
 * @brief Flush any cached state for ``node``: discard pending updates,
 *        unlink and free the per-node state ctx entry (if any), and
 *        clear the self-anchor cache when ``node`` is the self node.
 *
 * Called from ::esp_rmaker_node_delete so downstream caches don't
 * dangle on the freed pointer. Safe on nodes that were never touched
 * by the state pipeline - no-op.
 */
void esp_rmaker_state_drop_node(const esp_rmaker_node_t *node);

/**
 * @brief Flush cached state for every node (self + ready bridge children)
 *        via ::esp_rmaker_node_for_each. Each node is dropped under its own
 *        lock (bridge pool -> node ordering preserved).
 */
void esp_rmaker_state_drop_all_nodes(void);

#ifdef __cplusplus
}
#endif

#endif /* __RMAKER_NETWORK_STATE_CHANGES_H__ */
