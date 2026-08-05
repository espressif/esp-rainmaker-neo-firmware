/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dm_node.c
 * @brief Node model functions.
 */

/* Includes *******************************************************/

/* Declarations */
#include "node_internal.h"

/* Platform common headers */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Standard C headers */
#include <string.h>

/* Constants *******************************************************/

/**
 * @brief Tag for the node model.
 */
static const char *TAG = "rmng_dm_node";

/* Private function declarations *******************************************************/

/**
 * @brief Populate the node state as a JSON string.
 * @note This function must be called with the state lock held.
 *
 * @param[in] node Node handle.
 * @param[out] node_state Pointer to the node state JSON string. If NULL, then the function will return the required length of the node state JSON string.
 * @param[out] node_state_len Pointer to the length of the node state JSON string.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __esp_rmaker_populate_node_state_locked(const esp_rmaker_node_t *node, char *node_state, size_t *node_state_len);

/**
 * @brief Report the parameter configuration as a JSON string.
 *
 * @param[in] param Parameter pointer.
 * @param[in] jptr JSON generation string pointer.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t esp_rmaker_report_param_config(_esp_rmaker_param_t *param, json_gen_str_t *jptr);

/**
 * @brief Report the devices or services as a JSON string.
 *
 * @param[in] node Node handle.
 * @param[in] jptr JSON generation string pointer.
 * @param[in] key Key for the devices or services.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t esp_rmaker_report_devices_or_services(const esp_rmaker_node_t *node, json_gen_str_t *jptr, char *key);

/* Private function definitions *******************************************************/

