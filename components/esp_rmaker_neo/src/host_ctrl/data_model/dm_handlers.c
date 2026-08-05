/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dm_handlers.c
 * @brief Data model handlers for the host control.
 */

/* Includes ****************************************************************/

/* Declarations */
#include "data_model/dm_handlers.h"
#include "data_model/dm_param_helpers.h"

/* Standard includes */
#include <stdint.h>
#include <stdlib.h>

/* Constants */
#include "esp_rmaker_host_ctrl_constants.h"

/* RMNG includes */
#include "esp_rmaker_standard_services.h"
#include "host_ctrl_services.h"
#include "node_internal.h"
#include "host_ctrl_processing.h"

/* Platform common includes */
#include "osal_log.h"

/* Pre-processor definitions **************************************************/

#define RMAKER_TOTAL_SERVICE_COUNT (RMAKER_STANDARD_SERVICE_COUNT + RMAKER_HOST_CTRL_SERVICE_COUNT)

/* Private variables ************************************************************/

static const char *TAG = "rmng_hc_dm_handlers";

/**
 * @brief The standard service registry.
 */
static struct {
    esp_rmaker_standard_service_disable_func disable_funcs[RMAKER_TOTAL_SERVICE_COUNT];
    uint8_t registered;
    uint8_t max_registered;
} __esp_rmaker_host_ctrl_standard_service_registry = {
    .disable_funcs = {NULL},
    .registered = 0,
    .max_registered = RMAKER_TOTAL_SERVICE_COUNT,
};

/* Private function declarations *********************************************/

/* --- Standard services --- */

/**
 * @brief Register a standard service.
 * @param[in] disable_func The disable function.
 */
static void __register_standard_service_disable_func(esp_rmaker_standard_service_disable_func disable_func)
{
    if (__esp_rmaker_host_ctrl_standard_service_registry.registered >= __esp_rmaker_host_ctrl_standard_service_registry.max_registered) {
        OSAL_LOGE(TAG, "Standard service registry is full");
        return;
    }
    __esp_rmaker_host_ctrl_standard_service_registry.disable_funcs[__esp_rmaker_host_ctrl_standard_service_registry.registered++] = disable_func;
}

/**
 * @brief Disable all standard services.
 */
static void __disable_all_standard_services(void)
{
    for (uint8_t i = 0; i < __esp_rmaker_host_ctrl_standard_service_registry.registered; i++) {
        __esp_rmaker_host_ctrl_standard_service_registry.disable_funcs[i]();
        __esp_rmaker_host_ctrl_standard_service_registry.disable_funcs[i] = NULL;
    }
    __esp_rmaker_host_ctrl_standard_service_registry.registered = 0;
}

/* --- Handlers --- */

/**
 * @brief Adds standard services to the node model.
 * @note The buffer is in the format: "<service_character><service_character>...|".
 */
static void __handle_add_services(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Removes a param from the node model. Not currently supported.
 */
static void __handle_remove_param(uint8_t *buffer, size_t buffer_length);


/* Private functions **********************************************************/

/* --- Standard services --- */

/* --- Responses --- */

esp_rmaker_error_t esp_rmaker_host_ctrl_device_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param, const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    char *device_id = esp_rmaker_device_get_id(device);
    char *param_id = esp_rmaker_param_get_id(param);
    switch (val.type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        OSAL_LOGI(TAG, "Device write callback called for device: %s | %s -> %s", device_id, param_id, val.val.b ? "true" : "false");
        break;
    case RMAKER_VAL_TYPE_INTEGER:
        OSAL_LOGI(TAG, "Device write callback called for device: %s | %s -> %d", device_id, param_id, val.val.i);
        break;
    case RMAKER_VAL_TYPE_FLOAT:
        OSAL_LOGI(TAG, "Device write callback called for device: %s | %s -> %f", device_id, param_id, val.val.f);
        break;
    case RMAKER_VAL_TYPE_STRING:
    case RMAKER_VAL_TYPE_OBJECT:
    case RMAKER_VAL_TYPE_ARRAY:
        OSAL_LOGI(TAG, "Device write callback called for device: %s | %s -> %s", device_id, param_id, val.val.s);
        break;
    default:
        OSAL_LOGE(TAG, "Device write callback called for device: %s | %s with unknown type: %d", device_id, param_id, val.type);
    }

    esp_rmaker_param_update(param, val);
    return ESP_RMAKER_OK;
}

