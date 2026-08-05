/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dm_param.c
 * @brief Parameter model functions.
 */

#include "node_internal.h"

/* Timeseries includes */
#include "timeseries.h"
#include "osal_timesync.h"

/* Error types */
#include "esp_rmaker_error_types.h"

/* Platform common headers */
#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_time.h"

/* NVS-common headers */
#include "osal_storage.h"
#include "constants/nvs.h"
#include "constants/path.h"

/* Standard C headers */
#include <string.h>

/* JSON parser includes */
#include "json_parser.h"

/* State change management includes */
#include "network/state_changes.h"

/* Notify includes */
#include "network/notify.h"

/* Global variables *******************************************************/

/**
 * @brief Tag for the parameter model.
 */
static const char *TAG = "rmng_dm_param";

/* Function definitions *******************************************************/

esp_rmaker_param_t *esp_rmaker_param_create(const char *param_id, const char *type,
        esp_rmaker_param_val_t val, uint8_t properties)
{
    if (!param_id) {
        OSAL_LOGE(TAG, "Param id is mandatory");
        return NULL;
    }
    if (strchr(param_id, RMAKER_PATH_SEPARATOR_CHAR)) {
        OSAL_LOGE(TAG, "Param id '%s' contains reserved path separator '%c'", param_id, RMAKER_PATH_SEPARATOR_CHAR);
        return NULL;
    }
    bool is_ts = properties & PROP_FLAG_TIME_SERIES;
    bool is_ts_cumulative = properties & PROP_FLAG_TS_CUMULATIVE;

    if (is_ts && is_ts_cumulative) {
        OSAL_LOGE(TAG, "PROP_FLAG_TIME_SERIES and PROP_FLAG_TS_CUMULATIVE cannot be set together.");
        return NULL;
    }
    if (is_ts || is_ts_cumulative) {
        /* Check data type compatibility */
        if ((val.type == RMAKER_VAL_TYPE_ARRAY) || (val.type == RMAKER_VAL_TYPE_OBJECT)) {
            OSAL_LOGE(TAG, "Time series flags are not allowed for array/object param types.");
            return NULL;
        }

        /* Check time synchronization is initialized */
        if (!osal_timesync_is_initialized()) {
            OSAL_LOGE(TAG, "Please enable time synchronization in the RMNG configuration before creating time series params.");
            return NULL;
        }
    }
    _esp_rmaker_param_t *param = OSAL_CALLOC_EXTRAM(1, sizeof(_esp_rmaker_param_t));
    if (!param) {
        OSAL_LOGE(TAG, "Failed to allocate memory for param %s", param_id);
        return NULL;
    }
    param->id = OSAL_STRDUP_EXTRAM(param_id);
    if (!param->id) {
        OSAL_LOGE(TAG, "Failed to allocate memory for id for param %s.", param_id);
        goto param_create_err;
    }
    if (type) {
        param->type = OSAL_STRDUP_EXTRAM(type);
        if (!param->type) {
            OSAL_LOGE(TAG, "Failed to allocate memory for type for param %s.", param_id);
            goto param_create_err;
        }
    }
    param->val.type = val.type;
    param->prop_flags = properties;
    param->ttl_days = 0; /* Initialize TTL days to 0 */
    if ((val.type == RMAKER_VAL_TYPE_STRING) || (val.type == RMAKER_VAL_TYPE_OBJECT) ||
            (val.type == RMAKER_VAL_TYPE_ARRAY)) {
        if (val.val.s) {
            param->val.val.s = OSAL_STRDUP_EXTRAM(val.val.s);
            if (!param->val.val.s) {
                OSAL_LOGE(TAG, "Failed to allocate memory for the value of param %s.", param_id);
            }
        }
    } else {
        param->val.val = val.val;
    }
    return (esp_rmaker_param_t *)param;

param_create_err:
    esp_rmaker_param_delete((esp_rmaker_param_t *)param);
    return NULL;
}

