/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file automation.c
 * @brief Automation service implementation.
 *
 * Triggers are per-node: each node owns its trigger array in
 * ``node->automation`` (see node_internal.h). A getTriggerDetails for a node
 * replaces only that node's triggers; others are untouched. Per-node trigger
 * lists are guarded by the per-node lock (::esp_rmaker_node_lock). The service
 * keeps only the global trigger-state-report scheduler handle, guarded by its
 * own small mutex.
 */

/* Includes **********************************************************************/

/* Declarations */
#include "services/automation.h"

/* Standard C headers */
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

/* Platform common includes */
#include "osal_semaphore.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_scheduler.h"

/* RMNG includes */
#include "data_model_internal.h"
#include "node_internal.h"
#include "esp_rmaker_node.h"
#include "esp_rmaker_work_queue.h"
#include "esp_rmaker_runtime_gate.h"
#include "event_flags.h"
#include "network/notify.h"
#include "network/state_changes.h"

/* NVS includes */
#include "local_config.h"

/* Trigger details binary codec */
#include "util/esp_rmaker_trigger_codec.h"

/* Configuration */
#include "sdkconfig.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "bridge/bridge_internal.h"
#include "bridge/bridge_child_triggers_nvs.h"
#include "bridge/bridge_child_nvs.h"
#endif

/* Preprocessor definitions *******************************************************/

#define AUTOMATION_TRIGGER_STATE_REPORT_DELAY_MS 500

/* Types **********************************************************************/

typedef struct __update_id_tracker {
    esp_rmaker_state_update_id_t update_id;
    struct __update_id_tracker *next;
} __update_id_tracker_t;

typedef struct {
    __update_id_tracker_t *update_id_trackers; /**< The list of tracked update IDs */
    osal_scheduler_task_handle_t trigger_state_report_task_handle; /**< Handle for the scheduled trigger-state-report task (global) */
    osal_semaphore_handle_t mutex; /**< Guards trigger_state_report_task_handle only */
} __automation_priv_data_t;

/* Work-queue argument: owning node (stable pointer, not owned) + strdup'd
 * JSON (task frees). */
typedef struct {
    const esp_rmaker_node_t *node;
    char *data;
} __update_trigger_details_arg_t;

/* Variables **********************************************************************/

static const char *TAG = "rmng_svc_automation";

static __automation_priv_data_t *__priv_data;

/* Private function declarations *******************************************************/

/* --- Locking/unlocking (trigger-state-report scheduler handle only) --- */

/**
 * @brief Lock the service mutex guarding the report scheduler handle.
 */
static void __automation_sched_lock(void);

/**
 * @brief Unlock the service mutex guarding the report scheduler handle.
 */
static void __automation_sched_unlock(void);

/* --- Node helpers --- */

/**
 * @brief Get a node's embedded automation-trigger substruct.
 * @param[in] node The node.
 * @return Pointer to ``node->automation``, or NULL if ``node`` is NULL.
 */
static node_automation_trigger_state_t *__node_auto(const esp_rmaker_node_t *node);

/* --- Trigger list operations --- */

/**
 * @brief Free resources held by one trigger slot (id, update_id, heap-backed value).
 * @param[in] t The trigger slot to reset.
 */
static void __automation_trigger_reset(esp_rmaker_automation_trigger_t *t);

/**
 * @brief Free a node's trigger array and zero its count. Assumes the caller
 *        holds the owning node's lock.
 * @param[in] a The node's automation-trigger substruct.
 */
static void __trigger_list_free(node_automation_trigger_state_t *a);

/* --- JSON parsing --- */