/* --- Handlers --- */

void esp_rmaker_host_ctrl_handle_add_param(uint8_t *payload, size_t payload_length)
{
    esp_rmaker_host_ctrl_handle_add_param_with_hook(payload, payload_length, NULL, NULL);
}

void esp_rmaker_host_ctrl_handle_add_param_with_hook(
    uint8_t *buffer, size_t buffer_length,
    esp_rmaker_host_ctrl_resolve_node_t resolve_node,
    void *resolve_node_priv)
{
    enum { EXPECTED_DELIMITERS_COUNT = 10 };
    char *delimiters[EXPECTED_DELIMITERS_COUNT];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, EXPECTED_DELIMITERS_COUNT)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected %d for add param", EXPECTED_DELIMITERS_COUNT);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Resolve the target node. Bridge path supplies a child node;
     * self path leaves the hook NULL and falls back to the self node. */
    esp_rmaker_node_t *node = resolve_node ? resolve_node(resolve_node_priv)
                              : (esp_rmaker_node_t *)esp_rmaker_get_node();
    if (!node) {
        OSAL_LOGE(TAG, "add_param: target node unresolved");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    /* Check for existing device */
    char *device_id = (char *) buffer;
    esp_rmaker_device_t *device = esp_rmaker_node_get_device_by_id(node, device_id);
    if (device == NULL) {
        // Make new device
        char *device_type = delimiters[0] + 1;
        device = esp_rmaker_device_create(device_id, device_type, NULL);
        if (device == NULL) {
            OSAL_LOGE(TAG, "Failed to create device: %s", device_id);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
            return;
        }
        if (esp_rmaker_device_add_cb(device, esp_rmaker_host_ctrl_device_write_cb, NULL) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to add device write callback: %s", device_id);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
            return;
        }
        if (esp_rmaker_node_add_device(node, device) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to add device: %s", device_id);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
            return;
        }
    }

    /* Check for existing param */
    char *param_id = delimiters[1] + 1;
    esp_rmaker_param_t *param = esp_rmaker_device_get_param_by_id(device, param_id);
    if (param != NULL) {
        OSAL_LOGE(TAG, "Param already exists: %s", param_id);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Make new param */

    // Check param type
    char *param_type = delimiters[2] + 1;
    if (delimiters[3] - param_type < 1) {
        OSAL_LOGE(TAG, "Param type is empty");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    // Check param value
    char *param_value_start = delimiters[4] + 1, *param_value_end = delimiters[5];
    esp_rmaker_param_val_t param_value;
    if (!esp_rmaker_host_ctrl_get_param_value(param_value_start, param_value_end, &param_value)) {
        OSAL_LOGE(TAG, "Invalid param value: %s", param_value_start);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    // Check param properties
    char *param_properties_start = delimiters[8] + 1, *param_properties_end = delimiters[9];
    uint8_t param_prop_flags = esp_rmaker_host_ctrl_param_prop_flags_from_chars(param_properties_start, param_properties_end);

    // Create param
    param = esp_rmaker_param_create(param_id, param_type, param_value, param_prop_flags);
    if (param == NULL) {
        OSAL_LOGE(TAG, "Failed to create param: %s", param_id);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    // Check param UI type
    char *param_ui_type = delimiters[3] + 1;
    if (delimiters[4] - param_ui_type > 0) {
        esp_rmaker_error_t err = esp_rmaker_param_add_ui_type(param, param_ui_type);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to add UI type '%s' to param: %s", param_ui_type, param_id);
            esp_rmaker_param_delete(param);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
            return;
        }
    }

    // Add bounds
    char *min_bound_start = delimiters[5] + 1, *min_bound_end = delimiters[6];
    char *max_bound_start = delimiters[6] + 1, *max_bound_end = delimiters[7];
    char *step_start = delimiters[7] + 1, *step_end = delimiters[8];
    if (min_bound_end - min_bound_start > 0 &&
            max_bound_end - max_bound_start > 0 &&
            step_end - step_start > 0) {
        esp_rmaker_val_type_t val_type = param_value.type;
        esp_rmaker_param_val_t min_bound, max_bound, step;
        min_bound.type = val_type;
        max_bound.type = val_type;
        step.type = val_type;

        if (val_type == RMAKER_VAL_TYPE_INTEGER) {
            min_bound.val.i = atoi(min_bound_start);
            max_bound.val.i = atoi(max_bound_start);
            step.val.i = atoi(step_start);
        } else if (val_type == RMAKER_VAL_TYPE_FLOAT) {
            min_bound.val.f = atof(min_bound_start);
            max_bound.val.f = atof(max_bound_start);
            step.val.f = atof(step_start);
        }

        if (esp_rmaker_param_add_bounds(param, min_bound, max_bound, step) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to add bounds to param: %s", param_id);
            esp_rmaker_param_delete(param);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
            return;
        }
    }

    // Add param to device
    if (esp_rmaker_device_add_param(device, param) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add param to device: %s", param_id);
        esp_rmaker_param_delete(param);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    // Send response
    OSAL_LOGI(TAG, "Param added: %s: type: %s -> device: %s", param_id, param_type, device_id);
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_add_services(uint8_t *buffer, size_t buffer_length)
{
    /* Get delimiters */
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for add services");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Add services */
    char *c_service = (char *) buffer;
    while (c_service < delimiters[0]) {
        esp_rmaker_error_t err = ESP_RMAKER_OK;
        switch (*c_service) {
        case RMAKER_HOST_CTRL_SERVICE_CHAR_TIMEZONE:
            err = esp_rmaker_timezone_service_enable();
            if (err == ESP_RMAKER_OK) {
                __register_standard_service_disable_func(esp_rmaker_timezone_service_disable);
            }
            break;
        case RMAKER_HOST_CTRL_SERVICE_CHAR_LATENCY:
            err = esp_rmaker_host_ctrl_latency_service_enable();
            if (err == ESP_RMAKER_OK) {
                __register_standard_service_disable_func(esp_rmaker_host_ctrl_latency_service_disable);
            }
            break;
        case RMAKER_HOST_CTRL_SERVICE_CHAR_ON_NETWORK_CHAL_RESP:
            /* Challenge-response endpoint only (no local control endpoints) */
            err = esp_rmaker_chal_resp_service_enable();
            if (esp_rmaker_chal_resp_service_is_enabled()) {
                __register_standard_service_disable_func(esp_rmaker_chal_resp_service_disable);
            }
            break;
        case RMAKER_HOST_CTRL_SERVICE_CHAR_LOCAL_CTRL:
            err = esp_rmaker_local_ctrl_service_enable();
            if (err == ESP_RMAKER_OK) {
                __register_standard_service_disable_func(esp_rmaker_local_ctrl_service_disable);
            }
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE
            err = esp_rmaker_chal_resp_service_enable();
            if (esp_rmaker_chal_resp_service_is_enabled()) {
                __register_standard_service_disable_func(esp_rmaker_chal_resp_service_disable);
            }
#endif /* CONFIG_ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE */
            break;
        default:
            OSAL_LOGE(TAG, "Invalid service character: %c", *c_service);
            err = ESP_RMAKER_INVALID_ARG;
            break;
        }
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to add service: %c", *c_service);
            esp_rmaker_host_ctrl_send_response(err == ESP_RMAKER_INVALID_ARG
                                               ? RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID
                                               : RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR
                                              );
            return;
        }
        c_service++;
    }

    // Send response
    OSAL_LOGI(TAG, "Services added: %s", (char *) buffer);
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_remove_param(uint8_t *buffer, size_t buffer_length)
{
    /* There is no way to remove a param from the node model */
    OSAL_LOGE(TAG, "Remove param command is not supported yet");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
    return;

    // /* Get node */
    // const esp_rmaker_node_t *node = esp_rmaker_get_node();

    // /* Get delimiters */
    // char *delimiters[2];
    // if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 2))
    // {
    //     OSAL_LOGE(TAG, "Invalid delimiters count: expected 2 for remove param");
    //     esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
    //     return;
    // }
    // /* Find device */
    // char *device_id = (char *) buffer;
    // esp_rmaker_device_t *device = esp_rmaker_node_get_device_by_id(node, device_id);
    // if (device == NULL)
    // {
    //     OSAL_LOGE(TAG, "Device not found: %s", device_id);
    //     esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
    //     return;
    // }

    // /* Find param */
    // char *param_id = delimiters[0] + 1;
    // esp_rmaker_param_t *param = esp_rmaker_device_get_param_by_id(device, param_id);
    // if (param == NULL)
    // {
    //     OSAL_LOGE(TAG, "Param not found: %s", param_id);
    //     esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
    //     return;
    // }

    // /* Remove param */
    // esp_rmaker_error_t err;
    // err = esp_rmaker_device_remove_param(device, param);
    // if (err != ESP_RMAKER_OK)
    // {
    //     OSAL_LOGE(TAG, "Failed to remove param: %s", param_id);
    //     esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
    //     return;
    // }

    // /* Delete param */
    // err = esp_rmaker_param_delete(param);
    // if (err != ESP_RMAKER_OK)
    // {
    //     OSAL_LOGE(TAG, "Failed to delete param: %s", param_id);
    //     esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
    //     return;
    // }

    // // Send response
    // OSAL_LOGI(TAG, "Param removed: %s: device: %s", param_id, device_id);
    // esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

void esp_rmaker_host_ctrl_handle_update_param(uint8_t *buffer, size_t buffer_length)
{
    esp_rmaker_host_ctrl_handle_update_param_with_hook(buffer, buffer_length, NULL, NULL);
}

void esp_rmaker_host_ctrl_handle_update_param_with_hook(uint8_t *buffer, size_t buffer_length,
        esp_rmaker_host_ctrl_resolve_node_t resolve_node, void *resolve_node_priv)
{
    /* Resolve target node - bridge supplies a child resolver; self path
     * leaves the hook NULL and falls back to the self node. */
    const esp_rmaker_node_t *node = resolve_node ? resolve_node(resolve_node_priv)
                                    : esp_rmaker_get_node();
    if (!node) {
        OSAL_LOGE(TAG, "update_param: target node unresolved");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    /* Get delimiters */
    char *delimiters[3];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 3)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 3 for update param");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Find device */
    char *device_id = (char *) buffer;
    esp_rmaker_device_t *device = esp_rmaker_node_get_device_by_id(node, device_id);
    if (device == NULL) {
        OSAL_LOGE(TAG, "Device not found: %s", device_id);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return;
    }

    /* Get param name */
    char *param_id = delimiters[0] + 1;
    esp_rmaker_param_t *param = esp_rmaker_device_get_param_by_id(device, param_id);
    if (param == NULL) {
        OSAL_LOGE(TAG, "Param not found: %s", param_id);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return;
    }

    /* Get param value */
    char *param_value_start = delimiters[1] + 1, *param_value_end = delimiters[2];
    esp_rmaker_param_val_t param_value;
    if (!esp_rmaker_host_ctrl_get_param_value(param_value_start, param_value_end, &param_value)) {
        OSAL_LOGE(TAG, "Invalid param value: %s", param_value_start);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Update param */
    esp_rmaker_error_t err = esp_rmaker_param_update(param, param_value);
    if (err == ESP_RMAKER_INVALID_ARG) {
        OSAL_LOGE(TAG, "Invalid param value for param: %s", param_id);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update param: %s", param_id);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    // Send response
    OSAL_LOGI(TAG, "Param updated: %s: %s", param_id, param_value_start);
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

void esp_rmaker_host_ctrl_handle_get_param(uint8_t *buffer, size_t buffer_length)
{
    esp_rmaker_host_ctrl_handle_get_param_with_hook(buffer, buffer_length, NULL, NULL);
}

void esp_rmaker_host_ctrl_handle_get_param_with_hook(uint8_t *buffer, size_t buffer_length,
        esp_rmaker_host_ctrl_resolve_node_t resolve_node, void *resolve_node_priv)
{
    /* Get delimiters */
    char *delimiters[2];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 2)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 2 for get param");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Resolve target node - bridge supplies a child resolver; self path
     * leaves the hook NULL and falls back to the self node. */
    const esp_rmaker_node_t *node = resolve_node ? resolve_node(resolve_node_priv)
                                    : esp_rmaker_get_node();
    if (!node) {
        OSAL_LOGE(TAG, "get_param: target node unresolved");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    /* Get device */
    char *device_id = (char *) buffer;
    esp_rmaker_device_t *device = esp_rmaker_node_get_device_by_id(node, device_id);
    if (device == NULL) {
        OSAL_LOGE(TAG, "Device not found: %s", device_id);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return;
    }

    /* Get param */
    char *param_id = delimiters[0] + 1;
    esp_rmaker_param_t *param = esp_rmaker_device_get_param_by_id(device, param_id);
    if (param == NULL) {
        OSAL_LOGE(TAG, "Param not found: %s", param_id);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return;
    }

    /* Get param value */
    esp_rmaker_param_val_t *param_value = esp_rmaker_param_get_val(param);
    if (param_value == NULL) {
        OSAL_LOGE(TAG, "Failed to get param value: %s", param_id);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    /* Format param value */
    int param_value_length = esp_rmaker_host_ctrl_format_param_value(param_value, NULL, 0);
    if (param_value_length <= 0) {
        OSAL_LOGE(TAG, "Failed to format param value: %s", param_id);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    uint8_t param_properties = ((_esp_rmaker_param_t *) param)->prop_flags;
    int param_properties_length = esp_rmaker_host_ctrl_format_param_props(param_properties, NULL, 0);
    if (param_properties_length < 0) {
        OSAL_LOGE(TAG, "Failed to format param properties: %s", param_id);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    int payload_length = param_value_length + param_properties_length + 1; // +1 for delimiter
    char payload[payload_length];
    param_value_length = esp_rmaker_host_ctrl_format_param_value(param_value, payload, param_value_length + 1);
    if (param_value_length <= 0) {
        OSAL_LOGE(TAG, "Failed to format param value: %s", param_id);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }
    payload[param_value_length] = RMAKER_HOST_CTRL_DELIMITER_CHAR;

    if (param_properties_length > 0) {
        param_properties_length = esp_rmaker_host_ctrl_format_param_props(param_properties, payload + param_value_length + 1, param_properties_length);
        if (param_properties_length < 0) {
            OSAL_LOGE(TAG, "Failed to format param properties: %s", param_id);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
            return;
        }
    }

    /* Send response */
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, payload, payload_length);
}

/* --- Processing --- */

int esp_rmaker_host_ctrl_format_param_props(uint8_t param_properties, char *buffer, size_t buffer_length)
{
    if (buffer == NULL) {
        // Counting set bits
        int count = 0;
        while (param_properties > 0) {
            count += param_properties & 1;
            param_properties >>= 1;
        }
        return count;
    }

    bool failed_any = false;
    if (param_properties & PROP_FLAG_READ) {
        failed_any |= !esp_rmaker_host_ctrl_add_flag_c_to_buffer(RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_READ, &buffer, &buffer_length);
    }
    if (param_properties & PROP_FLAG_WRITE) {
        failed_any |= !esp_rmaker_host_ctrl_add_flag_c_to_buffer(RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_WRITE, &buffer, &buffer_length);
    }
    if (param_properties & PROP_FLAG_TIME_SERIES) {
        failed_any |= !esp_rmaker_host_ctrl_add_flag_c_to_buffer(RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_TIME_SERIES, &buffer, &buffer_length);
    }
    if (param_properties & PROP_FLAG_TS_CUMULATIVE) {
        failed_any |= !esp_rmaker_host_ctrl_add_flag_c_to_buffer(RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_TS_CUMULATIVE, &buffer, &buffer_length);
    }
    if (param_properties & PROP_FLAG_INDEXED) {
        failed_any |= !esp_rmaker_host_ctrl_add_flag_c_to_buffer(RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_INDEXED, &buffer, &buffer_length);
    }
    if (param_properties & PROP_FLAG_PERSIST) {
        failed_any |= !esp_rmaker_host_ctrl_add_flag_c_to_buffer(RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_PERSIST, &buffer, &buffer_length);
    }

    if (failed_any) {
        return -1;
    }

    return buffer_length;
}

uint8_t esp_rmaker_host_ctrl_param_prop_flags_from_chars(const char *param_properties_start, const char *param_properties_end)
{
    uint8_t param_prop_flags = 0;
    for (const char *p = param_properties_start; p < param_properties_end; p++) {
        switch (*p) {
        case RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_READ:
            param_prop_flags |= PROP_FLAG_READ;
            break;
        case RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_WRITE:
            param_prop_flags |= PROP_FLAG_WRITE;
            break;
        case RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_TIME_SERIES:
            param_prop_flags |= PROP_FLAG_TIME_SERIES;
            break;
        case RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_TS_CUMULATIVE:
            param_prop_flags |= PROP_FLAG_TS_CUMULATIVE;
            break;
        case RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_INDEXED:
            param_prop_flags |= PROP_FLAG_INDEXED;
            break;
        case RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_PERSIST:
            param_prop_flags |= PROP_FLAG_PERSIST;
            break;
        default:
            OSAL_LOGE(TAG, "Invalid property character encountered: %c", *p);
            break;
        }
    }

    return param_prop_flags;
}

/* Public functions **********************************************************/

void esp_rmaker_host_ctrl_data_model_on_reset(void)
{
    __disable_all_standard_services();

    /* Clear the stored values for the node */
    const esp_rmaker_node_t *node = esp_rmaker_get_node();
    esp_rmaker_error_t err = esp_rmaker_node_clear_stored_values(node);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to clear the stored values for the node");
    }
}

bool esp_rmaker_host_ctrl_data_model_handle_buffer(uint8_t *buffer, size_t buffer_length)
{
    char command = buffer[0];
    if (buffer_length <= 1) {
        /* Not intended command */
        return false;
    }

    /* Handle command */
    esp_rmaker_host_ctrl_payload_handler_t handler = NULL;
    uint8_t *payload = buffer + 1;
    size_t payload_length = buffer_length - 1;
    char payload_type;
    switch (command) {
    case RMAKER_HOST_CTRL_COMMAND_CHAR_ADD:
        payload_type = payload[0];
        payload++; payload_length--;
        if (payload_type == RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_PARAM) {
            handler = esp_rmaker_host_ctrl_handle_add_param;
        } else if (payload_type == RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_SERVICES) {
            handler = __handle_add_services;
        }
        break;
    case RMAKER_HOST_CTRL_COMMAND_CHAR_REMOVE:
        payload_type = payload[0];
        payload++; payload_length--;
        if (payload_type == RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_PARAM) {
            handler = __handle_remove_param;
        }
        break;
    case RMAKER_HOST_CTRL_COMMAND_CHAR_UPDATE:
        payload_type = payload[0];
        payload++; payload_length--;
        if (payload_type == RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_PARAM) {
            handler = esp_rmaker_host_ctrl_handle_update_param;
        }
        break;
    case RMAKER_HOST_CTRL_COMMAND_CHAR_GET:
        payload_type = payload[0];
        payload++; payload_length--;
        if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_PARAM) {
            handler = esp_rmaker_host_ctrl_handle_get_param;
        }
        break;
    default:
        /* Not handled by data model handler */
        return false;
    }

    if (handler == NULL) {
        /* Not handled by data model handler */
        return false;
    }

    handler(payload, payload_length);
    return true;
}
