/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dm_state_changes.c
 * @brief Default state changes implementation.
 */

/* Includes *******************************************************/

/* Declarations */
#include "data_model_internal.h"
#include "network/state_changes.h"

/* Standard includes */
#include <inttypes.h>

/* Platform includes */
#include "osal_mem_alloc.h"
#include "osal_log.h"
#include "osal_time.h"

/* Node model includes */
#include "node_internal.h"
#include "timeseries.h"

/* Identity buffer-size constant */
#include "constants/identity.h"

/* Timesync includes */
#include "osal_timesync.h"

/* Constants *******************************************************/

/**
 * @brief Mode of the set parameters request.
 */
typedef enum {
    /* Look for parameters by id */
    SET_PARAMS_MODE_BY_ID = 0,
    /* Look for parameters by type */
    SET_PARAMS_MODE_BY_TYPE = 1,
} __set_params_mode_t;

/* Global variables ******************************************************************/

static const char *TAG = "rmng_dm_state_chg";

/* Static function declarations *******************************************************/

/**
 * @brief Call the bulk write callback with the set parameters for a device.
 *
 * @param[in] device Pointer to the device.
 * @param[in] jptr Pointer to the JSON context.
 * @param[in] src Source of the request.
 * @param[in] mode Mode of the request.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t esp_rmaker_device_set_params(_esp_rmaker_device_t *device, jparse_ctx_t *jptr, esp_rmaker_req_src_t src, __set_params_mode_t mode);

/**
 * @brief Convert the set params mode to a string.
 *
 * @param[in] mode Set params mode.
 *
 * @return String representation of the set params mode.
 */
static const char *__set_params_mode_to_string(__set_params_mode_t mode);

/* Static function definitions *******************************************************/

static esp_rmaker_error_t esp_rmaker_device_set_params(_esp_rmaker_device_t *device, jparse_ctx_t *jptr, esp_rmaker_req_src_t src, __set_params_mode_t mode)
{
    _esp_rmaker_param_t *param = device->params;
    esp_rmaker_param_write_req_t *write_req = OSAL_CALLOC_EXTRAM(device->param_count, sizeof(esp_rmaker_param_write_req_t));
    if (!write_req) {
        OSAL_LOGE(TAG, "Could not allocate memory for set params.");
        return ESP_RMAKER_NO_MEM;
    }
    esp_rmaker_error_t err = ESP_RMAKER_OK;

    uint8_t num_param = 0;
    while (param) {
        /* Determine the search key based on the mode */
        const char *search_key = NULL;
        switch (mode) {
        case SET_PARAMS_MODE_BY_ID:
            search_key = param->id;
            break;
        case SET_PARAMS_MODE_BY_TYPE:
            search_key = param->type;
            break;
        default:
            OSAL_LOGE(TAG, "Invalid set params mode.");
            err = ESP_RMAKER_INVALID_ARG;
            goto set_params_free;
        }

        /* Parse the value from the JSON object */
        esp_rmaker_error_t parse_err = esp_rmaker_parse_val_from_object(jptr, search_key, param->val.type, &write_req[num_param].val);
        if (parse_err == ESP_RMAKER_NO_MEM) {
            err = ESP_RMAKER_NO_MEM;
            goto set_params_free;
        }
        if (parse_err == ESP_RMAKER_OK) {
            write_req[num_param++].param = (esp_rmaker_param_t *)param;
        }
        param = param->next;
    }
    OSAL_LOGI(TAG, "Found %d params in write request for %s", num_param, device->id);
    if (device->bulk_write_cb) {
        esp_rmaker_write_ctx_t ctx = {
            .src = src,
        };
        if (device->bulk_write_cb((esp_rmaker_device_t *)device, (const esp_rmaker_param_write_req_t *)write_req,
                                  num_param, device->priv_data, &ctx) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Remote update for device %s failed", device->id);
        }
    }
set_params_free:
    /* Free all values which are allocated on heap */
    for (int i = 0; i < num_param; i++) {
        if ((write_req[i].val.type == RMAKER_VAL_TYPE_STRING) || (write_req[i].val.type == RMAKER_VAL_TYPE_OBJECT ||
                (write_req[i].val.type == RMAKER_VAL_TYPE_ARRAY))) {
            if (write_req[i].val.val.s) {
                free(write_req[i].val.val.s);
            }
        }
    }
    if (write_req) {
        free(write_req);
    }
    return err;
}