esp_rmaker_error_t esp_rmaker_param_delete(const esp_rmaker_param_t *param)
{
    _esp_rmaker_param_t *_param = (_esp_rmaker_param_t *)param;
    if (!_param) {
        return ESP_RMAKER_INVALID_ARG;
    }

    if (_param->id) {
        free(_param->id);
    }
    if (_param->type) {
        free(_param->type);
    }
    if (_param->ui_type) {
        free(_param->ui_type);
    }
    if ((_param->val.type == RMAKER_VAL_TYPE_STRING) || (_param->val.type == RMAKER_VAL_TYPE_OBJECT) ||
            (_param->val.type == RMAKER_VAL_TYPE_ARRAY)) {
        if (_param->val.val.s) {
            free(_param->val.val.s);
        }
    }
    if (_param->bounds) {
        free(_param->bounds);
    }
    free(_param);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_param_add_bounds(const esp_rmaker_param_t *param,
        esp_rmaker_param_val_t min, esp_rmaker_param_val_t max, esp_rmaker_param_val_t step)
{
    if (!param) {
        OSAL_LOGE(TAG, "Param handle cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    _esp_rmaker_param_t *_param = (_esp_rmaker_param_t *)param;
    if ((_param->val.type != RMAKER_VAL_TYPE_INTEGER) && (_param->val.type != RMAKER_VAL_TYPE_FLOAT)) {
        OSAL_LOGE(TAG, "Only integer and float params can have bounds.");
        return ESP_RMAKER_INVALID_ARG;
    }
    if ((min.type != _param->val.type) || (max.type != _param->val.type) || (step.type != _param->val.type)) {
        OSAL_LOGE(TAG, "Cannot set bounds for %s because of value type mismatch.", _param->id);
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_param_bounds_t *bounds = OSAL_CALLOC_EXTRAM(1, sizeof(esp_rmaker_param_bounds_t));
    if (!bounds) {
        OSAL_LOGE(TAG, "Failed to allocate memory for parameter bounds.");
        return ESP_RMAKER_NO_MEM;
    }
    bounds->min = min;
    bounds->max = max;
    bounds->step = step;
    if (_param->bounds) {
        free(_param->bounds);
    }
    _param->bounds = bounds;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_param_add_ui_type(const esp_rmaker_param_t *param, const char *ui_type)
{
    if (!param || !ui_type) {
        OSAL_LOGE(TAG, "Param handle or UI type cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    _esp_rmaker_param_t *_param = (_esp_rmaker_param_t *)param;
    if (_param->ui_type) {
        free(_param->ui_type);
    }
    if ((_param->ui_type = OSAL_STRDUP_EXTRAM(ui_type)) != NULL ) {
        return ESP_RMAKER_OK;
    } else {
        return ESP_RMAKER_NO_MEM;
    }
}

esp_rmaker_error_t esp_rmaker_param_add_array_max_count(const esp_rmaker_param_t *param, int count)
{
    if (!param) {
        OSAL_LOGE(TAG, "Param handle cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    _esp_rmaker_param_t *_param = (_esp_rmaker_param_t *)param;
    if (_param->val.type != RMAKER_VAL_TYPE_ARRAY) {
        OSAL_LOGE(TAG, "Only array params can have max count.");
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_param_bounds_t *bounds = OSAL_CALLOC_EXTRAM(1, sizeof(esp_rmaker_param_bounds_t));
    if (!bounds) {
        OSAL_LOGE(TAG, "Failed to allocate memory for parameter bounds.");
        return ESP_RMAKER_NO_MEM;
    }
    bounds->max = esp_rmaker_int(count);
    if (_param->bounds) {
        free(_param->bounds);
    }
    _param->bounds = bounds;
    return ESP_RMAKER_OK;
}

char *esp_rmaker_param_get_id(const esp_rmaker_param_t *param)
{
    if (!param) {
        OSAL_LOGE(TAG, "Param handle cannot be NULL.");
        return NULL;
    }
    return ((_esp_rmaker_param_t *)param)->id;
}

char *esp_rmaker_param_get_type(const esp_rmaker_param_t *param)
{
    if (!param) {
        OSAL_LOGE(TAG, "Param handle cannot be NULL.");
        return NULL;
    }
    return ((_esp_rmaker_param_t *)param)->type;
}

esp_rmaker_param_val_t *esp_rmaker_param_get_val(esp_rmaker_param_t *param)
{
    if (!param) {
        OSAL_LOGE(TAG, "Param handle cannot be NULL.");
        return NULL;
    }
    return &((_esp_rmaker_param_t *)param)->val;
}

esp_rmaker_error_t esp_rmaker_param_update(const esp_rmaker_param_t *param, esp_rmaker_param_val_t val)
{
    if (!param) {
        OSAL_LOGW(TAG, "Param handle is NULL, skipping value update.");
        return ESP_RMAKER_INVALID_ARG;
    }
    _esp_rmaker_param_t *_param = (_esp_rmaker_param_t *)param;

    /* Owning node for locking. NULL until the param's device is attached to
     * a node (construction phase has no report contention). */
    const esp_rmaker_node_t *lnode = _param->parent ? _param->parent->parent : NULL;

    if (lnode) {
        esp_rmaker_node_lock(lnode);
    }

    /* Compare against the current value BEFORE applying the new one. An equal value is
     * still applied/reported and still re-evaluates automations - only the redundant NVS
     * write-back is skipped, to avoid flash wear. A type mismatch is rejected here. */
    esp_rmaker_error_t compare_err = esp_rmaker_val_compare(&_param->val, &val, RMAKER_VAL_COMPARE_EQ);
    if (compare_err == ESP_RMAKER_INVALID_ARG) {
        OSAL_LOGE(TAG, "Could not compare values - possible type mismatch.");
        if (lnode) {
            esp_rmaker_node_unlock(lnode);
        }
        return compare_err;
    }
    bool value_unchanged = (compare_err == ESP_RMAKER_OK);

    /* Check if value is within bounds */
    if (_param->bounds) {
        switch (_param->val.type) {
        case RMAKER_VAL_TYPE_INTEGER:
            if (val.val.i < _param->bounds->min.val.i || val.val.i > _param->bounds->max.val.i) {
                OSAL_LOGE(TAG, "New param value out of bounds.");
                if (lnode) {
                    esp_rmaker_node_unlock(lnode);
                }
                return ESP_RMAKER_INVALID_ARG;
            }
            break;
        case RMAKER_VAL_TYPE_FLOAT:
            if (val.val.f < _param->bounds->min.val.f || val.val.f > _param->bounds->max.val.f) {
                OSAL_LOGE(TAG, "New param value out of bounds.");
                if (lnode) {
                    esp_rmaker_node_unlock(lnode);
                }
                return ESP_RMAKER_INVALID_ARG;
            }
            break;
        default:
            break;
        }
    }

    switch (_param->val.type) {
    case RMAKER_VAL_TYPE_STRING:
    case RMAKER_VAL_TYPE_OBJECT:
    case RMAKER_VAL_TYPE_ARRAY: {
        char *new_val = NULL;
        if (val.val.s) {
            new_val = OSAL_STRDUP_EXTRAM(val.val.s);
            if (!new_val) {
                OSAL_LOGE(TAG, "Failed to allocate memory for new value for param %s.", _param->id);
                if (lnode) {
                    esp_rmaker_node_unlock(lnode);
                }
                return ESP_RMAKER_FAIL;
            }
        }
        if (_param->val.val.s) {
            free(_param->val.val.s);
        }
        _param->val.val.s = new_val;
        break;
    }
    case RMAKER_VAL_TYPE_BOOLEAN:
    case RMAKER_VAL_TYPE_INTEGER:
    case RMAKER_VAL_TYPE_FLOAT:
        _param->val.val = val.val;
        break;
    default:
        if (lnode) {
            esp_rmaker_node_unlock(lnode);
        }
        return ESP_RMAKER_INVALID_ARG;
    }

    if (lnode) {
        esp_rmaker_node_unlock(lnode);
    }

    /* NVS write-back only if the value actually changed and the param is persistent */
    if (!value_unchanged && (_param->prop_flags & PROP_FLAG_PERSIST)) {
        esp_rmaker_param_store_value(_param);
    }

    /* Get the update ID for the parameter */
    esp_rmaker_state_update_id_t update_id = esp_rmaker_state_update_id_create((esp_rmaker_param_t *)_param);
    if (!update_id) {
        OSAL_LOGE(TAG, "Failed to get update ID for parameter %s.", _param->id);
        return ESP_RMAKER_NO_MEM;
    }

    /* Mark the update ID for update */
    OSAL_LOGI(TAG, "Marking parameter %s for update.", _param->id);
    esp_rmaker_state_mark_for_update(update_id);

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_param_update_and_report(const esp_rmaker_param_t *param, esp_rmaker_param_val_t val)
{
    if (!param) {
        OSAL_LOGW(TAG, "Param handle is NULL, skipping value update and report.");
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t err = esp_rmaker_param_update(param, val);

    if (err == ESP_RMAKER_OK) {
        /* Report all parameters with value change */
        err = esp_rmaker_state_report(false);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to report all changed parameters: %d", err);
        }
    } else {
        OSAL_LOGE(TAG, "Failed to update parameter %s with value %d: %d", esp_rmaker_param_get_id(param), val.val.i, err);
    }

    return err;
}

esp_rmaker_error_t esp_rmaker_param_update_and_notify(const esp_rmaker_param_t *param, esp_rmaker_param_val_t val)
{
    if (!param) {
        OSAL_LOGW(TAG, "Param handle is NULL, skipping value update and notify.");
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t err = esp_rmaker_param_update(param, val);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update parameter %s before notify: %d", esp_rmaker_param_get_id(param), err);
        return err;
    }

    /* Fire a push notification for this update. The update ID identifies what changed and
     * is passed to the data-model-agnostic notify path.
     *
     * Best-effort: a notify failure is logged but does not change the update result. */
    esp_rmaker_state_update_id_t update_id = esp_rmaker_state_update_id_create((esp_rmaker_param_t *)param);
    const char *param_id = esp_rmaker_param_get_id(param);
    if (!param_id) {
        param_id = "<null>";
    }
    if (!update_id) {
        OSAL_LOGW(TAG, "Failed to create update ID for parameter %s; skipping notify.", param_id);
        return err;
    }

    esp_rmaker_error_t notify_err = esp_rmaker_notify_send_push(update_id);
    if (notify_err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to send push notification for parameter %s: %d", param_id, notify_err);
    } else {
        OSAL_LOGI(TAG, "Sent push notification for parameter %s", param_id);
    }

    data_model_state_update_id_release(update_id);

    return err;
}

esp_rmaker_error_t esp_rmaker_param_get_stored_value(_esp_rmaker_param_t *param, esp_rmaker_param_val_t *val)
{
    if (!param || !param->parent || !val) {
        return ESP_RMAKER_INVALID_ARG;
    }
    osal_storage_handle_t handle;
    osal_err_t err = osal_storage_open(RMAKER_NVS_PART_NAME, param->parent->id, OSAL_STORAGE_OPEN_READONLY, &handle);
    if (err == OSAL_ERR_NVS_NAMESPACE_NOT_FOUND) {
        OSAL_LOGD(TAG, "NVS namespace not found for param %s; value has not yet been stored", param->id);
        return ESP_RMAKER_NOT_FOUND;
    }
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to open NVS handle for param %s", param->id);
        return ESP_RMAKER_FAIL;
    }
    if ((param->val.type == RMAKER_VAL_TYPE_STRING) || (param->val.type == RMAKER_VAL_TYPE_OBJECT) ||
            (param->val.type == RMAKER_VAL_TYPE_ARRAY)) {
        size_t len = 0;
        if ((err = osal_storage_get(handle, param->id, NULL, &len, OSAL_STORAGE_TYPE_BINARY)) == OSAL_ERR_OK) {
            char *s_val = OSAL_CALLOC_EXTRAM(1, len + 1);
            if (!s_val) {
                OSAL_LOGE(TAG, "Failed to allocate memory for string value for param %s", param->id);
                return ESP_RMAKER_NO_MEM;
            } else {
                osal_storage_get(handle, param->id, s_val, &len, OSAL_STORAGE_TYPE_BINARY);
                s_val[len] = '\0';
                val->type = param->val.type;
                val->val.s = s_val;
            }
        }
    } else {
        size_t len = sizeof(esp_rmaker_param_val_t);
        err = osal_storage_get(handle, param->id, val, &len, OSAL_STORAGE_TYPE_BINARY);
    }
    osal_storage_close(handle);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to get value for param %s", param->id);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_param_store_value(_esp_rmaker_param_t *param)
{
    if (!param || !param->parent) {
        return ESP_RMAKER_INVALID_ARG;
    }
    osal_storage_handle_t handle;
    osal_err_t err = osal_storage_open(RMAKER_NVS_PART_NAME, param->parent->id, OSAL_STORAGE_OPEN_READWRITE, &handle);
    if (err != OSAL_ERR_OK) {
        return ESP_RMAKER_FAIL;
    }
    if ((param->val.type == RMAKER_VAL_TYPE_STRING) || (param->val.type == RMAKER_VAL_TYPE_OBJECT) ||
            (param->val.type == RMAKER_VAL_TYPE_ARRAY)) {
        /* Store only if value is not NULL */
        if (param->val.val.s) {
            err = osal_storage_set(handle, param->id, param->val.val.s, strlen(param->val.val.s), OSAL_STORAGE_TYPE_BINARY);
            osal_storage_commit(handle);
        } else {
            err = OSAL_ERR_OK;
        }
    } else {
        err = osal_storage_set(handle, param->id, &param->val, sizeof(esp_rmaker_param_val_t), OSAL_STORAGE_TYPE_BINARY);
        osal_storage_commit(handle);
    }
    osal_storage_close(handle);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to store value for param %s", param->id);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}