/**
 * @brief Parse trigger details JSON and install them on ``node``, replacing
 *        the node's existing triggers. Transcodes JSON->binary once and builds
 *        the live list from the binary (the canonical form). Builds into a temp
 *        list and swaps in only on success, so a parse failure leaves the
 *        node's live triggers intact. Assumes the caller holds ``node``'s lock.
 * @param[in]  node     The owning node.
 * @param[in]  data     The JSON array string.
 * @param[in]  data_len Length of the JSON data.
 * @param[out] out_blob If non-NULL and the build succeeds, receives the encoded
 *                      binary blob (caller frees) so it can be persisted without
 *                      re-encoding. Otherwise the blob is freed internally.
 * @param[out] out_blob_len Length of ``*out_blob`` (required if out_blob set).
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __build_trigger_details_for_node_locked(const esp_rmaker_node_t *node, const char *data,
        size_t data_len, uint8_t **out_blob, size_t *out_blob_len);

/**
 * @brief Persist a pre-encoded trigger-details blob to a node's NVS store
 *        (self -> local_config, child -> per-child ``bridge_triggers`` blob).
 * @param[in] node     The owning node.
 * @param[in] blob     The encoded binary blob.
 * @param[in] blob_len Blob length.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __persist_trigger_blob(const esp_rmaker_node_t *node, const uint8_t *blob, size_t blob_len);

/* --- Trigger state report --- */

/**
 * @brief Trigger state report scheduler task. Queues the report task.
 * @param[in] unused Unused.
 */
static void __trigger_state_report_scheduler_task(void *unused);

/**
 * @brief Trigger state report task. Walks every node and reports each node's
 *        changed triggers to its own notify topic.
 * @param[in] unused Unused.
 */
static void __trigger_state_report_task(void *unused);

/**
 * @brief Trigger state report payload function for one node. Writes that
 *        node's changed-trigger state to the JSON generator.
 * @param[in] jptr Pointer to the JSON generator.
 * @param[in] node_arg The node whose triggers to report (the report visitor
 *                      holds its lock for the duration of this call).
 * @param[in] is_sizing Whether this call is to size the payload.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __trigger_state_report_payload_fn(json_gen_str_t *jptr, void *node_arg, bool is_sizing);

/**
 * @brief Work queue task to update a node's trigger details.
 * @param[in] arg Heap ::__update_trigger_details_arg_t (node + strdup'd JSON).
 */
static void __update_trigger_details_work_queue_task(void *arg);

/* Private function definitions *******************************************************/

/* --- Locking (scheduler handle only) --- */

static void __automation_sched_lock(void)
{
    osal_semaphore_take(__priv_data->mutex, OSAL_MAX_DELAY);
}

static void __automation_sched_unlock(void)
{
    osal_semaphore_give(__priv_data->mutex);
}

/* --- Node helpers --- */

static node_automation_trigger_state_t *__node_auto(const esp_rmaker_node_t *node)
{
    return node ? &((_esp_rmaker_node_t *)node)->automation : NULL;
}

/* --- Trigger list operations (caller holds the node lock) --- */

static void __automation_trigger_reset(esp_rmaker_automation_trigger_t *t)
{
    if (!t) {
        return;
    }
    if (t->id) {
        free(t->id);
        t->id = NULL;
    }
    if (t->update_id) {
        data_model_state_update_id_release(t->update_id);
        t->update_id = NULL;
    }
    (void)esp_rmaker_val_free(&t->expected_val);
    memset(t, 0, sizeof(*t));
}

static void __trigger_list_free(node_automation_trigger_state_t *a)
{
    if (!a) {
        return;
    }
    if (a->list) {
        for (uint8_t i = 0; i < a->count; i++) {
            __automation_trigger_reset(&a->list[i]);
        }
        free(a->list);
        a->list = NULL;
    }
    a->count = 0;
}

/* --- Binary entry parsing --- */

/* NUL-terminated heap copy of n bytes. NULL on alloc failure. */
static char *__dup_n(const char *p, size_t n)
{
    char *s = (char *)OSAL_CALLOC_EXTRAM(1, n + 1);
    if (s) {
        memcpy(s, p, n);
    }
    return s;
}

/* Coerce a decoded lexical value to the data-model expected type, mirroring
 * the JSON path (``esp_rmaker_parse_val_from_object``): int promotes to float
 * and 0/1 to bool; everything else must match the expected category. */