static esp_rmaker_error_t __esp_rmaker_populate_node_state_locked(const esp_rmaker_node_t *node, char *node_state, size_t *node_state_len)
{
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    if (!_node) {
        OSAL_LOGE(TAG, "Node handle cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    json_gen_str_t jstr;

    json_gen_str_start(&jstr, node_state, *node_state_len, NULL, NULL);

    if (json_gen_start_object(&jstr) == 0) {
        _esp_rmaker_device_t *device = _node->devices;
        while (device) {
            bool device_added = false;
            _esp_rmaker_param_t *param = device->params;
            while (param) {
                if (!device_added) {
                    json_gen_push_object(&jstr, device->id);
                    device_added = true;
                }
                esp_rmaker_report_value(&param->val, param->id, &jstr);
                param = param->next;
            }
            if (device_added) {
                json_gen_pop_object(&jstr);
            }
            device = device->next;
        }
        json_gen_end_object(&jstr);
    }

    *node_state_len = json_gen_str_end(&jstr);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t esp_rmaker_report_param_config(_esp_rmaker_param_t *param, json_gen_str_t *jptr)
{
    json_gen_start_object(jptr);
    if (param->id) {
        json_gen_obj_set_string(jptr, "id", param->id);
    }
    if (param->type) {
        json_gen_obj_set_string(jptr, "type", param->type);
    }
    esp_rmaker_report_data_type(param->val.type, "data_type", jptr);
    json_gen_push_array(jptr, "properties");
    if (param->prop_flags & PROP_FLAG_READ) {
        json_gen_arr_set_string(jptr, "read");
    }
    if (param->prop_flags & PROP_FLAG_WRITE) {
        json_gen_arr_set_string(jptr, "write");
    }
    if (param->prop_flags & (PROP_FLAG_TIME_SERIES | PROP_FLAG_TS_CUMULATIVE)) {
        json_gen_arr_set_string(jptr, "time_series");
    }
    if (param->prop_flags & PROP_FLAG_INDEXED) {
        json_gen_arr_set_string(jptr, "indexed");
    }
    /* TODO: confirm whether the cloud consumes "persist" in the node config; it
     * is node-local behaviour and may not need to be advertised. */
    if (param->prop_flags & PROP_FLAG_PERSIST) {
        json_gen_arr_set_string(jptr, "persist");
    }

    json_gen_pop_array(jptr);
    if (param->bounds) {
        json_gen_push_object(jptr, "bounds");
        esp_rmaker_report_value(&param->bounds->min, "min", jptr);
        esp_rmaker_report_value(&param->bounds->max, "max", jptr);
        esp_rmaker_report_value(&param->bounds->step, "step", jptr);
        json_gen_pop_object(jptr);
    }
    if (param->ui_type) {
        json_gen_obj_set_string(jptr, "ui_type", param->ui_type);
    }
    json_gen_end_object(jptr);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t esp_rmaker_report_devices_or_services(const esp_rmaker_node_t *node, json_gen_str_t *jptr, char *key)
{
    _esp_rmaker_device_t *device = esp_rmaker_node_get_first_device(node);
    if (!device) {
        return ESP_RMAKER_OK;
    }
    bool is_service = false;
    if (strcmp(key, "services") == 0) {
        is_service = true;
    }
    json_gen_push_array(jptr, key);
    while (device) {
        if (device->is_service == is_service) {
            json_gen_start_object(jptr);
            json_gen_obj_set_string(jptr, "id", device->id);
            if (device->type) {
                json_gen_obj_set_string(jptr, "type", device->type);
            }
            if (device->attributes) {
                json_gen_push_array(jptr, "attributes");
                esp_rmaker_attr_t *attr = device->attributes;
                while (attr) {
                    esp_rmaker_report_attribute(attr, jptr);
                    attr = attr->next;
                }
                json_gen_pop_array(jptr);
            }
            if (device->primary) {
                json_gen_obj_set_string(jptr, "primary", device->primary->id);
            }
            if (device->params) {
                json_gen_push_array(jptr, "params");
                _esp_rmaker_param_t *param = device->params;
                while (param) {
                    esp_rmaker_report_param_config(param, jptr);
                    param = param->next;
                }
                json_gen_pop_array(jptr);
            }
            json_gen_end_object(jptr);
        }
        device = device->next;
    }
    json_gen_pop_array(jptr);
    return ESP_RMAKER_OK;
}

/* Data model adapter - node (static implementations) ************************/

const char *data_model_node_get_data_model_type(void)
{
    return "default";
}

esp_rmaker_error_t data_model_node_write_data_model_config(const esp_rmaker_node_t *node, json_gen_str_t *jptr)
{
    if (!node || !jptr) {
        OSAL_LOGE(TAG, "Node handle or JSON generation string pointer cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_report_devices_or_services(node, jptr, "devices");
    esp_rmaker_report_devices_or_services(node, jptr, "services");
    return ESP_RMAKER_OK;
}

char *data_model_node_get_node_params(void)
{
    const esp_rmaker_node_t *node = esp_rmaker_get_node();
    if (!node) {
        OSAL_LOGE(TAG, "Node handle cannot be NULL.");
        return NULL;
    }
    esp_rmaker_node_lock(node);
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    size_t node_state_len = 0;
    err = __esp_rmaker_populate_node_state_locked(node, NULL, &node_state_len);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to determine required size for node state JSON.");
        esp_rmaker_node_unlock(node);
        return NULL;
    }
    char *node_state = OSAL_CALLOC_EXTRAM(1, node_state_len);
    if (!node_state) {
        OSAL_LOGE(TAG, "Failed to allocate memory for node state JSON.");
        esp_rmaker_node_unlock(node);
        return NULL;
    }
    err = __esp_rmaker_populate_node_state_locked(node, node_state, &node_state_len);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to generate node state JSON.");
        free(node_state);
        esp_rmaker_node_unlock(node);
        return NULL;
    }
    OSAL_LOGD(TAG, "Generated node state:\n%s", node_state);
    esp_rmaker_node_unlock(node);
    return node_state;
}


esp_rmaker_error_t esp_rmaker_node_clear_stored_values(const esp_rmaker_node_t *node)
{
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    if (!_node) {
        OSAL_LOGE(TAG, "Node handle cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }

    _esp_rmaker_device_t *device = _node->devices;
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    while (device) {
        esp_rmaker_error_t device_err = esp_rmaker_device_clear_stored_values((esp_rmaker_device_t *)device);
        if (device_err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to clear stored values for device %s", device->id);
            err = device_err; // Continue with other devices but remember the error
        }
        device = device->next;
    }
    return err;
}

_esp_rmaker_device_t *esp_rmaker_node_get_first_device(const esp_rmaker_node_t *node)
{
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    if (!_node) {
        OSAL_LOGE(TAG, "Node handle cannot be NULL.");
        return NULL;
    }
    return _node->devices;
}

esp_rmaker_device_t *esp_rmaker_node_get_device_by_id(const esp_rmaker_node_t *node, const char *device_id)
{
    if (!node || !device_id) {
        OSAL_LOGE(TAG, "Node handle or device id cannot be NULL");
        return NULL;
    }
    _esp_rmaker_device_t *device = ((_esp_rmaker_node_t *)node)->devices;
    while (device) {
        if (strcmp(device->id, device_id) == 0) {
            break;
        }
        device = device->next;
    }
    return (esp_rmaker_device_t *)device;
}

esp_rmaker_error_t esp_rmaker_node_add_device(const esp_rmaker_node_t *node, const esp_rmaker_device_t *device)
{
    if (!node || !device) {
        OSAL_LOGE(TAG, "Node or Device/Service handle cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    _esp_rmaker_device_t *_new_device = (_esp_rmaker_device_t *)device;
    _esp_rmaker_device_t *_device = _node->devices;
    while (_device) {
        if (strcmp(_device->id, _new_device->id) == 0) {
            OSAL_LOGE(TAG, "%s with id %s already exists", _new_device->is_service ? "Service" : "Device", _new_device->id);
            return ESP_RMAKER_INVALID_ARG;
        }
        if (_device->next) {
            _device = _device->next;
        } else {
            break;
        }
    }

    esp_rmaker_node_lock(node);
    if (_device) {
        _device->next = _new_device;
    } else {
        _node->devices = _new_device;
    }
    _new_device->parent = node;
    esp_rmaker_node_unlock(node);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_node_remove_device(const esp_rmaker_node_t *node, const esp_rmaker_device_t *device)
{
    if (!node || !device) {
        OSAL_LOGE(TAG, "Node or Device/Service handle cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    _esp_rmaker_device_t *_device = (_esp_rmaker_device_t *)device;

    _esp_rmaker_device_t *tmp_device = _node->devices;
    _esp_rmaker_device_t *prev_device = NULL;
    while (tmp_device) {
        if (tmp_device == _device) {
            break;
        }
        prev_device = tmp_device;
        tmp_device = tmp_device->next;
    }
    if (!tmp_device) {
        OSAL_LOGE(TAG, "Device %s not found in node %s", _device->id, _node->info->name);
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_node_lock(node);
    if (tmp_device == _node->devices) {
        _node->devices = tmp_device->next;
    } else {
        prev_device->next = tmp_device->next;
    }
    tmp_device->parent = NULL;
    esp_rmaker_node_unlock(node);
    return ESP_RMAKER_OK;
}
