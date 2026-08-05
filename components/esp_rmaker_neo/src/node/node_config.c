/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file node_config.c
 * @brief Node configuration functions.
 */

/* Declarations */
#include "data_model_internal.h"
#include "node_internal.h"
#include "esp_rmaker_node.h"
#include "constants/identity.h"

/* Value includes */
#include "esp_rmaker_val.h"

/* Error types */
#include "esp_rmaker_error_types.h"

/* Standard C headers */
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* JSON-common includes */
#include "json_generator.h"

/* Logging includes */
#include "osal_time.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Utilities includes */
#include "util/esp_rmaker_convert_hex.h"

/* Checksum includes */
#include "checksum_impl.h"

/* NVS includes */
#include "osal_storage.h"
#include "constants/nvs.h"

/* Cloud includes */
#include "network/cloud/manager.h"
#include "network/mqtt_channels.h"
#include "network/mqtt_topics.h"
#include "network/state_changes.h"

/* Local config includes */
#include "local_config.h"

/* Event flags includes */
#include "event_flags.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "bridge/bridge_internal.h"
#include "bridge/bridge_child_nvs.h"
#include "node_config_pending.h"
#endif

/* Global variables ******************************************************************/

/* Tag for logging */
static const char *TAG = "rmng_node_config";

/* Static function definitions *******************************************************/

