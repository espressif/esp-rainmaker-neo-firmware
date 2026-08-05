/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file node_core.c
 * @brief Node model functions.
 */

/* Declarations */
#include "node_internal.h"
#include "esp_rmaker_node.h"

/* Network includes */
#include "network/state_changes.h"
#include "network/mqtt_topics.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "node_config_pending.h"
#endif

/* Services */
#include "services/automation.h"
#include "services/schedules.h"

/* Platform common headers */
#include "osal_log.h"
#include "osal_sysinfo.h"
#include "osal_mem_alloc.h"
#include "osal_semaphore.h"

/* Standard C headers */
#include <stdbool.h>
#include <string.h>

/* Configuration includes */
#include "sdkconfig.h"

/* Global variables ******************************************************************/

/* Tag for logging */
static const char *TAG = "rmng_node_core";

/* Flag to check if the self node has been created. Children use
 * ``_esp_rmaker_node_init`` (in-place) and bypass this guard. */
static bool node_created = false;

/* Static function declarations *******************************************************/

static void esp_rmaker_node_info_free(esp_rmaker_node_info_t *info);

static bool __esp_rmaker_node_is_tag_name_reserved(const char *tag_name);

static esp_rmaker_error_t __esp_rmaker_node_add_tag(const esp_rmaker_node_t *node, const char *tag_name, const char *tag_value, bool report_to_cloud);

/* Static function definitions *******************************************************/

static void esp_rmaker_node_info_free(esp_rmaker_node_info_t *info)
{
    if (info) {
        if (info->name) {
            free(info->name);
        }
        if (info->type) {
            free(info->type);
        }
        if (info->fw_version) {
            free(info->fw_version);
        }
        if (info->model) {
            free(info->model);
        }
        // if (info->subtype) {
        //     free(info->subtype);
        // }
        // if (info->secure_boot_digest) {
        //     esp_rmaker_secure_boot_digest_free(info->secure_boot_digest);
        //     info->secure_boot_digest = NULL;
        // }
        free(info);
    }
}

static bool __esp_rmaker_node_is_tag_name_reserved(const char *tag_name)
{
    if (!tag_name) {
        return false;
    }

    /* Check if the tag name is reserved */
    return strcmp(tag_name, RMAKER_INFO_KEY_NAME) == 0 ||
           strcmp(tag_name, RMAKER_INFO_KEY_TYPE) == 0 ||
           strcmp(tag_name, RMAKER_INFO_KEY_FW_VERSION) == 0 ||
           strcmp(tag_name, RMAKER_INFO_KEY_MODEL) == 0;
}

