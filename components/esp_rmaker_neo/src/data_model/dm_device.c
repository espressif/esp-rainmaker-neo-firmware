/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dm_device.c
 * @brief Device model functions.
 */

/* Declarations */
#include "node_internal.h"
#include "esp_rmaker_node.h"

/* Value includes */
#include "esp_rmaker_val.h"

/* Error types */
#include "esp_rmaker_error_types.h"

/* Standard types */
#include "esp_rmaker_standard_types.h"

/* Standard C headers */
#include <string.h>

/* Platform common headers */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* NVS includes */
#include "osal_storage.h"
#include "constants/nvs.h"
#include "constants/path.h"

/* State change management includes */
#include "network/state_changes.h"

/* Global variables ******************************************************************/

static const char *TAG = "rmng_dm_device";

/* Static function declarations *******************************************************/

/**
 * @brief Default bulk write callback.
 *
 * @param[in] device Pointer to the device.
 * @param[in] write_req Pointer to the write request.
 * @param[in] count Number of write requests.
 * @param[in] priv_data Pointer to the private data.
 * @param[in] ctx Context associated with the request.
 *
 * @return ESP_RMAKER_OK always; per-parameter failures are logged.
 */
static esp_rmaker_error_t esp_rmaker_default_bulk_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
        uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx);

/**
 * @brief Create a device.
 *
 * @param[in] id Id of the device.
 * @param[in] type Type of the device.
 * @param[in] priv Pointer to the private data.
 * @param[in] is_service Whether the device is a service.
 */
static esp_rmaker_device_t *__esp_rmaker_device_create(const char *id, const char *type, void *priv, bool is_service);

/* Static function definitions *******************************************************/