static esp_rmaker_error_t esp_rmaker_report_info(const esp_rmaker_node_t *node, json_gen_str_t *jptr)
{
    if (!jptr) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_node_info_t *info = esp_rmaker_node_get_info(node);
    json_gen_push_object(jptr, "info");
    json_gen_obj_set_string(jptr, RMAKER_INFO_KEY_NAME,  info->name);
    json_gen_obj_set_string(jptr, RMAKER_INFO_KEY_FW_VERSION,  info->fw_version);
    json_gen_obj_set_string(jptr, RMAKER_INFO_KEY_TYPE,  info->type);
    json_gen_obj_set_string(jptr, RMAKER_INFO_KEY_MODEL,  info->model);
    json_gen_pop_object(jptr);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t esp_rmaker_report_node_attributes(const esp_rmaker_node_t *node, json_gen_str_t *jptr)
{
    if (!jptr) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_attr_t *attr = esp_rmaker_node_get_first_attribute(node);
    if (!attr) {
        return ESP_RMAKER_OK;
    }
    json_gen_push_array(jptr, "attributes");
    while (attr) {
        esp_rmaker_report_attribute(attr, jptr);
        attr = attr->next;
    }
    json_gen_pop_array(jptr);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_report_attribute(const esp_rmaker_attr_t *attr, json_gen_str_t *jptr)
{
    if (!attr || !jptr) {
        return ESP_RMAKER_INVALID_ARG;
    }
    json_gen_start_object(jptr);
    json_gen_obj_set_string(jptr, "name", attr->name);
    json_gen_obj_set_string(jptr, "value", attr->value);
    json_gen_end_object(jptr);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_report_value(const esp_rmaker_param_val_t *val, char *key, json_gen_str_t *jptr)
{
    if (!key || !jptr) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (!val) {
        json_gen_obj_set_null(jptr, key);
        return ESP_RMAKER_OK;
    }
    switch (val->type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        json_gen_obj_set_bool(jptr, key, val->val.b);
        break;
    case RMAKER_VAL_TYPE_INTEGER:
        json_gen_obj_set_int(jptr, key, val->val.i);
        break;
    case RMAKER_VAL_TYPE_FLOAT:
        json_gen_obj_set_float(jptr, key, val->val.f);
        break;
    case RMAKER_VAL_TYPE_STRING:
        json_gen_obj_set_string(jptr, key, val->val.s);
        break;
    case RMAKER_VAL_TYPE_OBJECT:
        json_gen_push_object_str(jptr, key, val->val.s);
        break;
    case RMAKER_VAL_TYPE_ARRAY:
        json_gen_push_array_str(jptr, key, val->val.s);
        break;
    default:
        break;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_report_data_type(esp_rmaker_val_type_t type, char *data_type_key, json_gen_str_t *jptr)
{
    switch (type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        json_gen_obj_set_string(jptr, data_type_key, "bool");
        break;
    case RMAKER_VAL_TYPE_INTEGER:
        json_gen_obj_set_string(jptr, data_type_key, "int");
        break;
    case RMAKER_VAL_TYPE_FLOAT:
        json_gen_obj_set_string(jptr, data_type_key, "float");
        break;
    case RMAKER_VAL_TYPE_STRING:
        json_gen_obj_set_string(jptr, data_type_key, "string");
        break;
    case RMAKER_VAL_TYPE_OBJECT:
        json_gen_obj_set_string(jptr, data_type_key, "object");
        break;
    case RMAKER_VAL_TYPE_ARRAY:
        json_gen_obj_set_string(jptr, data_type_key, "array");
        break;
    default:
        json_gen_obj_set_string(jptr, data_type_key, "invalid");
        break;
    }
    return ESP_RMAKER_OK;
}

/**
 * @brief Resolve the node_id string to emit for a given node.
 *
 * Self node -> cloud-assigned thing name from credentials store (malloc'd).
 * Child node -> strdup of the child's thing_name from the bridge slot.
 * Caller frees on success.
 */
static esp_rmaker_error_t __resolve_node_id(const esp_rmaker_node_t *node, char **out_node_id)
{
    *out_node_id = NULL;
    if (esp_rmaker_node_is_self(node)) {
        return esp_rmaker_credentials_get_thing_name(out_node_id);
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
    if (!child || !child->thing_name) {
        OSAL_LOGE(TAG, "Child node has no thing_name (not yet ack'd)");
        return ESP_RMAKER_INVALID_STATE;
    }
    *out_node_id = OSAL_STRDUP_EXTRAM(child->thing_name);
    return (*out_node_id) ? ESP_RMAKER_OK : ESP_RMAKER_NO_MEM;
#else
    (void)node;
    return ESP_RMAKER_INVALID_ARG;
#endif
}

static int __esp_rmaker_get_node_config(const esp_rmaker_node_t *node, char *buf, size_t buf_size)
{
    int ret;
    char *node_id = NULL;
    const char *data_model_type = data_model_node_get_data_model_type();
    if (!data_model_type) {
        OSAL_LOGE(TAG, "Data model type not set");
        return -1;
    }
    if (!node) {
        OSAL_LOGE(TAG, "Node cannot be NULL");
        return -1;
    }
    if (!esp_rmaker_node_get_info(node)) {
        OSAL_LOGW(TAG, "Node info not filled yet; skipping node-config report");
        return -1;
    }

    esp_rmaker_error_t err = __resolve_node_id(node, &node_id);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to resolve node ID");
        return -1;
    }

    json_gen_str_t jstr;
    json_gen_str_start(&jstr, buf, buf_size, NULL, NULL);
    json_gen_start_object(&jstr);
    json_gen_obj_set_string(&jstr, "node_id", node_id);
    if (json_gen_push_object(&jstr, "config") != 0) {
        ret = -1;
        goto __esp_rmaker_get_node_config_end;
    }
    json_gen_obj_set_string(&jstr, "data_model", data_model_type);
    esp_rmaker_report_info(node, &jstr);
    esp_rmaker_report_node_attributes(node, &jstr);
    err = data_model_node_write_data_model_config(node, &jstr);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to write data model config");
        ret = -1;
        goto __esp_rmaker_get_node_config_end;
    }
    if (json_gen_pop_object(&jstr) != 0) {
        ret = -1;
        goto __esp_rmaker_get_node_config_end;
    }
    if (json_gen_end_object(&jstr) != 0) {
        ret = -1;
        goto __esp_rmaker_get_node_config_end;
    }

    ret = json_gen_str_end(&jstr);

__esp_rmaker_get_node_config_end:
    free(node_id);
    return ret;
}

static char *__get_node_config(const esp_rmaker_node_t *node)
{
    if (!node) {
        OSAL_LOGE(TAG, "Node cannot be NULL");
        return NULL;
    }
    /* Two-pass: NULL/0 to size, then alloc and emit. */
    int req_size = __esp_rmaker_get_node_config(node, NULL, 0);
    if (req_size < 0) {
        OSAL_LOGE(TAG, "Failed to get required size for Node config JSON.");
        return NULL;
    }
    char *node_config = OSAL_CALLOC_EXTRAM(1, req_size);
    if (!node_config) {
        OSAL_LOGE(TAG, "Failed to allocate %d bytes for node config", req_size);
        return NULL;
    }
    if (__esp_rmaker_get_node_config(node, node_config, req_size) < 0) {
        free(node_config);
        OSAL_LOGE(TAG, "Failed to generate Node config JSON.");
        return NULL;
    }
    OSAL_LOGI(TAG, "Generated Node config of length %d", req_size);
    return node_config;
}

char *esp_rmaker_get_node_config(void)
{
    return __get_node_config(esp_rmaker_get_node());
}

/**
 * @brief Ack-callback context for a node-config publish.
 *
 * Carries the ctx that was published for + the checksum that was sent,
 * so on ack we can (a) persist the right checksum/ncfg_ver under the
 * right namespace, and (b) recompute the *current* per-ctx checksum
 * to detect stale acks (devices mutated mid-flight). On a stale ack
 * we leave the pending entry in the pending list - the retry context
 * will republish on its next tick.
 */
typedef struct {
    const esp_rmaker_node_t *node;
    uint8_t hash[RMAKER_CHECKSUM_LEN];
} __report_node_config_priv_t;

static void __persist_and_mark_ncfg_ver(const esp_rmaker_node_t *node, const uint8_t *hash_new)
{
    if (esp_rmaker_node_is_self(node)) {
        esp_rmaker_error_t err = esp_rmaker_checksum_store(hash_new, RMAKER_NVS_CHECKSUM_KEY_NODE_CONFIG);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to store self checksum in NVS");
        } else {
            OSAL_LOGI(TAG, "Self Node Configuration checksum stored in NVS.");
        }
    } else {
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
        if (child) {
            esp_rmaker_error_t err = bridge_child_nvs_set_node_config(child, hash_new);
            if (err != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to store child checksum in NVS");
            }
        }
#endif
    }

    /* ncfg_ver *is* the node-config checksum (a change-token that needs no
     * wall clock); piggyback it onto the next state report for this node. */
    (void)esp_rmaker_state_mark_for_update_ncfg_ver_for_node(node, hash_new);
}

static void __report_node_config_cb(esp_rmaker_cloud_event_set_response_t *p_response, void *priv_data)
{
    __report_node_config_priv_t *priv = (__report_node_config_priv_t *)priv_data;
    const esp_rmaker_node_t *node = priv->node;

    if (!p_response->success) {
        OSAL_LOGE(TAG, "Cloud reported node configuration update failed: %s",
                  p_response->error_message ? p_response->error_message : "Unknown error");
        /* Leave the entry in the pending list with inflight cleared;
         * the retry context will refire. */
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        node_config_pending_clear_inflight(node);
#endif
        free(priv);
        return;
    }

    char __tname[RMAKER_THING_NAME_BUFFER_SIZE];
    esp_rmaker_node_resolve_thing_name(node, __tname, sizeof(__tname));
    OSAL_LOGI(TAG, "Node Configuration reported to cloud (thing=%s)", __tname);

    /* Stale-ack recompute: rebuild the current per-node JSON, hash it,
     * and compare with the hash we shipped. If they differ, devices
     * mutated mid-flight - keep the entry in the pending list so the
     * retry context republishes the fresh blob, and DO NOT persist the
     * stale checksum/ncfg_ver. */
    char *current_cfg = __get_node_config(node);
    if (current_cfg) {
        uint8_t hash_current[RMAKER_CHECKSUM_LEN];
        esp_rmaker_error_t err = esp_rmaker_checksum_generate((const uint8_t *)current_cfg, strlen(current_cfg), hash_current);
        free(current_cfg);
        if (err == ESP_RMAKER_OK && memcmp(hash_current, priv->hash, RMAKER_CHECKSUM_LEN) != 0) {
            OSAL_LOGW(TAG, "Node config mutated mid-publish; republishing on next retry tick.");
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
            node_config_pending_clear_inflight(node);
#endif
            free(priv);
            return;
        }
    }

    /* Clean ack: persist + drop from pending. */
    __persist_and_mark_ncfg_ver(node, priv->hash);
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    node_config_pending_remove(node);
#endif

    if (esp_rmaker_node_is_self(node)) {
        esp_rmaker_event_flags_set_node_config_sent();
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    else {
        esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
        if (child) {
            bridge_internal_dispatch_child_event(child, BRIDGE_CHILD_EVENT_NODE_CONFIG_SENT);
        }
    }
#endif
    free(priv);
}

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
/**
 * @brief Compare the freshly-computed checksum against the per-child
 *        stored checksum in the bridge NVS record.
 *
 * Sets ``*p_changed`` to true on first-time publish (no record / unset
 * checksum) or when the bytes differ. Returns OK on success.
 */
static esp_rmaker_error_t __child_checksum_changed(esp_rmaker_bridge_child_handle_t child,
        const uint8_t hash_new[RMAKER_CHECKSUM_LEN],
        bool *p_changed)
{
    bridge_child_nvs_record_t rec;
    esp_rmaker_error_t err = bridge_child_nvs_load(child, &rec);
    if (err == ESP_RMAKER_NOT_FOUND) {
        *p_changed = true;
        return ESP_RMAKER_OK;
    }
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (!rec.ncfg_checksum_set) {
        *p_changed = true;
        return ESP_RMAKER_OK;
    }
    *p_changed = (memcmp(rec.ncfg_checksum, hash_new, RMAKER_CHECKSUM_LEN) != 0);
    return ESP_RMAKER_OK;
}
#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

/**
 * @brief Common per-node report path: build JSON, hash, dedup-check, publish.
 *
 * Returns:
 *  - ESP_RMAKER_OK on send-issued or on no-change-skip.
 *  - error otherwise.
 */
esp_rmaker_error_t esp_rmaker_internal_report_node_config_for_node(const esp_rmaker_node_t *node)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }

    char *node_config = __get_node_config(node);
    if (!node_config) {
        OSAL_LOGE(TAG, "Could not get node configuration for reporting to cloud");
        return ESP_RMAKER_FAIL;
    }

    uint8_t hash_new[RMAKER_CHECKSUM_LEN];
    esp_rmaker_error_t err = esp_rmaker_checksum_generate((const uint8_t *)node_config, strlen(node_config), hash_new);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to generate SHA-256 hash of node configuration");
        free(node_config);
        return err;
    }

    /* Per-node checksum dedup. */
    bool changed = true;
    if (esp_rmaker_node_is_self(node)) {
        esp_rmaker_checksum_status_t cs = esp_rmaker_checksum_compare(hash_new, RMAKER_NVS_CHECKSUM_KEY_NODE_CONFIG);
        if (cs == RMAKER_CHECKSUM_NOT_CHANGED) {
            changed = false;
        } else if (cs != RMAKER_CHECKSUM_CHANGED) {
            OSAL_LOGE(TAG, "Failed to compare self node config checksum");
            free(node_config);
            return ESP_RMAKER_FAIL;
        }
    } else {
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
        if (!child) {
            free(node_config);
            return ESP_RMAKER_INVALID_ARG;
        }
        err = __child_checksum_changed(child, hash_new, &changed);
        if (err != ESP_RMAKER_OK) {
            free(node_config);
            return err;
        }
#else
        free(node_config);
        return ESP_RMAKER_INVALID_ARG;
#endif
    }

    if (!changed) {
        char __tname_unch[RMAKER_THING_NAME_BUFFER_SIZE];
        esp_rmaker_node_resolve_thing_name(node, __tname_unch, sizeof(__tname_unch));
        OSAL_LOGI(TAG, "Node Configuration unchanged for '%s'; skipping publish.", __tname_unch);
        free(node_config);
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        /* Already in-sync: drop from pending list. */
        node_config_pending_remove(node);
#endif
        return ESP_RMAKER_OK;
    }

    if (!esp_rmaker_cloud_manager_is_listening()) {
        OSAL_LOGE(TAG, "Cloud manager not listening; cannot publish node config now");
        free(node_config);
        return ESP_RMAKER_INVALID_STATE;
    }

    __report_node_config_priv_t *priv = OSAL_CALLOC_EXTRAM(1, sizeof(*priv));
    esp_rmaker_cloud_event_set_response_cb_context_t *cbctx = OSAL_CALLOC_EXTRAM(1, sizeof(*cbctx));
    if (!priv || !cbctx) {
        OSAL_LOGE(TAG, "Alloc failure for node config publish priv");
        free(priv); free(cbctx); free(node_config);
        return ESP_RMAKER_NO_MEM;
    }
    priv->node = node;
    memcpy(priv->hash, hash_new, RMAKER_CHECKSUM_LEN);
    cbctx->cb = __report_node_config_cb;
    cbctx->priv_data = priv;

    char __tname_rep[RMAKER_THING_NAME_BUFFER_SIZE];
    esp_rmaker_node_resolve_thing_name(node, __tname_rep, sizeof(__tname_rep));
    OSAL_LOGD(TAG, "Reporting Node Configuration (thing=%s):\n%s", __tname_rep, node_config);

    esp_rmaker_cloud_event_t event;
    esp_rmaker_cloud_event_setNodeConfig(&event, node_config, cbctx);
    esp_rmaker_error_t ret = esp_rmaker_cloud_manager_send(esp_rmaker_node_topic_ctx(node), &event, 1, MQTT_CHANNEL_SUB_CLOUD_MANAGER_REPORT_NODE_CONFIG);
    if (ret != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to report Node Configuration to cloud");
        free(priv); free(cbctx); free(node_config);
        return ret;
    }
    free(node_config);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_report_node_config(void)
{
    return esp_rmaker_internal_report_node_config_for_node(esp_rmaker_get_node());
}

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
esp_rmaker_error_t esp_rmaker_report_node_config_for_child(esp_rmaker_bridge_child_handle_t child)
{
    return esp_rmaker_internal_report_node_config_for_node(bridge_internal_child_node(child));
}
#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