static esp_rmaker_error_t __esp_rmaker_node_add_tag(const esp_rmaker_node_t *node, const char *tag_name, const char *tag_value, bool report_to_cloud)
{
    if (!node || !tag_name || !tag_value) {
        OSAL_LOGE(TAG, "Node handle, tag name or value cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_tag_t *tag = ((_esp_rmaker_node_t *)node)->tags, *prev_tag = NULL;
    while (tag) {
        if (strcmp(tag->name, tag_name) == 0) {
            esp_rmaker_node_lock(node);

            if (strcmp(tag->value, tag_value) == 0) {
                /* Tag value is the same. No update required. */
                esp_rmaker_node_unlock(node);
                return ESP_RMAKER_OK;
            }

            /* Overwrite the tag value */
            if (tag->value) {
                free(tag->value);
            }
            tag->value = OSAL_STRDUP_EXTRAM(tag_value);
            if (!tag->value) {
                OSAL_LOGE(TAG, "Failed to allocate memory for value for tag '%s'.", tag_name);
                esp_rmaker_node_unlock(node);
                return ESP_RMAKER_NO_MEM;
            }
            if (report_to_cloud) {
                tag->flags |= RMAKER_SIGNAL_FLAG_VALUE_CHANGE;
            }
            esp_rmaker_node_unlock(node);
            OSAL_LOGI(TAG, "Node tag '%s': '%s' updated", tag_name, tag_value);
            if (report_to_cloud) {
                esp_rmaker_state_schedule_report(false);
            }
            return ESP_RMAKER_OK;
        }
        prev_tag = tag;
        tag = tag->next;
    }

    tag = (esp_rmaker_tag_t *)OSAL_CALLOC_EXTRAM(1, sizeof(esp_rmaker_tag_t));
    if (!tag) {
        OSAL_LOGE(TAG, "Failed to create node tag '%s'.", tag_name);
        return ESP_RMAKER_NO_MEM;
    }
    tag->name = OSAL_STRDUP_EXTRAM(tag_name);
    tag->value = OSAL_STRDUP_EXTRAM(tag_value);
    if (!tag->name || !tag->value) {
        OSAL_LOGE(TAG, "Failed to allocate memory for name/value for tag '%s'.", tag_name);
        esp_rmaker_tag_delete(tag);
        return ESP_RMAKER_NO_MEM;
    }
    if (report_to_cloud) {
        tag->flags |= RMAKER_SIGNAL_FLAG_VALUE_CHANGE;
    }
    esp_rmaker_node_lock(node);
    if (prev_tag) {
        prev_tag->next = tag;
    } else {
        ((_esp_rmaker_node_t *)node)->tags = tag;
    }
    esp_rmaker_node_unlock(node);
    OSAL_LOGI(TAG, "Node tag '%s': '%s' created", tag_name, tag_value);
    if (report_to_cloud) {
        esp_rmaker_state_schedule_report(false);
    }
    return ESP_RMAKER_OK;
}

/* Public function definitions *******************************************************/

esp_rmaker_error_t esp_rmaker_node_lock(const esp_rmaker_node_t *node)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    osal_semaphore_handle_t lock = ((_esp_rmaker_node_t *)node)->lock;
    if (!lock) {
        OSAL_LOGE(TAG, "Node has no lock.");
        return ESP_RMAKER_INVALID_STATE;
    }
    if (osal_semaphore_take(lock, OSAL_MAX_DELAY) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to lock node.");
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_node_unlock(const esp_rmaker_node_t *node)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    osal_semaphore_handle_t lock = ((_esp_rmaker_node_t *)node)->lock;
    if (!lock) {
        return ESP_RMAKER_INVALID_STATE;
    }
    if (osal_semaphore_give(lock) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to unlock node.");
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

void _esp_rmaker_node_init(_esp_rmaker_node_t *node)
{
    if (!node) {
        return;
    }
    /* Preserve the per-node lock across in-place re-init (reused child
     * slots keep their mutex); create one if this is first-time init. */
    osal_semaphore_handle_t lock = node->lock;
    memset(node, 0, sizeof(*node));
    if (!lock) {
        lock = osal_semaphore_create_mutex();
        if (!lock) {
            OSAL_LOGE(TAG, "Failed to create node lock.");
        }
    }
    node->lock = lock;
    /* Default to self ops; bridge slot init overwrites priv/valid/ops
     * before publishing the child node. */
    node->topic_ctx.ops = &esp_rmaker_topic_ops_self;
    node->topic_ctx.priv = NULL;
    node->topic_ctx.valid = NULL;

    /* Initialize data-model specific states */
    node->devices = NULL;
}

void _esp_rmaker_node_reset(_esp_rmaker_node_t *node)
{
    if (!node) {
        return;
    }
    /* Flush per-manager embedded state before we tear down node-owned
     * heap (devices/attrs/tags/info). Safe to call on a node that was
     * never touched by these managers - both are idempotent. */
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    node_config_pending_remove((const esp_rmaker_node_t *)node);
#endif
    esp_rmaker_state_drop_node((const esp_rmaker_node_t *)node);
    esp_rmaker_automation_drop_node((const esp_rmaker_node_t *)node);
    esp_rmaker_schedule_service_unload_node((const esp_rmaker_node_t *)node);

    esp_rmaker_attr_t *attr = node->attributes;
    while (attr) {
        esp_rmaker_attr_t *next_attr = attr->next;
        esp_rmaker_attribute_delete(attr);
        attr = next_attr;
    }
    node->attributes = NULL;

    _esp_rmaker_device_t *device = node->devices;
    while (device) {
        _esp_rmaker_device_t *next_device = device->next;
        device->parent = NULL;
        esp_rmaker_device_delete((esp_rmaker_device_t *)device);
        device = next_device;
    }
    node->devices = NULL;
    esp_rmaker_tag_t *tag = node->tags;
    while (tag) {
        esp_rmaker_tag_t *next_tag = tag->next;
        esp_rmaker_tag_delete(tag);
        tag = next_tag;
    }
    node->tags = NULL;

    if (node->info) {
        esp_rmaker_node_info_free(node->info);
        node->info = NULL;
    }
}

esp_rmaker_error_t esp_rmaker_node_fill_with_info(const esp_rmaker_node_t *node, const esp_rmaker_node_info_t *info)
{
    if (!node || !info) {
        OSAL_LOGE(TAG, "Node or info cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    if (!info->name || !info->type || !info->fw_version || !info->model) {
        OSAL_LOGE(TAG, "Node info must have name, type, fw_version and model set.");
        return ESP_RMAKER_INVALID_ARG;
    }
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    if (_node->info) {
        OSAL_LOGE(TAG, "Node already filled with info.");
        return ESP_RMAKER_INVALID_STATE;
    }
    _node->info = (esp_rmaker_node_info_t *)OSAL_CALLOC_EXTRAM(1, sizeof(esp_rmaker_node_info_t));
    if (!_node->info) {
        return ESP_RMAKER_NO_MEM;
    }
    _node->info->name = OSAL_STRDUP_EXTRAM(info->name);
    _node->info->type = OSAL_STRDUP_EXTRAM(info->type);
    _node->info->fw_version = OSAL_STRDUP_EXTRAM(info->fw_version);
    _node->info->model = OSAL_STRDUP_EXTRAM(info->model);
    if (!_node->info->name || !_node->info->type || !_node->info->fw_version || !_node->info->model) {
        OSAL_LOGE(TAG, "Failed to dup node info fields.");
        esp_rmaker_node_info_free(_node->info);
        _node->info = NULL;
        return ESP_RMAKER_NO_MEM;
    }

    /* Auto-add the four reserved tags mirroring the info fields. */
    esp_rmaker_error_t err = __esp_rmaker_node_add_tag(node, RMAKER_INFO_KEY_NAME, info->name, false);
    if (err == ESP_RMAKER_OK) {
        err = __esp_rmaker_node_add_tag(node, RMAKER_INFO_KEY_TYPE, info->type, false);
    }
    if (err == ESP_RMAKER_OK) {
        err = __esp_rmaker_node_add_tag(node, RMAKER_INFO_KEY_FW_VERSION, info->fw_version, false);
    }
    if (err == ESP_RMAKER_OK) {
        err = __esp_rmaker_node_add_tag(node, RMAKER_INFO_KEY_MODEL, info->model, false);
    }
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to mirror node info into tags");
        return err;
    }
    OSAL_LOGI(TAG, "Node filled: Name - %s, Type - %s, Firmware Version - %s, Model - %s",
              info->name, info->type, info->fw_version, info->model);
    return ESP_RMAKER_OK;
}

esp_rmaker_node_t *esp_rmaker_node_create(const char *name, const char *type)
{
    if (node_created) {
        OSAL_LOGE(TAG, "Self node already created.");
        return NULL;
    }
    if (!name || !type) {
        OSAL_LOGE(TAG, "Node Name and Type are mandatory.");
        return NULL;
    }
    _esp_rmaker_node_t *node = (_esp_rmaker_node_t *)OSAL_CALLOC_EXTRAM(1, sizeof(_esp_rmaker_node_t));
    if (!node) {
        OSAL_LOGE(TAG, "Failed to allocate memory for node.");
        return NULL;
    }
    _esp_rmaker_node_init(node);

    const char *fw_version = osal_sysinfo_get_fw_version();
    const char *project_name = osal_sysinfo_get_project_name();
    if (!fw_version || !project_name) {
        OSAL_LOGE(TAG, "Platform fw_version / project_name not available.");
        free(node);
        return NULL;
    }
    esp_rmaker_node_info_t info = {
        .name = (char *)name,
        .type = (char *)type,
        .fw_version = (char *)fw_version,
        .model = (char *)project_name,
    };
    esp_rmaker_error_t err = esp_rmaker_node_fill_with_info((esp_rmaker_node_t *)node, &info);
    if (err != ESP_RMAKER_OK) {
        _esp_rmaker_node_reset(node);
        free(node);
        return NULL;
    }
    node_created = true;
    return (esp_rmaker_node_t *)node;
}

esp_rmaker_error_t esp_rmaker_node_delete(const esp_rmaker_node_t *node)
{
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    if (!_node) {
        return ESP_RMAKER_INVALID_ARG;
    }

    _esp_rmaker_node_reset(_node);
    if (_node->lock) {
        osal_semaphore_delete(_node->lock);
        _node->lock = NULL;
    }
    free(_node);
    node_created = false;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_node_set_online_for_node(const esp_rmaker_node_t *node, bool online)
{
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    if (!_node) {
        OSAL_LOGE(TAG, "Node handle cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_node_lock(node);
    if (online) {
        _node->status_flags |= RMAKER_NODE_STATUS_FLAG_ONLINE;
    } else {
        _node->status_flags &= ~RMAKER_NODE_STATUS_FLAG_ONLINE;
    }
    esp_rmaker_node_unlock(node);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_node_set_online(bool online)
{
    return esp_rmaker_node_set_online_for_node(esp_rmaker_get_node(), online);
}

bool esp_rmaker_node_is_online(const esp_rmaker_node_t *node)
{
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    if (!_node) {
        return false;
    }
    esp_rmaker_node_lock(node);
    bool online = (_node->status_flags & RMAKER_NODE_STATUS_FLAG_ONLINE) != 0;
    esp_rmaker_node_unlock(node);
    return online;
}

esp_rmaker_error_t esp_rmaker_node_add_attribute(const esp_rmaker_node_t *node, const char *attr_name, const char *value)
{
    if (!node || !attr_name || !value) {
        OSAL_LOGE(TAG, "Node handle, attribute name or value cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_attr_t *attr = ((_esp_rmaker_node_t *)node)->attributes, *prev_attr = NULL;
    while (attr) {
        if (strcmp(attr->name, attr_name) == 0) {
            OSAL_LOGE(TAG, "Node attribute with name %s already exists.", attr_name);
            return ESP_RMAKER_INVALID_ARG;
        }
        prev_attr = attr;
        attr = attr->next;
    }
    attr = (esp_rmaker_attr_t *)OSAL_CALLOC_EXTRAM(1, sizeof(esp_rmaker_attr_t));
    if (!attr) {
        OSAL_LOGE(TAG, "Failed to create node attribute %s.", attr_name);
        return ESP_RMAKER_NO_MEM;
    }
    attr->name = OSAL_STRDUP_EXTRAM(attr_name);
    attr->value = OSAL_STRDUP_EXTRAM(value);
    if (!attr->name || !attr->value) {
        OSAL_LOGE(TAG, "Failed to allocate memory for name/value for attribute %s.", attr_name);
        esp_rmaker_attribute_delete(attr);
        return ESP_RMAKER_NO_MEM;
    }

    if (prev_attr) {
        prev_attr->next = attr;
    } else {
        ((_esp_rmaker_node_t *)node)->attributes = attr;
    }
    OSAL_LOGI(TAG, "Node attribute %s created", attr_name);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_node_add_tag(const esp_rmaker_node_t *node, const char *tag_name, const char *tag_value)
{
    if (__esp_rmaker_node_is_tag_name_reserved(tag_name)) {
        OSAL_LOGE(TAG, "Tag name %s is reserved and cannot be used.", tag_name);
        return ESP_RMAKER_INVALID_ARG;
    }
    return __esp_rmaker_node_add_tag(node, tag_name, tag_value, false);
}

esp_rmaker_error_t esp_rmaker_node_update_tag(const esp_rmaker_node_t *node, const char *tag_name, const char *tag_value)
{
    if (__esp_rmaker_node_is_tag_name_reserved(tag_name)) {
        OSAL_LOGE(TAG, "Tag name %s is reserved and cannot be used.", tag_name);
        return ESP_RMAKER_INVALID_ARG;
    }
    return __esp_rmaker_node_add_tag(node, tag_name, tag_value, true);
}

esp_rmaker_node_info_t *esp_rmaker_node_get_info(const esp_rmaker_node_t *node)
{
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    if (!_node) {
        OSAL_LOGE(TAG, "Node handle cannot be NULL.");
        return NULL;
    }
    return _node->info;
}

esp_rmaker_attr_t *esp_rmaker_node_get_first_attribute(const esp_rmaker_node_t *node)
{
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    if (!_node) {
        OSAL_LOGE(TAG, "Node handle cannot be NULL.");
        return NULL;
    }
    return _node->attributes;
}

esp_rmaker_tag_t *esp_rmaker_node_get_first_tag(const esp_rmaker_node_t *node)
{
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    if (!_node) {
        OSAL_LOGE(TAG, "Node handle cannot be NULL.");
        return NULL;
    }
    return _node->tags;
}

esp_rmaker_tag_t *esp_rmaker_node_get_tag_by_name(const esp_rmaker_node_t *node, const char *tag_name)
{
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    if (!_node || !tag_name) {
        OSAL_LOGE(TAG, "Node handle or tag name cannot be NULL.");
        return NULL;
    }

    /* Get tag */
    esp_rmaker_tag_t *tag = _node->tags;
    while (tag) {
        if (strcmp(tag->name, tag_name) == 0) {
            break;
        }
        tag = tag->next;
    }
    return tag;
}

esp_rmaker_error_t esp_rmaker_attribute_delete(esp_rmaker_attr_t *attr)
{
    if (attr) {
        if (attr->name) {
            free(attr->name);
        }
        if (attr->value) {
            free(attr->value);
        }
        free(attr);
        return ESP_RMAKER_OK;
    }
    return ESP_RMAKER_INVALID_ARG;
}

esp_rmaker_error_t esp_rmaker_tag_delete(esp_rmaker_tag_t *tag)
{
    if (tag) {
        if (tag->name) {
            free(tag->name);
        }
        if (tag->value) {
            free(tag->value);
        }
        free(tag);
        return ESP_RMAKER_OK;
    }
    return ESP_RMAKER_INVALID_ARG;
}

esp_rmaker_error_t esp_rmaker_parse_val_from_object(jparse_ctx_t *p_jctx, const char *param_id, esp_rmaker_val_type_t expected_type, esp_rmaker_param_val_t *val)
{
    if (!p_jctx || !param_id || !val) {
        return ESP_RMAKER_INVALID_ARG;
    }

    switch (expected_type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        if (json_obj_get_bool(p_jctx, param_id, &val->val.b) == 0) {
            val->type = RMAKER_VAL_TYPE_BOOLEAN;
            return ESP_RMAKER_OK;
        }
        break;
    case RMAKER_VAL_TYPE_INTEGER:
        if (json_obj_get_int(p_jctx, param_id, &val->val.i) == 0) {
            val->type = RMAKER_VAL_TYPE_INTEGER;
            return ESP_RMAKER_OK;
        }
        break;
    case RMAKER_VAL_TYPE_FLOAT:
        if (json_obj_get_float(p_jctx, param_id, &val->val.f) == 0) {
            val->type = RMAKER_VAL_TYPE_FLOAT;
            return ESP_RMAKER_OK;
        }
        break;
    case RMAKER_VAL_TYPE_STRING: {
        int val_size = 0;
        if (json_obj_get_strlen(p_jctx, param_id, &val_size) == 0) {
            val_size++; /* For NULL termination */
            val->val.s = OSAL_CALLOC_EXTRAM(1, val_size);
            if (!val->val.s) {
                return ESP_RMAKER_NO_MEM;
            }
            json_obj_get_string(p_jctx, param_id, val->val.s, val_size);
            val->type = RMAKER_VAL_TYPE_STRING;
            return ESP_RMAKER_OK;
        }
        break;
    }
    case RMAKER_VAL_TYPE_OBJECT: {
        int val_size = 0;
        if (json_obj_get_object_strlen(p_jctx, param_id, &val_size) == 0) {
            val_size++; /* For NULL termination */
            val->val.s = OSAL_CALLOC_EXTRAM(1, val_size);
            if (!val->val.s) {
                return ESP_RMAKER_NO_MEM;
            }
            json_obj_get_object_str(p_jctx, param_id, val->val.s, val_size);
            val->type = RMAKER_VAL_TYPE_OBJECT;
            return ESP_RMAKER_OK;
        }
        break;
    }
    case RMAKER_VAL_TYPE_ARRAY: {
        int val_size = 0;
        if (json_obj_get_array_strlen(p_jctx, param_id, &val_size) == 0) {
            val_size++; /* For NULL termination */
            val->val.s = OSAL_CALLOC_EXTRAM(1, val_size);
            if (!val->val.s) {
                return ESP_RMAKER_NO_MEM;
            }
            json_obj_get_array_str(p_jctx, param_id, val->val.s, val_size);
            val->type = RMAKER_VAL_TYPE_ARRAY;
            return ESP_RMAKER_OK;
        }
        break;
    }
    default:
        break;
    }
    return ESP_RMAKER_NOT_FOUND;
}