static esp_rmaker_error_t esp_rmaker_default_bulk_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
        uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    _esp_rmaker_device_t *_device = (_esp_rmaker_device_t *)device;
    _esp_rmaker_param_t *param;
    if (_device) {
        for (int i = 0; i < count; i++) {
            param = (_esp_rmaker_param_t *)(write_req[i].param);
            if (param->type && (strcmp(param->type, ESP_RMAKER_PARAM_NAME) == 0)) {
#ifndef CONFIG_RMAKER_NAME_PARAM_CB
                esp_rmaker_param_update(write_req[i].param, write_req[i].val);
                continue;
#else
                if (!_device->write_cb) {
                    esp_rmaker_param_update(write_req[i].param, write_req[i].val);
                    continue;
                }
#endif
            }
            if (_device->write_cb) {
                if (_device->write_cb(device, write_req[i].param, write_req[i].val, priv_data, ctx) != ESP_RMAKER_OK) {
                    OSAL_LOGE(TAG, "Remote update to param %s - %s failed", _device->id, ((_esp_rmaker_param_t *)(write_req[i].param))->id);
                }
            } else {
                OSAL_LOGW(TAG, "No write callback for device %s", _device->id);
            }
        }
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_device_t *__esp_rmaker_device_create(const char *id, const char *type, void *priv, bool is_service)
{
    if (!id) {
        OSAL_LOGE(TAG, "%s id is mandatory", is_service ? "Service" : "Device");
        return NULL;
    }
    if (strchr(id, RMAKER_PATH_SEPARATOR_CHAR)) {
        OSAL_LOGE(TAG, "%s id '%s' contains reserved path separator '%c'", is_service ? "Service" : "Device", id, RMAKER_PATH_SEPARATOR_CHAR);
        return NULL;
    }
    _esp_rmaker_device_t *_device = OSAL_CALLOC_EXTRAM(1, sizeof(_esp_rmaker_device_t));
    if (!_device) {
        OSAL_LOGE(TAG, "Failed to allocate memory for %s %s", is_service ? "Service" : "Device", id);
        return NULL;
    }
    _device->id = OSAL_STRDUP_EXTRAM(id);
    if (!_device->id) {
        OSAL_LOGE(TAG, "Failed to allocate memory for id for %s %s", is_service ? "Service" : "Device", id);
        goto device_create_err;
    }
    if (type) {
        _device->type = OSAL_STRDUP_EXTRAM(type);
        if (!_device->type) {
            OSAL_LOGE(TAG, "Failed to allocate memory for type for %s %s", is_service ? "Service" : "Device", id);
            goto device_create_err;
        }
    }
    _device->priv_data = priv;
    _device->is_service = is_service;
    /* Adding a default bulk write callback for backward compatibility with application code using single param write callback */
    _device->bulk_write_cb = esp_rmaker_default_bulk_write_cb;

    return (esp_rmaker_device_t *)_device;

device_create_err:
    esp_rmaker_device_delete((esp_rmaker_device_t *)_device);
    return NULL;
}

/* Public functions *******************************************************/

esp_rmaker_error_t esp_rmaker_device_clear_stored_values(const esp_rmaker_device_t *device)
{
    _esp_rmaker_device_t *_device = (_esp_rmaker_device_t *)device;
    if (!_device || !_device->id) {
        return ESP_RMAKER_INVALID_ARG;
    }

    osal_err_t nvs_err;
    osal_storage_handle_t handle;
    nvs_err = osal_storage_open(RMAKER_NVS_PART_NAME, _device->id, OSAL_STORAGE_OPEN_READWRITE, &handle);
    if (nvs_err == OSAL_ERR_NVS_NAMESPACE_NOT_FOUND) {
        OSAL_LOGD(TAG, "No NVS to clear for device %s", _device->id);
        return ESP_RMAKER_OK;
    }
    if (nvs_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to open NVS for device %s: %d", _device->id, nvs_err);
        return ESP_RMAKER_FAIL;
    }
    nvs_err = osal_storage_erase_all(handle);
    if (nvs_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to erase NVS for device %s: %d", _device->id, nvs_err);
        osal_storage_close(handle);
        return ESP_RMAKER_FAIL;
    }
    nvs_err = osal_storage_commit(handle);
    if (nvs_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to commit NVS erase for device %s: %d", _device->id, nvs_err);
        osal_storage_close(handle);
        return ESP_RMAKER_FAIL;
    }
    osal_storage_close(handle);
    OSAL_LOGD(TAG, "Cleared stored values for device %s", _device->id);
    return ESP_RMAKER_OK;
}

esp_rmaker_device_t *esp_rmaker_device_create(const char *id, const char *type, void *priv)
{
    return __esp_rmaker_device_create(id, type, priv, false);
}
esp_rmaker_device_t *esp_rmaker_service_create(const char *id, const char *type, void *priv)
{
    return __esp_rmaker_device_create(id, type, priv, true);
}

esp_rmaker_error_t esp_rmaker_device_delete(const esp_rmaker_device_t *device)
{
    _esp_rmaker_device_t *_device = (_esp_rmaker_device_t *)device;
    if (_device) {
        if (_device->parent) {
            OSAL_LOGE(TAG, "Cannot delete device as it is part of a node. Remove it from the node first.");
            return ESP_RMAKER_INVALID_STATE;
        }
        esp_rmaker_attr_t *attr = _device->attributes;
        while (attr) {
            esp_rmaker_attr_t *next_attr = attr->next;
            esp_rmaker_attribute_delete(attr);
            attr = next_attr;
        }

        _esp_rmaker_param_t *param = _device->params;
        while (param) {
            _esp_rmaker_param_t *next_param = param->next;
            esp_rmaker_param_delete((esp_rmaker_param_t *)param);
            param = next_param;
        }
        if (_device->type) {
            free(_device->type);
        }
        if (_device->id) {
            free(_device->id);
        }
        free(_device);
        return ESP_RMAKER_OK;
    }
    return ESP_RMAKER_INVALID_ARG;
}

esp_rmaker_error_t esp_rmaker_device_add_param(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param)
{
    if (!device || !param) {
        OSAL_LOGE(TAG, "Device or Param handle cannot be NULL");
        return ESP_RMAKER_INVALID_ARG;
    }
    _esp_rmaker_device_t *_device = (_esp_rmaker_device_t *)device;
    _esp_rmaker_param_t *_new_param = (_esp_rmaker_param_t *)param;

    /* Owning node for locking. NULL until the device is attached to a node
     * (construction phase has no report contention). */
    const esp_rmaker_node_t *lnode = _device->parent;

    _esp_rmaker_param_t *_param = _device->params;
    while (_param) {
        if (strcmp(_param->id, _new_param->id) == 0) {
            OSAL_LOGE(TAG, "Parameter with id %s already exists in Device %s", _new_param->id, _device->id);
            return ESP_RMAKER_INVALID_ARG;
        }
        if (_param->next) {
            _param = _param->next;
        } else {
            break;
        }
    }
    if (lnode) {
        esp_rmaker_node_lock(lnode);
    }
    if (_param) {
        _param->next = _new_param;
    } else {
        _device->params = _new_param;
    }
    _device->param_count++;
    _new_param->parent = _device;
    if (lnode) {
        esp_rmaker_node_unlock(lnode);
    }
    /* We check the stored value here, and not during param creation, because a parameter
     * in itself isn't unique. However, it is unique within a given device and hence can
     * be uniquely represented in storage only when added to a device.
     */
    esp_rmaker_param_val_t stored_val;
    stored_val.type = _new_param->val.type;
    if (_new_param->prop_flags & PROP_FLAG_PERSIST) {
        if (esp_rmaker_param_get_stored_value(_new_param, &stored_val) == ESP_RMAKER_OK) {
            if (lnode) {
                esp_rmaker_node_lock(lnode);
            }
            if ((_new_param->val.type == RMAKER_VAL_TYPE_STRING) || (_new_param->val.type == RMAKER_VAL_TYPE_OBJECT)
                    || (_new_param->val.type == RMAKER_VAL_TYPE_ARRAY)) {
                if (_new_param->val.val.s) {
                    free(_new_param->val.val.s);
                }
            }
            _new_param->val = stored_val;
            if (lnode) {
                esp_rmaker_node_unlock(lnode);
            }
            /* The device callback should be invoked once with the stored value, so
             * that applications can do initialisations as required.
             */
            if (_device->bulk_write_cb) {
                /* However, the callback should be invoked, only if the parameter is not
                 * of type ESP_RMAKER_PARAM_NAME, as it has special handling internally.
                 */
                if (!(_new_param->type && strcmp(_new_param->type, ESP_RMAKER_PARAM_NAME) == 0)) {
                    esp_rmaker_write_ctx_t ctx = {
                        .src = ESP_RMAKER_REQ_SRC_INIT,
                    };
                    esp_rmaker_param_write_req_t write_req = {
                        .param = (esp_rmaker_param_t *)param,
                        .val = stored_val,
                    };
                    _device->bulk_write_cb(device, &write_req, 1, _device->priv_data, &ctx);
                }
            }
        } else {
            esp_rmaker_param_store_value(_new_param);
        }
    }
    OSAL_LOGD(TAG, "Param %s added in %s", _new_param->id, _device->id);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_device_add_attribute(const esp_rmaker_device_t *device, const char *attr_name, const char *val)
{
    if (!device || !attr_name || !val) {
        OSAL_LOGE(TAG, "Device handle, attribute name or value cannot be NULL");
        return ESP_RMAKER_INVALID_ARG;
    }
    _esp_rmaker_device_t *_device = ( _esp_rmaker_device_t *)device;
    esp_rmaker_attr_t *attr = _device->attributes;
    while (attr) {
        if (strcmp(attr_name, attr->name) == 0) {
            OSAL_LOGE(TAG, "Attribute with name %s already exists in Device %s", attr_name, _device->id);
            return ESP_RMAKER_INVALID_ARG;
        }
        if (attr->next) {
            attr = attr->next;
        } else {
            break;
        }
    }
    esp_rmaker_attr_t *new_attr = OSAL_CALLOC_EXTRAM(1, sizeof(esp_rmaker_attr_t));
    if (!new_attr) {
        OSAL_LOGE(TAG, "Failed to allocate memory for device attribute");
        return ESP_RMAKER_NO_MEM;
    }
    new_attr->name = OSAL_STRDUP_EXTRAM(attr_name);
    new_attr->value = OSAL_STRDUP_EXTRAM(val);
    if (!new_attr->name || !new_attr->value) {
        OSAL_LOGE(TAG, "Failed to allocate memory for device attribute name or value");
        esp_rmaker_attribute_delete(new_attr);
        return ESP_RMAKER_NO_MEM;
    }
    if (attr) {
        attr->next = new_attr;
    } else {
        _device->attributes = new_attr;
    }
    OSAL_LOGD(TAG, "Device attribute %s.%s added", _device->id, attr_name);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_device_assign_primary_param(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param)
{
    if (!device || !param) {
        OSAL_LOGE(TAG, "Device or Param handle cannot be NULL");
        return ESP_RMAKER_INVALID_ARG;
    }
    ((_esp_rmaker_device_t *)device)->primary = (_esp_rmaker_param_t *)param;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_device_add_cb(const esp_rmaker_device_t *device, esp_rmaker_device_write_cb_t write_cb, esp_rmaker_device_read_cb_t read_cb)
{
    if (!device) {
        OSAL_LOGE(TAG, "Device handle cannot be NULL");
        return ESP_RMAKER_INVALID_ARG;
    }
    _esp_rmaker_device_t *_device = (_esp_rmaker_device_t *)device;
    _device->write_cb = write_cb;
    _device->read_cb = read_cb;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_device_add_bulk_cb(const esp_rmaker_device_t *device, esp_rmaker_device_bulk_write_cb_t write_cb,
        esp_rmaker_device_bulk_read_cb_t read_cb)
{
    if (!device) {
        OSAL_LOGE(TAG, "Device handle cannot be NULL");
        return ESP_RMAKER_INVALID_ARG;
    }
    _esp_rmaker_device_t *_device = (_esp_rmaker_device_t *)device;
    _device->bulk_write_cb = write_cb;
    _device->bulk_read_cb = read_cb;
    return ESP_RMAKER_OK;
}

char *esp_rmaker_device_get_id(const esp_rmaker_device_t *device)
{
    if (!device) {
        OSAL_LOGE(TAG, "Device handle cannot be NULL.");
        return NULL;
    }
    return ((_esp_rmaker_device_t *)device)->id;
}

void *esp_rmaker_device_get_priv_data(const esp_rmaker_device_t *device)
{
    if (!device) {
        OSAL_LOGE(TAG, "Device handle cannot be NULL.");
        return NULL;
    }
    return ((_esp_rmaker_device_t *)device)->priv_data;
}

char *esp_rmaker_device_get_type(const esp_rmaker_device_t *device)
{
    if (!device) {
        OSAL_LOGE(TAG, "Device handle cannot be NULL.");
        return NULL;
    }
    return ((_esp_rmaker_device_t *)device)->type;
}

esp_rmaker_param_t *esp_rmaker_device_get_param_by_type(const esp_rmaker_device_t *device, const char *param_type)
{
    if (!device || !param_type) {
        OSAL_LOGE(TAG, "Device handle or param type cannot be NULL");
        return NULL;
    }
    _esp_rmaker_param_t *param = ((_esp_rmaker_device_t *)device)->params;
    while (param) {
        if (strcmp(param->type, param_type) == 0) {
            break;
        }
        param = param->next;
    }
    return (esp_rmaker_param_t *)param;
}

esp_rmaker_param_t *esp_rmaker_device_get_param_by_id(const esp_rmaker_device_t *device, const char *param_id)
{
    if (!device || !param_id) {
        OSAL_LOGE(TAG, "Device handle or param id cannot be NULL");
        return NULL;
    }
    _esp_rmaker_param_t *param = ((_esp_rmaker_device_t *)device)->params;
    while (param) {
        if (strcmp(param->id, param_id) == 0) {
            break;
        }
        param = param->next;
    }
    return (esp_rmaker_param_t *)param;
}