static const char *__set_params_mode_to_string(__set_params_mode_t mode)
{
    switch (mode) {
    case SET_PARAMS_MODE_BY_ID:
        return "By ID";
    case SET_PARAMS_MODE_BY_TYPE:
        return "By Type";
    default:
        return "UNKNOWN";
    }
}

/* Public function definitions *******************************************************/

esp_rmaker_state_update_id_t esp_rmaker_state_update_id_create(const esp_rmaker_param_t *param)
{
    if (!param) {
        OSAL_LOGE(TAG, "Parameter cannot be NULL.");
        return NULL;
    }
    _esp_rmaker_param_t *p = (_esp_rmaker_param_t *)param;
    if (!p->parent) {
        OSAL_LOGE(TAG, "Parameter parent cannot be NULL.");
        return NULL;
    }
    _esp_rmaker_state_update_id_t *update_id = OSAL_CALLOC_EXTRAM(1, sizeof(_esp_rmaker_state_update_id_t));
    if (!update_id) {
        OSAL_LOGE(TAG, "Could not allocate memory for update ID.");
        return NULL;
    }
    update_id->device = p->parent;
    update_id->param = p;
    return (esp_rmaker_state_update_id_t)update_id;
}

esp_rmaker_error_t data_model_state_update_id_get_value(esp_rmaker_state_update_id_t update_id, esp_rmaker_param_val_t *val)
{
    if (!update_id || !val) {
        OSAL_LOGE(TAG, "Update ID or value cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    _esp_rmaker_state_update_id_t *u = (_esp_rmaker_state_update_id_t *)update_id;
    *val = u->param->val;
    return ESP_RMAKER_OK;
}

int data_model_state_update_id_compare(esp_rmaker_state_update_id_t update_id1, esp_rmaker_state_update_id_t update_id2)
{
    if (!update_id1 || !update_id2) {
        OSAL_LOGE(TAG, "Update ID cannot be NULL.");
        return -1;
    }
    _esp_rmaker_state_update_id_t *u1 = (_esp_rmaker_state_update_id_t *)update_id1;
    _esp_rmaker_state_update_id_t *u2 = (_esp_rmaker_state_update_id_t *)update_id2;

    // Pure pointer comparison is sufficient since the list is sorted by device.
    if (u1->device != u2->device) {
        return (int)(u1->device - u2->device);
    }
    return (int)(u1->param - u2->param);
}

esp_rmaker_error_t data_model_state_update_id_get_all(const esp_rmaker_node_t *node, esp_rmaker_state_update_id_t **update_ids, size_t *num_update_ids)
{
    if (!node || !update_ids || !num_update_ids) {
        OSAL_LOGE(TAG, "Node, update IDs, or number of update IDs cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }
    *update_ids = NULL;
    *num_update_ids = 0;

    esp_rmaker_state_update_id_t *update_ids_array = NULL;
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    esp_rmaker_node_lock(node);

    /* Sum param_count across all devices owned by ``node``. */
    size_t count = 0;
    _esp_rmaker_device_t *device = esp_rmaker_node_get_first_device(node);
    while (device) {
        count += device->param_count;
        device = device->next;
    }

    if (count == 0) {
        char node_name[RMAKER_THING_NAME_BUFFER_SIZE];
        esp_rmaker_node_resolve_thing_name(node, node_name, sizeof(node_name));
        OSAL_LOGD(TAG, "No update IDs to get for node %s.", node_name);
        err = ESP_RMAKER_OK;
        goto get_all_update_ids_err;
    }

    update_ids_array = OSAL_CALLOC_EXTRAM(count, sizeof(esp_rmaker_state_update_id_t));
    if (!update_ids_array) {
        OSAL_LOGE(TAG, "Could not allocate memory for %" PRIu32 " update ID(s).", (uint32_t)count);
        err = ESP_RMAKER_NO_MEM;
        goto get_all_update_ids_err;
    }

    size_t idx = 0;
    device = esp_rmaker_node_get_first_device(node);
    while (device) {
        _esp_rmaker_param_t *param = device->params;
        while (param) {
            esp_rmaker_state_update_id_t update_id = esp_rmaker_state_update_id_create((esp_rmaker_param_t *)param);
            if (!update_id) {
                OSAL_LOGE(TAG, "Could not create update ID for parameter %s.", param->id);
                err = ESP_RMAKER_NO_MEM;
                goto get_all_update_ids_err;
            }
            update_ids_array[idx++] = update_id;
            param = param->next;
        }
        device = device->next;
    }
    esp_rmaker_node_unlock(node);

    *update_ids = update_ids_array;
    *num_update_ids = count;
    return ESP_RMAKER_OK;

get_all_update_ids_err:
    esp_rmaker_node_unlock(node);
    if (update_ids_array) {
        free(update_ids_array);
    }
    return err;
}

void data_model_state_update_id_release(esp_rmaker_state_update_id_t update_id)
{
    _esp_rmaker_state_update_id_t *u = (_esp_rmaker_state_update_id_t *)update_id;
    if (u) {
        free(u);
    }
    return;
}

const esp_rmaker_node_t *data_model_state_update_id_to_node(esp_rmaker_state_update_id_t update_id)
{
    if (!update_id) {
        return NULL;
    }
    _esp_rmaker_state_update_id_t *u = (_esp_rmaker_state_update_id_t *)update_id;
    if (!u->device) {
        return NULL;
    }
    return u->device->parent;
}

bool data_model_state_update_info_has_indexed_updates(const esp_rmaker_state_update_info_t *update_info_list)
{
    const esp_rmaker_state_update_info_t *current = update_info_list;
    while (current) {
        /* Skip synthetic flag entries (e.g. ONLINE) - they are emitted
         * inline by state_changes.c and don't carry a parameter. */
        if (current->flags != RMAKER_STATE_UPDATE_FLAG_NONE) {
            current = current->next;
            continue;
        }
        _esp_rmaker_state_update_id_t *u = (_esp_rmaker_state_update_id_t *)current->update_id;
        if (u->param->prop_flags & PROP_FLAG_INDEXED) {
            return true;
        }
        current = current->next;
    }
    return false;
}

esp_rmaker_error_t data_model_state_generate_update_payload_json(const esp_rmaker_state_update_info_t *update_info_list, json_gen_str_t *p_jstr_named, json_gen_str_t *p_jstr_indexed)
{
    if (!update_info_list || !p_jstr_named) {
        OSAL_LOGE(TAG, "Update info list or named JSON string cannot be NULL.");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* List is sorted by device */
    const esp_rmaker_state_update_info_t *current = update_info_list;
    const _esp_rmaker_device_t *last_device = NULL, *last_device_indexed = NULL;
    while (current) {
        /* Skip synthetic flag entries - handled by state_changes.c. */
        if (current->flags != RMAKER_STATE_UPDATE_FLAG_NONE) {
            current = current->next;
            continue;
        }
        _esp_rmaker_state_update_id_t *u = (_esp_rmaker_state_update_id_t *)current->update_id;

        /* Check for indexed updates */
        bool is_indexed = u->param->prop_flags & PROP_FLAG_INDEXED && p_jstr_indexed != NULL;

        /* Start a new device object if the current device is different from the last */
        if (u->device != last_device) {
            if (is_indexed && last_device_indexed) {
                json_gen_pop_object(p_jstr_indexed);
            }
            if (last_device) {
                json_gen_pop_object(p_jstr_named);
            }
            if (is_indexed) {
                json_gen_push_object(p_jstr_indexed, u->device->id);
                last_device_indexed = u->device;
            }
            json_gen_push_object(p_jstr_named, u->device->id);
            last_device = u->device;
        }

        /* Add the parameter to the appropriate JSON string */
        esp_rmaker_report_value(&u->param->val, u->param->id, p_jstr_named);
        if (is_indexed) {
            esp_rmaker_report_value(&u->param->val, u->param->id, p_jstr_indexed);
        }

        /* Move to the next update */
        current = current->next;
    }

    /* Close the last device object */
    if (last_device) {
        json_gen_pop_object(p_jstr_named);
    }
    if (last_device_indexed) {
        json_gen_pop_object(p_jstr_indexed);
    }

    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __state_handle_update_payload_json(const esp_rmaker_node_t *node, const char *payload, size_t payload_len, esp_rmaker_req_src_t src, __set_params_mode_t mode)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    char node_name[RMAKER_THING_NAME_BUFFER_SIZE];
    esp_rmaker_node_resolve_thing_name(node, node_name, sizeof(node_name));
    OSAL_LOGI(TAG, "Received update payload:\n"
              "\tNode   : %s\n"
              "\tSource : %s\n"
              "\tMode   : %s\n"
              "\tPayload: %.*s",
              node_name,
              esp_rmaker_req_src_to_string(src),
              __set_params_mode_to_string(mode),
              (int) payload_len, payload);
    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, payload, payload_len) != 0) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    _esp_rmaker_device_t *device = esp_rmaker_node_get_first_device(node);
    while (device) {
        const char *search_key = NULL;
        switch (mode) {
        case SET_PARAMS_MODE_BY_ID:
            search_key = device->id;
            break;
        case SET_PARAMS_MODE_BY_TYPE:
            search_key = device->type;
            break;
        default:
            OSAL_LOGE(TAG, "Invalid set params mode.");
            return ESP_RMAKER_INVALID_ARG;
        }
        if (json_obj_get_object(&jctx, search_key) == 0) {
            /* Group control payloads (BY_TYPE) wrap the per-device-type params in a "params" envelope. */
            bool need_params_envelope = (mode == SET_PARAMS_MODE_BY_TYPE);
            bool entered_params_envelope = false;
            if (need_params_envelope) {
                if (json_obj_get_object(&jctx, "params") == 0) {
                    entered_params_envelope = true;
                } else {
                    /* No params envelope for this device type - nothing to apply. */
                    json_obj_leave_object(&jctx);
                    device = device->next;
                    continue;
                }
            }
            err = esp_rmaker_device_set_params(device, &jctx, src, mode);
            if (entered_params_envelope) {
                json_obj_leave_object(&jctx);
            }
            json_obj_leave_object(&jctx);
            if (err != ESP_RMAKER_OK) {
                break;
            }
        }
        device = device->next;
    }
    json_parse_end(&jctx);

    /* Report scheduling is left to the callbacks */
    return err;
}

esp_rmaker_error_t data_model_state_handle_update_payload_json(const esp_rmaker_node_t *node, const char *payload, size_t payload_len, esp_rmaker_req_src_t src)
{
    return __state_handle_update_payload_json(node, payload, payload_len, src, SET_PARAMS_MODE_BY_ID);
}

esp_rmaker_error_t data_model_state_handle_update_payload_json_group(const esp_rmaker_node_t *node, const char *payload, size_t payload_len, esp_rmaker_req_src_t src)
{
    return __state_handle_update_payload_json(node, payload, payload_len, src, SET_PARAMS_MODE_BY_TYPE);
}

esp_rmaker_val_type_t data_model_state_expected_val_type_from_update_id(esp_rmaker_state_update_id_t update_id)
{
    if (!update_id) {
        OSAL_LOGE(TAG, "Update ID cannot be NULL.");
        return RMAKER_VAL_TYPE_INVALID;
    }

    _esp_rmaker_state_update_id_t *u = (_esp_rmaker_state_update_id_t *)update_id;
    return u->param->val.type;
}