static esp_rmaker_error_t __coerce_value(const esp_rmaker_trigger_entry_t *e,
        esp_rmaker_val_type_t expected, esp_rmaker_param_val_t *val)
{
    switch (expected) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        if (e->value_type == RMAKER_TRIGGER_VT_BOOL) {
            val->val.b = e->value_bool;
        } else if (e->value_type == RMAKER_TRIGGER_VT_INT && (e->value_int == 0 || e->value_int == 1)) {
            val->val.b = (e->value_int != 0);
        } else {
            return ESP_RMAKER_INVALID_ARG;
        }
        val->type = RMAKER_VAL_TYPE_BOOLEAN;
        return ESP_RMAKER_OK;
    case RMAKER_VAL_TYPE_INTEGER:
        if (e->value_type != RMAKER_TRIGGER_VT_INT) {
            return ESP_RMAKER_INVALID_ARG;
        }
        val->val.i = e->value_int;
        val->type = RMAKER_VAL_TYPE_INTEGER;
        return ESP_RMAKER_OK;
    case RMAKER_VAL_TYPE_FLOAT:
        if (e->value_type == RMAKER_TRIGGER_VT_FLOAT) {
            val->val.f = e->value_float;
        } else if (e->value_type == RMAKER_TRIGGER_VT_INT) {
            val->val.f = (float)e->value_int;
        } else {
            return ESP_RMAKER_INVALID_ARG;
        }
        val->type = RMAKER_VAL_TYPE_FLOAT;
        return ESP_RMAKER_OK;
    case RMAKER_VAL_TYPE_STRING:
    case RMAKER_VAL_TYPE_OBJECT:
    case RMAKER_VAL_TYPE_ARRAY: {
        uint8_t want = (expected == RMAKER_VAL_TYPE_STRING) ? RMAKER_TRIGGER_VT_STRING
                       : (expected == RMAKER_VAL_TYPE_OBJECT) ? RMAKER_TRIGGER_VT_OBJECT
                       : RMAKER_TRIGGER_VT_ARRAY;
        if (e->value_type != want) {
            return ESP_RMAKER_INVALID_ARG;
        }
        val->val.s = __dup_n(e->value_str, e->value_str_len);
        if (!val->val.s) {
            return ESP_RMAKER_NO_MEM;
        }
        val->type = expected;
        return ESP_RMAKER_OK;
    }
    default:
        return ESP_RMAKER_INVALID_ARG;
    }
}

/* Map a decoded operator code to the compare enum, enforcing that ordered
 * comparisons (gt/lt/ge/le) only apply to numeric types. Returns
 * ESP_RMAKER_INVALID_ARG on an op disallowed for the value type. */
static esp_rmaker_error_t __op_to_compare(uint8_t op_code, esp_rmaker_val_type_t expected,
        esp_rmaker_val_compare_t *out)
{
    switch (op_code) {
    case RMAKER_TRIGGER_OP_EQ: *out = RMAKER_VAL_COMPARE_EQ;  return ESP_RMAKER_OK;
    case RMAKER_TRIGGER_OP_NE: *out = RMAKER_VAL_COMPARE_NEQ; return ESP_RMAKER_OK;
    default: break;
    }
    if (expected == RMAKER_VAL_TYPE_BOOLEAN || expected == RMAKER_VAL_TYPE_STRING
            || expected == RMAKER_VAL_TYPE_OBJECT || expected == RMAKER_VAL_TYPE_ARRAY) {
        OSAL_LOGE(TAG, "Ordered operator not supported for boolean/string-like values; only eq/ne.");
        return ESP_RMAKER_INVALID_ARG;
    }
    switch (op_code) {
    case RMAKER_TRIGGER_OP_GT: *out = RMAKER_VAL_COMPARE_GT;  return ESP_RMAKER_OK;
    case RMAKER_TRIGGER_OP_LT: *out = RMAKER_VAL_COMPARE_LT;  return ESP_RMAKER_OK;
    case RMAKER_TRIGGER_OP_GE: *out = RMAKER_VAL_COMPARE_GTE; return ESP_RMAKER_OK;
    case RMAKER_TRIGGER_OP_LE: *out = RMAKER_VAL_COMPARE_LTE; return ESP_RMAKER_OK;
    default: return ESP_RMAKER_FAIL;
    }
}

/* Build one live trigger from a decoded entry. Caller has already checked
 * ``entry->enabled``. On any failure the partially-built trigger is reset. */
