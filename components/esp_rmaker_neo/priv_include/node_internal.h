/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file node_internal.h
 * @brief Internal node data model.
 */

#ifndef __NODE_INTERNAL_H__
#define __NODE_INTERNAL_H__

/* Includes *******************************************************/

/* Standard includes. */
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>

/* Node includes. */
#include "esp_rmaker_node.h"

/* Value includes. */
#include "esp_rmaker_val.h"

/* Error includes. */
#include "esp_rmaker_error_types.h"

/* Platform includes. */
#include "osal_semaphore.h"

/* JSON includes. */
#include "json_parser.h"
#include "json_generator.h"

/* State includes. */
#include "network/state_changes.h"

/* Topic ctx includes. */
#include "network/mqtt_topics.h"

/* Configuration includes. */
#include "sdkconfig.h"


/* Data model includes. */
#include "data_model_internal.h"

/* Constants *******************************************************/

/* Info keys */
#define RMAKER_INFO_KEY_NAME "name"
#define RMAKER_INFO_KEY_TYPE "type"
#define RMAKER_INFO_KEY_FW_VERSION "fw_version"
#define RMAKER_INFO_KEY_MODEL "model"

/**
 * @brief Length of the SHA-256 node-tags checksum.
 */
#define RMAKER_NODE_TAGS_CHECKSUM_LEN 32

/* Public structures *******************************************************/

typedef enum {
    /** Node is online */
    RMAKER_NODE_STATUS_FLAG_ONLINE = (1 << 0),
} esp_rmaker_node_status_flag_t;

/* Per-manager embedded state ************************************************
 *
 * Each per-node manager owns one substruct on the node. Substructs are
 * memset-zero at ::_esp_rmaker_node_init time; managers that need to
 * release heap-owned fields hook into ::_esp_rmaker_node_reset (or are
 * wired through ::esp_rmaker_node_delete cleanup hooks).
 *
 * Node module never touches substruct internals - only the owning
 * manager does. This file is the canonical layout schema; managers
 * include node_internal.h to reach their fields via ``&node->field``.
 */

/**
 * @brief Tag-checksum manager state (network/state_changes.c).
 *
 * Per-node SHA-256 of the set of tags published to the cloud.
 * ``committed`` is the live value (cleared on publish failure so the
 * next report re-emits everything). ``pending`` is the snapshot taken
 * at the most recent publish; the indexed-shadow ack handler commits
 * it to ``committed`` (and to per-node NVS) on success. Two-phase so
 * concurrent publishes degrade to last-write-wins, not corruption.
 */
typedef struct {
    uint8_t committed[RMAKER_NODE_TAGS_CHECKSUM_LEN];
    uint8_t pending[RMAKER_NODE_TAGS_CHECKSUM_LEN];
    bool committed_set;
    bool pending_set;
} node_tag_checksum_state_t;

/**
 * @brief State-pipeline manager state (network/state_changes.c).
 *
 * Per-node staging list for pending param/value updates awaiting the
 * next scheduled state report. ``head == NULL`` <=> "no pending updates
 * for this node" -> the report task skips it on its next wake. Lock:
 * ::esp_rmaker_state_lock / ::esp_rmaker_state_unlock from
 * network/state_changes.h. The list is freed by
 * ::esp_rmaker_state_drop_node on node delete.
 */
typedef struct {
    esp_rmaker_state_update_info_t *head;  /**< Linked list of pending updates. */
    size_t count;                          /**< Length of ``head``. */
} node_state_pipeline_state_t;

/**
 * @brief Node-config publish retry-queue state (node/node_config_pending.c).
 *
 * ``pending`` is set when the node has config dirty enough to need a
 * republish; the retry tick fires every pending node whose ``inflight``
 * is clear. ``inflight`` is set right before publish, cleared in the
 * ack callback (or on publish error). The same node can be re-marked
 * ``pending`` mid-flight (e.g. attaching more devices); on ack the
 * retry context picks it back up.
 */
typedef struct {
    bool pending;
    bool inflight;
} node_config_pending_state_t;

/* Forward decl (full type in services/automation.h, which includes this
 * header). A pointer to the incomplete type is all the substruct needs. */
typedef struct __automation_trigger_t esp_rmaker_automation_trigger_t;

/**
 * @brief Automation-trigger manager state (services/automation.c).
 *
 * Per-node trigger array, rebuilt wholesale for a node on each
 * getTriggerDetails for that node. Freed by ::esp_rmaker_automation_drop_node
 * on node reset/delete. Guarded by the per-node lock (::esp_rmaker_node_lock).
 */
typedef struct {
    esp_rmaker_automation_trigger_t *list;  /**< Per-node trigger array. */
    uint8_t count;                          /**< Length of ``list``. */
} node_automation_trigger_state_t;

/* Forward decl - full type is the opaque ``esp_schedule_handle_t`` from the
 * esp_schedule component (see services/schedules.c). Substruct only stores
 * the array of opaque handles so this header stays free of that dep. */
typedef void *esp_schedule_handle_t;

