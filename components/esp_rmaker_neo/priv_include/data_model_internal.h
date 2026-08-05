/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file data_model_internal.h
 * @brief Internal data model declarations.
 */

#ifndef __DATA_MODEL_INTERNAL_H__
#define __DATA_MODEL_INTERNAL_H__

/* Includes *******************************************************/

/* Standard includes. */
#include <stdint.h>
#include <stdbool.h>

/* Data model includes. */
#include "esp_rmaker_data_model.h"

/* Node includes. */
#include "esp_rmaker_node.h"

/* State includes. */
#include "esp_rmaker_state.h"
#include "network/state_changes.h"

/* Structures *******************************************************/

/**
 * @brief Node device parameter.
 */
struct esp_rmaker_param {
    char *id;
    char *type;
    char *ui_type;
    uint8_t prop_flags; /* esp_rmaker_param_property_flags_t */
    uint16_t ttl_days;  /* TTL in days for simple time series data */
    esp_rmaker_param_val_t val;
    esp_rmaker_param_bounds_t *bounds;
    struct esp_rmaker_device *parent;
    struct esp_rmaker_param *next;
};
typedef struct esp_rmaker_param _esp_rmaker_param_t;

/**
 * @brief Node device.
 */
struct esp_rmaker_device {
    char *id;
    char *type;
    uint8_t param_count;
    esp_rmaker_device_write_cb_t write_cb;
    esp_rmaker_device_read_cb_t read_cb;
    esp_rmaker_device_bulk_write_cb_t bulk_write_cb;
    esp_rmaker_device_bulk_read_cb_t bulk_read_cb;
    void *priv_data;
    bool is_service;
    esp_rmaker_attr_t *attributes;
    _esp_rmaker_param_t *params;
    _esp_rmaker_param_t *primary;
    const esp_rmaker_node_t *parent;
    struct esp_rmaker_device *next;
};
typedef struct esp_rmaker_device _esp_rmaker_device_t;

/**
 * @brief Internal update ID.
 */
typedef struct {
    const _esp_rmaker_device_t *device;
    const _esp_rmaker_param_t *param;
} _esp_rmaker_state_update_id_t;

/* Public functions *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the first device in the node.
 *
 * @param[in] node Node handle.
 *
 * @return Pointer to the first device.
 */
_esp_rmaker_device_t *esp_rmaker_node_get_first_device(const esp_rmaker_node_t *node);

/**
 * @brief Delete the parameter.
 *
 * @param[in] param Pointer to the parameter.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_param_delete(const esp_rmaker_param_t *param);

/**
 * @brief Create a state update ID for a parameter.
 *
 * @param[in] param Pointer to the parameter.
 *
 * @return The state update ID.
 */
esp_rmaker_state_update_id_t esp_rmaker_state_update_id_create(const esp_rmaker_param_t *param);

/**
 * @brief Get the stored value of the parameter from NVS.
 *
 * @param[in] param Pointer to the parameter.
 * @param[out] val Pointer to the value to get.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_param_get_stored_value(_esp_rmaker_param_t *param, esp_rmaker_param_val_t *val);

/**
 * @brief Store the value of the parameter in NVS.
 *
 * @param[in] param Pointer to the parameter.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_param_store_value(_esp_rmaker_param_t *param);

/* Data model operations (state changes - update ID) **************************/

/** Get the value of an update ID. Does not copy the value. */
esp_rmaker_error_t data_model_state_update_id_get_value(esp_rmaker_state_update_id_t update_id, esp_rmaker_param_val_t *val);

/** Compare two update IDs. */
int data_model_state_update_id_compare(esp_rmaker_state_update_id_t update_id1, esp_rmaker_state_update_id_t update_id2);

/**
 * @brief Get all update IDs owned by the given node.
 *
 * Enumerates only the parameters attached to ``node``.
 */
esp_rmaker_error_t data_model_state_update_id_get_all(const esp_rmaker_node_t *node, esp_rmaker_state_update_id_t **update_ids, size_t *num_update_ids);

/** Release an update ID. Renders the update ID invalid. */
void data_model_state_update_id_release(esp_rmaker_state_update_id_t update_id);

/**
 * @brief Resolve the node an update ID's owning device belongs to.
 *
 * Returns the owning node handle (self or a bridge child). Used by the
 * state report pipeline to route the update into the correct per-node
 * update list. Returns NULL if the update ID is no longer resolvable.
 */
const esp_rmaker_node_t *data_model_state_update_id_to_node(esp_rmaker_state_update_id_t update_id);

/* Data model operations (state changes - update payload JSON) ****************/

/** Check if the update info has any indexed updates. */
bool data_model_state_update_info_has_indexed_updates(const esp_rmaker_state_update_info_t *update_info_list);

/** Generate a JSON object to update the RainMaker Neo cloud with. */
esp_rmaker_error_t data_model_state_generate_update_payload_json(const esp_rmaker_state_update_info_t *update_info_list, json_gen_str_t *p_jstr_named, json_gen_str_t *p_jstr_indexed);

/** Handle an inbound params payload addressed to ``node`` (devices matched by ``id``). */
esp_rmaker_error_t data_model_state_handle_update_payload_json(const esp_rmaker_node_t *node, const char *update_payload_json, size_t update_payload_json_len, esp_rmaker_req_src_t src);

/** Handle an inbound params payload addressed to ``node``, matching devices by
 *  ``type`` instead of ``id``; for group control fan-out, the dispatcher
 *  invokes it once per node. */
esp_rmaker_error_t data_model_state_handle_update_payload_json_group(const esp_rmaker_node_t *node, const char *update_payload_json, size_t update_payload_json_len, esp_rmaker_req_src_t src);

/* Data model operations (automation) ******************************************/

/** Get the expected value type from an update ID. */
esp_rmaker_val_type_t data_model_state_expected_val_type_from_update_id(esp_rmaker_state_update_id_t update_id);

/* Data model operations (timeseries) ******************************************/

/** Check if timeseries is enabled for an update ID. */
esp_rmaker_error_t data_model_timeseries_enabled_for_update_id(const esp_rmaker_state_update_id_t update_id, bool *enabled, bool *is_cumulative);

/* Data model operations (path) ************************************************/

/** Build a path string from an update ID. Returns malloc'd string; caller frees. */
char *data_model_update_id_to_path(const esp_rmaker_state_update_id_t update_id);

/** Parse a path string into an update ID. Returns NULL on failure; caller
 *  releases via data_model_state_update_id_release(). */
esp_rmaker_state_update_id_t data_model_path_to_update_id(const char *path);

/** Parse a path string into an update ID, scoped to ``node``'s device tree.
 *  Returns NULL on failure. */
esp_rmaker_state_update_id_t data_model_path_to_update_id_for_node(const esp_rmaker_node_t *node, const char *path);

/* Data model operations (node) ************************************************/

/** Get the data model type string reported in the node config. */
const char *data_model_node_get_data_model_type(void);

/** Write the data model config to the JSON string for ``node``. */
esp_rmaker_error_t data_model_node_write_data_model_config(const esp_rmaker_node_t *node, json_gen_str_t *jptr);

/** Get the current state of the node parameters as a JSON string. */
char *data_model_node_get_node_params(void);

#ifdef __cplusplus
}
#endif

#endif /* __DATA_MODEL_INTERNAL_H__ */