static esp_rmaker_error_t __parse_trigger_from_entry_locked(const esp_rmaker_node_t *node,
        const esp_rmaker_trigger_entry_t *entry, esp_rmaker_automation_trigger_t *trigger)
{
    if (!node || !entry || !trigger) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;

    if (entry->id_len == 0) {
        err = ESP_RMAKER_FAIL;
        goto fail;
    }
    trigger->id = __dup_n(entry->id, entry->id_len);
    if (!trigger->id) {
        err = ESP_RMAKER_NO_MEM;
        goto fail;
    }

    /* Resolve path -> update id (path must be NUL-terminated for the lookup). */
    char *path_buf = __dup_n(entry->path, entry->path_len);
    if (!path_buf) {
        err = ESP_RMAKER_NO_MEM;
        goto fail;
    }
    trigger->update_id = data_model_path_to_update_id_for_node(node, path_buf);
    free(path_buf);
    if (!trigger->update_id) {
        OSAL_LOGE(TAG, "Failed to resolve trigger path to update ID.");
        err = ESP_RMAKER_FAIL;
        goto fail;
    }

    esp_rmaker_val_type_t expected_val_type =
        data_model_state_expected_val_type_from_update_id(trigger->update_id);
    if (expected_val_type == RMAKER_VAL_TYPE_INVALID) {
        OSAL_LOGE(TAG, "Failed to get expected value type from update ID.");
        err = ESP_RMAKER_FAIL;
        goto fail;
    }

    err = __op_to_compare(entry->op_code, expected_val_type, &trigger->compare_op);
    if (err != ESP_RMAKER_OK) {
        goto fail;
    }

    err = __coerce_value(entry, expected_val_type, &trigger->expected_val);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Trigger value type mismatch.");
        goto fail;
    }

    return ESP_RMAKER_OK;

fail:
    __automation_trigger_reset(trigger);
    return err;
}

/* Parse ``data`` into ``node->automation``, replacing the existing list.
 * Builds into a temp list and swaps in only on success, so a parse failure
 * leaves the node's live triggers intact. Caller holds the node lock. */
static esp_rmaker_error_t __build_trigger_details_from_binary_locked(const esp_rmaker_node_t *node,
        const uint8_t *blob, size_t blob_len)
{
    node_automation_trigger_state_t *a = __node_auto(node);
    if (!a) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_trigger_iter_t it;
    size_t count = 0;
    if (esp_rmaker_trigger_details_iter_begin(blob, blob_len, &it, &count) != ESP_RMAKER_OK) {
        return ESP_RMAKER_FAIL;
    }

    if (count > RMAKER_TRIGGER_MAX_COUNT) {
        OSAL_LOGE(TAG, "Too many triggers. Max is %d. Found %u.", RMAKER_TRIGGER_MAX_COUNT, (unsigned int)count);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Empty list is valid: clear the node's triggers and we're done. */
    if (count == 0) {
        __trigger_list_free(a);
        return ESP_RMAKER_OK;
    }

    node_automation_trigger_state_t tmp = { .list = NULL, .count = 0 };
    tmp.list = (esp_rmaker_automation_trigger_t *)OSAL_CALLOC_EXTRAM(count, sizeof(esp_rmaker_automation_trigger_t));
    if (!tmp.list) {
        OSAL_LOGE(TAG, "Failed to allocate memory for trigger list.");
        return ESP_RMAKER_NO_MEM;
    }

    int triggers_added = 0;
    bool failed = false;
    esp_rmaker_trigger_entry_t entry;
    esp_rmaker_error_t ierr;
    while ((ierr = esp_rmaker_trigger_details_iter_next(&it, &entry)) == ESP_RMAKER_OK) {
        if (!entry.enabled) {
            continue; /* disabled triggers are not installed */
        }
        if (__parse_trigger_from_entry_locked(node, &entry, &tmp.list[triggers_added]) == ESP_RMAKER_OK) {
            triggers_added++;
        } else {
            failed = true;
            break;
        }
    }
    if (ierr == ESP_RMAKER_FAIL) {
        failed = true; /* corrupt blob mid-stream */
    }

    /* Any failure -> discard temp, leave the node's live list intact. */
    if (failed) {
        OSAL_LOGE(TAG, "Failed to build triggers; keeping previous triggers.");
        tmp.count = triggers_added;
        __trigger_list_free(&tmp);
        return ESP_RMAKER_FAIL;
    }

    /* Success -> swap temp in, freeing the node's old list. */
    __trigger_list_free(a);
    if (triggers_added == 0) {
        /* All disabled - free the (now empty) temp array. */
        free(tmp.list);
        tmp.list = NULL;
    } else {
        a->list = tmp.list;
        a->count = (uint8_t)triggers_added;
        OSAL_LOGI(TAG, "Added %d triggers.", triggers_added);
    }
    return ESP_RMAKER_OK;
}

/* Parse ``data`` (JSON array) into ``node->automation``, replacing the
 * existing list. The blob is the canonical parsed form, so this transcodes
 * JSON->binary exactly once and builds from the binary. On success, the blob
 * is handed back via ``out_blob`` (if provided) so the caller can persist it
 * without a second encode. Caller holds the node lock. */
static esp_rmaker_error_t __build_trigger_details_for_node_locked(const esp_rmaker_node_t *node, const char *data,
        size_t data_len, uint8_t **out_blob, size_t *out_blob_len)
{
    if (out_blob) {
        *out_blob = NULL;
        *out_blob_len = 0;
    }
    if (!__node_auto(node)) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (!data || data_len == 0) {
        OSAL_LOGE(TAG, "Trigger details cannot be NULL or empty.");
        return ESP_RMAKER_INVALID_ARG;
    }

    uint8_t *blob = NULL;
    size_t blob_len = 0;
    esp_rmaker_error_t err = esp_rmaker_trigger_details_encode(data, data_len, &blob, &blob_len);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to encode trigger details (%d)", err);
        return err;
    }
    err = __build_trigger_details_from_binary_locked(node, blob, blob_len);
    if (err == ESP_RMAKER_OK && out_blob) {
        *out_blob = blob;       /* hand ownership to the caller for persistence */
        *out_blob_len = blob_len;
    } else {
        free(blob);
    }
    return err;
}