/**
 * @brief Schedule manager state (services/schedules.c).
 *
 * Per-node esp_schedule handle list, rebuilt wholesale for a node on each
 * getSchedDetails for that node. Each handle's priv_data carries the action
 * to fire, the owning node's key (so a fired schedule can be routed back to
 * its node), the cloud-side schedule id, and whether the trigger is one-shot
 * (so a spent schedule can be dropped without re-reading esp_schedule).
 *
 * Handles hold no persistent state - esp_schedule runs with NVS disabled and
 * the schedule service persists the details JSON per node instead - so they are
 * simply torn down via ``esp_schedule_delete`` from
 * ::esp_rmaker_schedule_service_unload_node on node reset/delete, and rebuilt from the
 * stored JSON on the next boot. Guarded by the per-node lock
 * (::esp_rmaker_node_lock).
 */
typedef struct {
    esp_schedule_handle_t *handles; /**< Per-node esp_schedule handle array. */
    uint8_t count;                  /**< Length of ``handles``. */
} node_schedule_state_t;


/**
 * @brief Node.
 */
typedef struct {
    /* Per-node lock. Serializes all embedded per-manager state below
     * (state_update, tag_check, pending_cfg, devices/attrs/tags). Created
     * once when the node's storage is first set up and PRESERVED across
     * ::_esp_rmaker_node_init / ::_esp_rmaker_node_reset (child slots are
     * reused). Destroyed only at node delete / slot-pool teardown.
     * Lock via ::esp_rmaker_node_lock / ::esp_rmaker_node_unlock. */
    osal_semaphore_handle_t lock;

    uint8_t status_flags; /**< Flags to indicate the status of the node, see esp_rmaker_node_status_flag_t */
    esp_rmaker_node_info_t *info;
    esp_rmaker_attr_t *attributes;
    esp_rmaker_tag_t *tags;
    esp_rmaker_topic_ctx_t topic_ctx; /**< Per-node MQTT-topic identity. */

    /* Per-manager embedded state. See substruct comments above for
     * ownership / lifecycle. Substructs are memset-zero at init. */
    node_tag_checksum_state_t       tag_check;
    node_state_pipeline_state_t     state_update;
    node_config_pending_state_t     pending_cfg;
    node_automation_trigger_state_t automation;
    node_schedule_state_t           schedule;

    _esp_rmaker_device_t *devices;
} _esp_rmaker_node_t;

/* Public functions *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the self node.
 *
 * @return Heap node handle on success; NULL on failure.
 */
esp_rmaker_node_t *esp_rmaker_node_create(const char *name, const char *type);

/**
 * @brief Lock the per-node mutex.
 *
 * Serializes access to the node's embedded per-manager state. Non-recursive:
 * never call while already holding this node's lock. Holds to the lock
 * ordering ``bridge children pool > state_mutex (global) > node lock`` - do
 * not acquire the bridge pool lock or ``esp_rmaker_state_lock`` while holding
 * a node lock.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG if node is NULL,
 *         ESP_RMAKER_INVALID_STATE if the node has no lock, ESP_RMAKER_FAIL on
 *         take failure.
 */
esp_rmaker_error_t esp_rmaker_node_lock(const esp_rmaker_node_t *node);

/**
 * @brief Unlock the per-node mutex. Counterpart to ::esp_rmaker_node_lock.
 */
esp_rmaker_error_t esp_rmaker_node_unlock(const esp_rmaker_node_t *node);

/**
 * @brief Initialise an already-allocated node in place.
 *
 * Preserves an existing ::lock handle across the in-place re-init so reused
 * child slots keep their mutex; creates one if absent.
 */
void _esp_rmaker_node_init(_esp_rmaker_node_t *node);

/**
 * @brief Free contents of an in-place node,
 *        leaving the struct itself intact.
 *        Counterpart to ::_esp_rmaker_node_init.
 */
void _esp_rmaker_node_reset(_esp_rmaker_node_t *node);

/**
 * @brief Delete a heap-allocated node (created by ::esp_rmaker_node_create).
 *
 * @note Recursively destroys devices/params. Do NOT call on a slot-embedded
 *       node - use ::_esp_rmaker_node_reset for those.
 */
esp_rmaker_error_t esp_rmaker_node_delete(const esp_rmaker_node_t *p_node);

/**
 * @brief Visitor for ::esp_rmaker_node_for_each.
 *
 * Return value is informational - iteration always completes.
 */
typedef esp_rmaker_error_t (*esp_rmaker_node_visitor_t)(const esp_rmaker_node_t *node, void *priv);

/**
 * @brief Iterate self + every READY bridge child node.
 *
 * Visitor is called with the self node first, then with each ready child
 * node (in slot-pool order). Bridge mutex is taken around child iteration
 * but released around the self-node visit.
 */
void esp_rmaker_node_for_each(esp_rmaker_node_visitor_t visitor, void *priv);

/**
 * @brief Get the topic ctx embedded in this node.
 *
 * @return Pointer to ``&node->topic_ctx`` (stable for the node's lifetime),
 *         or NULL if ``node`` is NULL.
 */