static esp_rmaker_error_t __persist_trigger_blob(const esp_rmaker_node_t *node, const uint8_t *blob, size_t blob_len)
{
    if (esp_rmaker_node_is_self(node)) {
        return esp_rmaker_local_config_set_trigger_details(blob, blob_len);
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
    return child ? bridge_child_triggers_nvs_set(child, blob, blob_len) : ESP_RMAKER_NOT_FOUND;
#else
    return ESP_RMAKER_NOT_FOUND;
#endif
}

/* Void the persisted trigger version for ``node`` so the cloud's version
 * handshake re-pushes the details. Used when a stored blob can't be decoded
 * (corrupt, or written by an older release that persisted raw JSON). */
static void __invalidate_trigger_version(const esp_rmaker_node_t *node)
{
    if (esp_rmaker_node_is_self(node)) {
        esp_rmaker_local_config_set_trigger_ver(-1);
        return;
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
    if (child) {
        bridge_child_nvs_set_trigger_ver(child, -1);
    }
#endif
}

/* --- Trigger state report (per node) --- */

static void __trigger_state_report_scheduler_task(void *unused)
{
    /* Runtime gate: don't schedule a trigger report while stopping/stopped/resetting. */
    if (!esp_rmaker_should_do_work()) {
        return;
    }
    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(__trigger_state_report_task, NULL);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add trigger state report task to work queue with esp_rmaker_error_t: %d", err);
    }
}

/* Visitor: for one node, if any trigger CHANGED, send a node-scoped report
 * and clear the CHANGED flags. Runs under the node lock; the payload fn reads
 * the node's list without re-locking (this visitor holds the lock). */
static esp_rmaker_error_t __trigger_report_visitor(const esp_rmaker_node_t *node, void *priv)
{
    (void)priv;
    node_automation_trigger_state_t *a = __node_auto(node);
    if (!a) {
        return ESP_RMAKER_OK;
    }

    esp_rmaker_node_lock(node);
    bool any_changed = false;
    for (uint8_t i = 0; i < a->count; i++) {
        if (a->list[i].flags & RMAKER_TRIGGER_FLAG_CHANGED) {
            any_changed = true;
            break;
        }
    }
    if (any_changed) {
        esp_rmaker_notification_t notification = {
            .report_payload_fn = __trigger_state_report_payload_fn,
            .data = (void *)node,
        };
        esp_rmaker_error_t err = esp_rmaker_notify_send_for_node(node, &notification);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to send trigger state report notification: %d", err);
        } else {
            for (uint8_t i = 0; i < a->count; i++) {
                a->list[i].flags &= ~RMAKER_TRIGGER_FLAG_CHANGED;
            }
        }
    }
    esp_rmaker_node_unlock(node);
    return ESP_RMAKER_OK;
}

static void __trigger_state_report_task(void *unused)
{
    (void)unused;
    /* Runtime gate: bail before walking node/trigger memory that reset may be
     * freeing during stop/reset. */
    if (!esp_rmaker_should_do_work()) {
        return;
    }
    /* Walk self + ready children, reporting each node's changed triggers to
     * its own topic. */
    esp_rmaker_node_for_each(__trigger_report_visitor, NULL);
}

/* Payload fn for one node. ``node_arg`` is the node; the report visitor holds
 * the node lock, so the list is read here without re-locking. */
static esp_rmaker_error_t __trigger_state_report_payload_fn(json_gen_str_t *jptr, void *node_arg, bool is_sizing)
{
    (void)is_sizing;
    const esp_rmaker_node_t *node = (const esp_rmaker_node_t *)node_arg;
    node_automation_trigger_state_t *a = __node_auto(node);
    if (!a || !jptr) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;

    if (a->count == 0) {
        return ESP_RMAKER_OK;
    }

    bool at_least_one = false;
    for (uint8_t i = 0; i < a->count; i++) {
        if (a->list[i].flags & RMAKER_TRIGGER_FLAG_CHANGED) {
            at_least_one = true;
            break;
        }
    }
    if (!at_least_one) {
        return ESP_RMAKER_INVALID_STATE;
    }

    if (json_gen_push_object(jptr, "automation") != 0) {
        OSAL_LOGE(TAG, "Failed to push automation object.");
        return ESP_RMAKER_FAIL;
    }
    if (json_gen_push_array(jptr, "trigger") != 0) {
        OSAL_LOGE(TAG, "Failed to push trigger array.");
        json_gen_pop_object(jptr);
        return ESP_RMAKER_FAIL;
    }

    for (uint8_t i = 0; i < a->count; i++) {
        esp_rmaker_automation_trigger_t *trigger = &a->list[i];
        if (trigger->flags & RMAKER_TRIGGER_FLAG_CHANGED) {
            bool value = trigger->flags & RMAKER_TRIGGER_FLAG_MET;
            if (json_gen_start_object(jptr) == 0) {
                if (json_gen_obj_set_string(jptr, "id", trigger->id) != 0) {
                    OSAL_LOGE(TAG, "Failed to set trigger ID.");
                    err = ESP_RMAKER_FAIL;
                    goto __payload_end;
                }
                if (json_gen_obj_set_bool(jptr, "value", value) != 0) {
                    OSAL_LOGE(TAG, "Failed to set trigger value.");
                    err = ESP_RMAKER_FAIL;
                    goto __payload_end;
                }
                json_gen_end_object(jptr);
            }
        }
    }

__payload_end:
    json_gen_pop_array(jptr);
    json_gen_pop_object(jptr);
    return err;
}

static void __update_trigger_details_work_queue_task(void *arg_in)
{
    __update_trigger_details_arg_t *arg = (__update_trigger_details_arg_t *)arg_in;
    if (!arg) {
        return;
    }
    char *data = arg->data;
    const esp_rmaker_node_t *node = arg->node;

    /* Encode JSON->binary once: the build consumes the blob and hands it back
     * so we persist the same bytes without re-encoding. */
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    esp_rmaker_node_lock(node);
    esp_rmaker_error_t err = __build_trigger_details_for_node_locked(node, data, strlen(data), &blob, &blob_len);
    if (err == ESP_RMAKER_OK) {
        esp_rmaker_error_t perr = __persist_trigger_blob(node, blob, blob_len);
        if (perr != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to persist trigger details: %d", perr);
        }
    } else {
        OSAL_LOGE(TAG, "Failed to parse trigger details: %d", err);
    }
    esp_rmaker_node_unlock(node);
    free(blob);

    /* Signal trigger-details completion only after build+persist finishes, so
     * waiters never act on a not-yet-installed trigger list. */
    if (esp_rmaker_node_is_self(node)) {
        esp_rmaker_event_flags_set_trigger_details_received();
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    } else {
        esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
        if (child) {
            bridge_internal_dispatch_child_event(child, BRIDGE_CHILD_EVENT_TRIGGER_DETAILS_RECEIVED);
        }
#endif
    }

    if (data) {
        free(data);
    }
    free(arg);
}