const esp_rmaker_topic_ctx_t *esp_rmaker_node_topic_ctx(const esp_rmaker_node_t *node);

/**
 * @brief True if ``node`` is the self node.
 */
bool esp_rmaker_node_is_self(const esp_rmaker_node_t *node);

/**
 * @brief True if ``node`` is in the named subgroup.
 *
 * Self node reads its subgroup membership from local_config; child nodes
 * read it from the bridge slot's ``group_info_str``. Returns ``false``
 * for NULL node, empty subgroup name, or a node with no known group info.
 */
bool esp_rmaker_node_is_in_subgroup(const esp_rmaker_node_t *node, const char *subgroup);

/**
 * @brief Resolve the cloud thing name for ``node`` via its topic ctx ops.
 *
 * Writes into ``buf`` (NUL-terminated). On failure (NULL node, missing
 * ops, or ops can't resolve - e.g. child not yet ack'd by cloud), writes
 * ``"<unknown>"``. Intended for log/debug strings; never returns -1, so
 * callsites can use ``%s`` without guarding.
 *
 * @return Bytes written (excluding NUL).
 */
int esp_rmaker_node_resolve_thing_name(const esp_rmaker_node_t *node, char *buf, size_t buf_size);

/**
 * @brief Set the online state of the node.
 *
 * @param[in] online True if the node is online, false otherwise.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_node_set_online(bool online);

/**
 * @brief Set the online state of a specific node.
 *
 * @param[in] node   The node.
 * @param[in] online True if the node is online, false otherwise.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_node_set_online_for_node(const esp_rmaker_node_t *node, bool online);

/**
 * @brief Get the online state of a node.
 *
 * @param[in] node The node.
 * @return true if the node is marked online, false otherwise (or node NULL).
 */
bool esp_rmaker_node_is_online(const esp_rmaker_node_t *node);

/**
 * @brief Get the node config as a JSON string.
 *
 * @return JSON string.
 */
char *esp_rmaker_get_node_config(void);

/**
 * @brief Per-node node-config report path.
 *
 * Internal entry point used by both the public ``esp_rmaker_report_node_config``
 * (self node) and the pending-list drain in ``node_config_pending``
 * (any node). Builds the per-node JSON slice, performs checksum dedup
 * against the appropriate NVS store (self ``chksum`` ns / per-child
 * record), and publishes via the cloud manager if changed.
 */
esp_rmaker_error_t esp_rmaker_internal_report_node_config_for_node(const esp_rmaker_node_t *node);

/**
 * @brief Get the first attribute in the node.
 *
 * @param[in] node Node handle.
 *
 * @return Pointer to the first attribute.
 */
esp_rmaker_attr_t *esp_rmaker_node_get_first_attribute(const esp_rmaker_node_t *node);

/**
 * @brief Get the first tag in the node.

 * @param[in] node Node handle.
 *
 * @return Pointer to the first tag.
 */
esp_rmaker_tag_t *esp_rmaker_node_get_first_tag(const esp_rmaker_node_t *node);

/**
 * @brief Delete the attribute.
 *
 * @param[in] attr Pointer to the attribute.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_attribute_delete(esp_rmaker_attr_t *attr);

/**
 * @brief Delete the tag.
 *
 * @param[in] tag Pointer to the tag.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_tag_delete(esp_rmaker_tag_t *tag);

/**
 * @brief Get the tag by name.
 *
 * @param[in] node Node handle.
 * @param[in] tag_name Tag name.
 *
 * @return Pointer to the tag if found, otherwise NULL.
 */
esp_rmaker_tag_t *esp_rmaker_node_get_tag_by_name(const esp_rmaker_node_t *node, const char *tag_name);

/* --- JSON formatting functions --- */

/**
 * @brief Parse a parameter's value from a JSON object.
 * @param[in] p_jctx The JSON context.
 * @param[in] param_id The id of the parameter.
 * @param[in] expected_type The expected type of the value.
 * @param[out] val The value to parse into.
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_NOT_FOUND if the parameter is not found, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_parse_val_from_object(jparse_ctx_t *p_jctx, const char *param_id, esp_rmaker_val_type_t expected_type, esp_rmaker_param_val_t *val);

/**
 * @brief Report the attribute.
 *
 * @param[in] attr Pointer to the attribute.
 * @param[in] jptr JSON generation string pointer.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_report_attribute(const esp_rmaker_attr_t *attr, json_gen_str_t *jptr);

/**
 * @brief Report the value of the parameter.
 *
 * @param[in] val Pointer to the value. If NULL, a null JSON value is written.
 * @param[in] key Key for the value.
 * @param[in] jptr JSON generation string pointer.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_report_value(const esp_rmaker_param_val_t *val, char *key, json_gen_str_t *jptr);

/**
 * @brief Report the data type of the parameter.
 *
 * @param[in] type Data type.
 * @param[in] data_type_key Key for the data type.
 * @param[in] jptr JSON generation string pointer.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_report_data_type(esp_rmaker_val_type_t type, char *data_type_key, json_gen_str_t *jptr);

#ifdef __cplusplus
}
#endif

#endif /* __NODE_INTERNAL_H__ */