/* Public function definitions *******************************************************/

/* --- Initialization/deinitialization --- */

esp_rmaker_error_t esp_rmaker_automation_service_init(void)
{
    __priv_data = (__automation_priv_data_t *)OSAL_CALLOC_EXTRAM(1, sizeof(__automation_priv_data_t));
    if (!__priv_data) {
        OSAL_LOGE(TAG, "Failed to allocate memory for automation private data.");
        return ESP_RMAKER_NO_MEM;
    }
    __priv_data->mutex = osal_semaphore_create_mutex();
    if (!__priv_data->mutex) {
        OSAL_LOGE(TAG, "Failed to create mutex for automation private data.");
        free(__priv_data);
        __priv_data = NULL;
        return ESP_RMAKER_NO_MEM;
    }
    __priv_data->update_id_trackers = NULL;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __drop_all_visitor(const esp_rmaker_node_t *node, void *priv)
{
    (void)priv;
    esp_rmaker_automation_drop_node(node);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_automation_service_deinit(void)
{
    if (!__priv_data) {
        OSAL_LOGW(TAG, "Automation private data is not initialized.");
        return ESP_RMAKER_OK;
    }
    if (__priv_data->trigger_state_report_task_handle) {
        osal_scheduler_cancel_task(&__priv_data->trigger_state_report_task_handle);
        __priv_data->trigger_state_report_task_handle = NULL;
    }
    /* Free every node's trigger list (self + ready children). */
    esp_rmaker_node_for_each(__drop_all_visitor, NULL);

    if (__priv_data->mutex) {
        osal_semaphore_delete(__priv_data->mutex);
        __priv_data->mutex = NULL;
    }
    free(__priv_data);
    __priv_data = NULL;
    return ESP_RMAKER_OK;
}

/* --- On start --- */

esp_rmaker_error_t esp_rmaker_automation_service_on_start(void)
{
    /* Load the self node's persisted triggers. Child nodes reload when they
     * become ready (see the bridge on-connect path). */
    return esp_rmaker_automation_service_reload_for_node(esp_rmaker_get_node());
}

/* --- Trigger handling --- */

void esp_rmaker_automation_drop_node(const esp_rmaker_node_t *node)
{
    if (!node) {
        return;
    }
    esp_rmaker_node_lock(node);
    __trigger_list_free(__node_auto(node));
    esp_rmaker_node_unlock(node);
}

esp_rmaker_error_t esp_rmaker_automation_service_update_trigger_details(const esp_rmaker_node_t *node, const char *trigger_details)
{
    if (!__priv_data) {
        OSAL_LOGW(TAG, "Automation service is not initialized. Ignoring update trigger details.");
        return ESP_RMAKER_OK;
    }
    if (!node || !trigger_details) {
        return ESP_RMAKER_INVALID_ARG;
    }
    __update_trigger_details_arg_t *arg = (__update_trigger_details_arg_t *)OSAL_CALLOC_EXTRAM(1, sizeof(*arg));
    if (!arg) {
        OSAL_LOGE(TAG, "Failed to allocate trigger details task arg.");
        return ESP_RMAKER_NO_MEM;
    }
    arg->node = node;
    arg->data = OSAL_STRDUP_EXTRAM(trigger_details);
    if (!arg->data) {
        OSAL_LOGE(TAG, "Failed to duplicate trigger details.");
        free(arg);
        return ESP_RMAKER_NO_MEM;
    }
    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(__update_trigger_details_work_queue_task, (void *)arg);
    if (err != ESP_RMAKER_OK) {
        free(arg->data);
        free(arg);
    }
    return err;
}

esp_rmaker_error_t esp_rmaker_automation_service_reload_for_node(const esp_rmaker_node_t *node)
{
    if (!__priv_data) {
        return ESP_RMAKER_OK;
    }
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Read the node's persisted trigger-details blob (compact codec form). */
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    if (esp_rmaker_node_is_self(node)) {
        blob = esp_rmaker_local_config_get_trigger_details(&blob_len);
    } else {
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
        if (child) {
            blob = bridge_child_triggers_nvs_get(child, &blob_len);
        }
#endif
    }
    if (!blob) {
        return ESP_RMAKER_OK; /* nothing stored */
    }

    /* Build the live list straight from the blob (same path the receipt
     * builder ends in). A build failure means the blob is corrupt or was
     * written by an older release that stored raw JSON: void the persisted
     * version so the cloud re-pushes, and treat the boot reload as a no-op
     * (not fatal). */
    esp_rmaker_node_lock(node);
    esp_rmaker_error_t err = __build_trigger_details_from_binary_locked(node, blob, blob_len);
    esp_rmaker_node_unlock(node);
    free(blob);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Unreadable persisted trigger blob (%d); voiding version to refetch", err);
        __invalidate_trigger_version(node);
        return ESP_RMAKER_OK;
    }

    /* Mirror ::__update_trigger_details_work_queue_task: signal child-side
     * tests / observers that the reload completed. The on-connect task
     * dispatches this so ``BridgeChildRemote.wait_on_trigger_details`` can
     * gate on NVS-rehydrated triggers becoming live. */
    if (err == ESP_RMAKER_OK && !esp_rmaker_node_is_self(node)) {
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
        if (child) {
            bridge_internal_dispatch_child_event(child, BRIDGE_CHILD_EVENT_TRIGGER_DETAILS_RECEIVED);
        }
#endif
    }
    return err;
}

esp_rmaker_error_t esp_rmaker_automation_service_update_id_check_and_fire(const esp_rmaker_state_update_id_t update_id, esp_rmaker_param_val_t val)
{
    if (!update_id) {
        OSAL_LOGE(TAG, "Update ID cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    if (!__priv_data) {
        return ESP_RMAKER_OK;
    }

    /* Resolve the owning node (NULL -> self) and scan only its triggers. */
    const esp_rmaker_node_t *node = data_model_state_update_id_to_node(update_id);
    if (!node) {
        node = esp_rmaker_get_node();
    }
    node_automation_trigger_state_t *a = __node_auto(node);
    if (!a) {
        return ESP_RMAKER_INVALID_STATE;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    bool at_least_one_trigger_fired = false;

    esp_rmaker_node_lock(node);
    for (uint8_t i = 0; i < a->count; i++) {
        esp_rmaker_automation_trigger_t *trigger = &a->list[i];
        if (data_model_state_update_id_compare(trigger->update_id, update_id) != 0) {
            continue;
        }
        OSAL_LOGI(TAG, "Checking trigger: %s", trigger->id);
        esp_rmaker_error_t check_err = esp_rmaker_val_compare(&val, &trigger->expected_val, trigger->compare_op);
        if (check_err == ESP_RMAKER_OK) {
            trigger->flags |= RMAKER_TRIGGER_FLAG_MET;
        } else if (check_err == ESP_RMAKER_FAIL) {
            trigger->flags &= ~RMAKER_TRIGGER_FLAG_MET;
        } else {
            OSAL_LOGE(TAG, "Failed to compare values.");
            err = ESP_RMAKER_FAIL;
            break;
        }
        trigger->flags |= RMAKER_TRIGGER_FLAG_CHANGED;
        at_least_one_trigger_fired = true;
    }
    esp_rmaker_node_unlock(node);

    /* Schedule the (global) trigger-state report if anything fired. The
     * scheduler handle is guarded by the service mutex, taken outside the
     * node lock. */
    if (at_least_one_trigger_fired) {
        __automation_sched_lock();
        osal_err_t report_err;
        if (__priv_data->trigger_state_report_task_handle) {
            report_err = osal_scheduler_reset_timer(__priv_data->trigger_state_report_task_handle, AUTOMATION_TRIGGER_STATE_REPORT_DELAY_MS);
        } else {
            report_err = osal_scheduler_schedule_task(&__priv_data->trigger_state_report_task_handle, AUTOMATION_TRIGGER_STATE_REPORT_DELAY_MS, __trigger_state_report_scheduler_task, NULL);
        }
        __automation_sched_unlock();
        if (report_err != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to schedule trigger state report.");
            err = ESP_RMAKER_FAIL;
        } else {
            OSAL_LOGI(TAG, "Scheduled trigger state report task.");
        }
    }

    return err;
}
