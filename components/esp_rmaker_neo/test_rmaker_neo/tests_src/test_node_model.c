/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "esp_rmaker_flow.h"
#include "node_internal.h"
#include "data_model_internal.h"
#include "local_config.h"
#include "osal_storage.h"
#include "constants/nvs.h"
#include "network/state_changes.h"
#include "osal_sysinfo.h"
#include "json_parser.h"

static const char *test_node_name = "test_node";
static const char *test_node_type = "test_type";
static esp_rmaker_node_t *__setup(void)
{
    esp_rmaker_config_t config = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, test_node_name, test_node_type);
    TEST_ASSERT_NOT_NULL_MESSAGE(node, "Failed to create node");
    return node;
}

static void __teardown(esp_rmaker_node_t *node)
{
    /* Clear stored values for all devices before deinit to ensure test isolation */
    esp_rmaker_node_clear_stored_values(node);
    esp_rmaker_node_deinit(node);
}

static int __count_devices(esp_rmaker_node_t *node)
{
    int count = 0;
    _esp_rmaker_device_t *device = ((_esp_rmaker_node_t *)node)->devices;
    while (device) {
        count++;
        device = device->next;
    }
    return count;
}

static int __count_params(esp_rmaker_device_t *device)
{
    int count = 0;
    _esp_rmaker_param_t *param = ((_esp_rmaker_device_t *)device)->params;
    while (param) {
        count++;
        param = param->next;
    }
    return count;
}

static osal_err_t __clear_nvs(const char *device_id)
{
    osal_storage_handle_t handle;
    osal_err_t nvs_err;
    nvs_err = osal_storage_open(RMAKER_NVS_PART_NAME, device_id, OSAL_STORAGE_OPEN_READWRITE, &handle);
    if (nvs_err == OSAL_ERR_NVS_NAMESPACE_NOT_FOUND) {
        return OSAL_ERR_OK;
    }
    if (nvs_err != OSAL_ERR_OK) {
        return nvs_err;
    }
    nvs_err = osal_storage_erase_all(handle);
    if (nvs_err != OSAL_ERR_OK) {
        return nvs_err;
    }
    nvs_err = osal_storage_commit(handle);
    if (nvs_err != OSAL_ERR_OK) {
        return nvs_err;
    }
    nvs_err = osal_storage_close(handle);
    if (nvs_err != OSAL_ERR_OK) {
        return nvs_err;
    }
    return OSAL_ERR_OK;
}

void test_node_model_basic(void)
{
    esp_rmaker_node_t *node = __setup();

    esp_rmaker_error_t err;
    char *err_msg = NULL;

    /* Create devices */
    esp_rmaker_device_t *device1 = esp_rmaker_device_create("td1", "test_type", NULL);
    if (device1 == NULL) {
        err_msg = "Failed to create device";
        goto node_model_basic_end;
    }

    esp_rmaker_device_t *device2 = esp_rmaker_device_create("td2", "test_type", NULL);
    if (device2 == NULL) {
        err_msg = "Failed to create device";
        goto node_model_basic_end;
    }

    esp_rmaker_device_t *service1 = esp_rmaker_service_create("ts1", "test_type", NULL);
    if (service1 == NULL) {
        err_msg = "Failed to create service";
        goto node_model_basic_end;
    }

    /* Make some parameters */
    esp_rmaker_param_t *param_int = esp_rmaker_param_create("tp_int", "int_type", esp_rmaker_int(10), PROP_FLAG_READ);
    if (param_int == NULL) {
        err_msg = "Failed to create integer parameter";
        goto node_model_basic_end;
    }

    esp_rmaker_param_t *param_str = esp_rmaker_param_create("tp_str", "str_type", esp_rmaker_str("test_str"), PROP_FLAG_READ);
    if (param_str == NULL) {
        err_msg = "Failed to create string parameter";
        goto node_model_basic_end;
    }

    esp_rmaker_param_t *param_float = esp_rmaker_param_create("tp_float", "float_type", esp_rmaker_float(20.0), PROP_FLAG_READ);
    if (param_float == NULL) {
        err_msg = "Failed to create float parameter";
        goto node_model_basic_end;
    }

    esp_rmaker_param_t *param_bool = esp_rmaker_param_create("tp_bool", "bool_type", esp_rmaker_bool(true), PROP_FLAG_READ);
    if (param_bool == NULL) {
        err_msg = "Failed to create boolean parameter";
        goto node_model_basic_end;
    }

    /* Add parameters to the devices */
    err = esp_rmaker_device_add_param(device1, param_int);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add integer parameter";
        goto node_model_basic_end;
    }
    /* Try adding a parameter that already exists */
    err = esp_rmaker_device_add_param(device1, param_int);
    if (err == ESP_RMAKER_OK) {
        err_msg = "Added integer parameter that already exists";
        goto node_model_basic_end;
    }

    // Try adding device to node, then parameter to device
    err = esp_rmaker_node_add_device(node, device1);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add device1 to node";
        goto node_model_basic_end;
    }

    err = esp_rmaker_device_add_param(device1, param_str);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add string parameter";
        goto node_model_basic_end;
    }

    err = esp_rmaker_device_add_param(device1, param_float);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add float parameter";
        goto node_model_basic_end;
    }

    err = esp_rmaker_device_add_param(device2, param_bool);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add boolean parameter";
        goto node_model_basic_end;
    }

    err = esp_rmaker_node_add_device(node, device2);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add device2 to node";
        goto node_model_basic_end;
    }

    /* Add service to node */
    err = esp_rmaker_node_add_device(node, service1);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add service1 to node";
        goto node_model_basic_end;
    }

    /* Verify counts of devices and params */
    if (__count_devices(node) != 3) {
        err_msg = "Number of devices for node is incorrect";
        goto node_model_basic_end;
    }
    if (__count_params(device1) != 3) {
        err_msg = "Number of parameters for device1 is incorrect";
        goto node_model_basic_end;
    }
    if (__count_params(device2) != 1) {
        err_msg = "Number of parameters for device2 is incorrect";
        goto node_model_basic_end;
    }

    /* Retrieve the parameters */
    esp_rmaker_param_t *retrieved_param_int = esp_rmaker_device_get_param_by_id(device1, "tp_int");
    if (retrieved_param_int != param_int) {
        err_msg = "Failed to retrieve integer parameter";
        goto node_model_basic_end;
    }

    /* Check value */
    esp_rmaker_param_val_t *retrieved_val = esp_rmaker_param_get_val(retrieved_param_int);
    if (retrieved_val == NULL) {
        err_msg = "Failed to get value of integer parameter";
        goto node_model_basic_end;
    }
    if (retrieved_val->type != RMAKER_VAL_TYPE_INTEGER) {
        err_msg = "Integer parameter type is incorrect";
        goto node_model_basic_end;
    }
    if (retrieved_val->val.i != 10) {
        err_msg = "Integer parameter value is incorrect";
        goto node_model_basic_end;
    }

    /* Try retrieving a parameter by type */
    esp_rmaker_param_t *retrieved_param_bool_by_type = esp_rmaker_device_get_param_by_type(device2, "bool_type");
    if (retrieved_param_bool_by_type != param_bool) {
        err_msg = "Failed to retrieve bool parameter by type";
        goto node_model_basic_end;
    }
    esp_rmaker_param_t *retrieved_param_str_by_type = esp_rmaker_device_get_param_by_type(device1, "str_type");
    if (retrieved_param_str_by_type != param_str) {
        err_msg = "Failed to retrieve str parameter by type";
        goto node_model_basic_end;
    }
    esp_rmaker_param_t *retrieved_param_float_by_type = esp_rmaker_device_get_param_by_type(device1, "float_type");
    if (retrieved_param_float_by_type != param_float) {
        err_msg = "Failed to retrieve float parameter by type";
        goto node_model_basic_end;
    }
    if (esp_rmaker_device_get_param_by_type(device1, "nonexistent_type") != NULL) {
        err_msg = "Retrieved parameter by type that doesn't exist";
        goto node_model_basic_end;
    }

    /* Try retrieving a parameter that doesn't exist */
    retrieved_param_int = esp_rmaker_device_get_param_by_id(device1, "tp_does_not_exist");
    if (retrieved_param_int != NULL) {
        err_msg = "Retrieved parameter that doesn't exist";
        goto node_model_basic_end;
    }

node_model_basic_end:
    __teardown(node);

    if (err_msg) {
        TEST_FAIL_MESSAGE(err_msg);
    } else {
        TEST_PASS();
    }
}

void test_node_model_error_paths(void)
{
    /* Test invalid arguments */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_attribute_delete(NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_tag_delete(NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_delete(NULL));
    esp_rmaker_node_t *node = esp_rmaker_node_create(NULL, test_node_type);
    TEST_ASSERT_NULL(node);
    node = esp_rmaker_node_create(test_node_name, NULL);
    TEST_ASSERT_NULL(node);
    node = esp_rmaker_node_create(NULL, NULL);
    TEST_ASSERT_NULL(node);

    /* Create node */
    esp_rmaker_config_t config = {
        .enable_time_sync = false,
    };
    node = esp_rmaker_node_init(&config, test_node_name, test_node_type);
    TEST_ASSERT_NOT_NULL(node);

    /* Recreate should fail*/
    esp_rmaker_node_t *node2 = esp_rmaker_node_init(&config, test_node_name, test_node_type);
    TEST_ASSERT_NULL(node2);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_device(node, NULL));

    esp_rmaker_node_deinit(node);
}

void test_node_model_persistent_params(void)
{
    esp_rmaker_node_t *node = __setup();

    esp_rmaker_error_t err;
    osal_err_t nvs_err;
    char err_msg[256] = {0};
    uint8_t param_flags = PROP_FLAG_READ | PROP_FLAG_PERSIST;

    // Keep keys short to avoid NVS errors
    char test_param_id_format[] = "tp_%d";
    char test_device_id[] = "td_persist";

    /* Clear any existing stored values */
    nvs_err = __clear_nvs(test_device_id);
    if (nvs_err != OSAL_ERR_OK) {
        snprintf(err_msg, sizeof(err_msg), "Failed to clear NVS: %d", nvs_err);
        goto node_model_persistent_params_end;
    }

    /* Create values */
    esp_rmaker_param_val_t vals[4] = {
        esp_rmaker_int(10),
        esp_rmaker_float(20.0),
        esp_rmaker_bool(true),
        esp_rmaker_str("test_str"),
    };
    /* Create parameters */
    esp_rmaker_param_t *params[4];

    for (int i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        char param_id[32];
        snprintf(param_id, sizeof(param_id), test_param_id_format, i);
        params[i] = esp_rmaker_param_create(param_id, "test_type", vals[i], param_flags);
        if (params[i] == NULL) {
            snprintf(err_msg, sizeof(err_msg), "Failed to create parameter: %s", param_id);
            goto node_model_persistent_params_end;
        }
    }

    /* Create a device */
    esp_rmaker_device_t *device = esp_rmaker_device_create(test_device_id, "test_type", NULL);
    if (device == NULL) {
        snprintf(err_msg, sizeof(err_msg), "Failed to create device");
        goto node_model_persistent_params_end;
    }

    /* Add parameters to device */
    for (int i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
        err = esp_rmaker_device_add_param(device, params[i]);
        if (err != ESP_RMAKER_OK) {
            snprintf(err_msg, sizeof(err_msg), "Failed to add parameter %d to device after creation: %d", i, err);
            goto node_model_persistent_params_end;
        }
    }

    /* Check stored values */
    esp_rmaker_param_val_t stored_vals[4];
    for (int i = 0; i < sizeof(stored_vals) / sizeof(stored_vals[0]); i++) {
        err = esp_rmaker_param_get_stored_value((_esp_rmaker_param_t *)params[i], &stored_vals[i]);
        if (err != ESP_RMAKER_OK) {
            snprintf(err_msg, sizeof(err_msg), "Failed to get stored value after creation: %d", err);
            goto node_model_persistent_params_end;
        }
        if (stored_vals[i].type != vals[i].type) {
            snprintf(err_msg, sizeof(err_msg), "Stored value type is incorrect after creation: %d != %d", stored_vals[i].type, vals[i].type);
            goto node_model_persistent_params_end;
        }
        switch (stored_vals[i].type) {
        case RMAKER_VAL_TYPE_INTEGER:
            if (stored_vals[i].val.i != vals[i].val.i) {
                snprintf(err_msg, sizeof(err_msg), "Stored integer value is incorrect after creation: %d != %d", stored_vals[i].val.i, vals[i].val.i);
                goto node_model_persistent_params_end;
            }
            break;
        case RMAKER_VAL_TYPE_FLOAT:
            if (stored_vals[i].val.f != vals[i].val.f) {
                snprintf(err_msg, sizeof(err_msg), "Stored float value is incorrect after creation: %f != %f", stored_vals[i].val.f, vals[i].val.f);
                goto node_model_persistent_params_end;
            }
            break;
        case RMAKER_VAL_TYPE_BOOLEAN:
            if (stored_vals[i].val.b != vals[i].val.b) {
                snprintf(err_msg, sizeof(err_msg), "Stored bool value is incorrect after creation: %d != %d", stored_vals[i].val.b, vals[i].val.b);
                goto node_model_persistent_params_end;
            }
            break;
        case RMAKER_VAL_TYPE_STRING:
            if (strcmp(stored_vals[i].val.s, vals[i].val.s) != 0) {
                snprintf(err_msg, sizeof(err_msg), "Stored string value is incorrect after creation: %s != %s", stored_vals[i].val.s, vals[i].val.s);
                goto node_model_persistent_params_end;
            }
            break;
        default:
            snprintf(err_msg, sizeof(err_msg), "Stored value type is incorrect after creation: %d", stored_vals[i].type);
            goto node_model_persistent_params_end;
            break;
        }
    }

    /* Update a parameter */
    vals[0].val.i = 20;
    err = esp_rmaker_param_update(params[0], vals[0]);
    if (err != ESP_RMAKER_OK) {
        snprintf(err_msg, sizeof(err_msg), "Failed to update parameter after update: %d", err);
        goto node_model_persistent_params_end;
    }

    /* Check stored value */
    err = esp_rmaker_param_get_stored_value((_esp_rmaker_param_t *)params[0], &stored_vals[0]);
    if (err != ESP_RMAKER_OK) {
        snprintf(err_msg, sizeof(err_msg), "Failed to get stored value after update: %d", err);
        goto node_model_persistent_params_end;
    }

    if (stored_vals[0].type != RMAKER_VAL_TYPE_INTEGER) {
        snprintf(err_msg, sizeof(err_msg), "Stored integer value type is incorrect after update: %d", stored_vals[0].type);
        goto node_model_persistent_params_end;
    }

    if (stored_vals[0].val.i != 20) {
        snprintf(err_msg, sizeof(err_msg), "Stored integer value is incorrect after update: %d != %d", stored_vals[0].val.i, 20);
        goto node_model_persistent_params_end;
    }

    /* Change values */
    esp_rmaker_param_val_t new_vals[4] = {
        esp_rmaker_int(30),
        esp_rmaker_float(30.0),
        esp_rmaker_bool(false),
        esp_rmaker_str("test_str2"),
    };

    /* Re-create the parameters */
    for (int i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
        char param_id[32];
        snprintf(param_id, sizeof(param_id), test_param_id_format, i);
        params[i] = esp_rmaker_param_create(param_id, "test_type", new_vals[i], param_flags);
        if (params[i] == NULL) {
            snprintf(err_msg, sizeof(err_msg), "Failed to recreate parameter: %s", param_id);
            goto node_model_persistent_params_end;
        }
    }

    /* Re-create the device */
    esp_rmaker_device_t *old_device = device;
    device = esp_rmaker_device_create(test_device_id, "test_type", NULL);
    if (device == NULL) {
        snprintf(err_msg, sizeof(err_msg), "Failed to recreate device");
        goto node_model_persistent_params_end;
    }

    /* Add parameters to device */
    for (int i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
        err = esp_rmaker_device_add_param(device, params[i]);
        if (err != ESP_RMAKER_OK) {
            snprintf(err_msg, sizeof(err_msg), "%d: Failed to add parameter to device after re-creation: %d", i, err);
            goto node_model_persistent_params_end;
        }
    }

    /* Check stored values - expect old values */
    for (int i = 0; i < sizeof(stored_vals) / sizeof(stored_vals[0]); i++) {
        err = esp_rmaker_param_get_stored_value((_esp_rmaker_param_t *)params[i], &stored_vals[i]);
        if (err != ESP_RMAKER_OK) {
            snprintf(err_msg, sizeof(err_msg), "%d: Failed to get stored value after re-creation: %d", i, err);
            goto node_model_persistent_params_end;
        }
        if (stored_vals[i].type != vals[i].type) {
            snprintf(err_msg, sizeof(err_msg), "%d: Stored value type is incorrect after re-creation: %d != %d", i, stored_vals[i].type, vals[i].type);
            goto node_model_persistent_params_end;
        }
        switch (stored_vals[i].type) {
        case RMAKER_VAL_TYPE_INTEGER:
            if (stored_vals[i].val.i != vals[i].val.i) {
                snprintf(err_msg, sizeof(err_msg), "%d: Stored integer value is incorrect after re-creation: %d != %d", i, stored_vals[i].val.i, vals[i].val.i);
                goto node_model_persistent_params_end;
            }
            break;
        case RMAKER_VAL_TYPE_FLOAT:
            if (stored_vals[i].val.f != vals[i].val.f) {
                snprintf(err_msg, sizeof(err_msg), "%d: Stored float value is incorrect after re-creation: %f != %f", i, stored_vals[i].val.f, vals[i].val.f);
                goto node_model_persistent_params_end;
            }
            break;
        case RMAKER_VAL_TYPE_BOOLEAN:
            if (stored_vals[i].val.b != vals[i].val.b) {
                snprintf(err_msg, sizeof(err_msg), "%d: Stored bool value is incorrect after re-creation: %d != %d", i, stored_vals[i].val.b, vals[i].val.b);
                goto node_model_persistent_params_end;
            }
            break;
        case RMAKER_VAL_TYPE_STRING:
            if (strcmp(stored_vals[i].val.s, vals[i].val.s) != 0) {
                snprintf(err_msg, sizeof(err_msg), "%d: Stored string value is incorrect after re-creation: %s != %s", i, stored_vals[i].val.s, vals[i].val.s);
                goto node_model_persistent_params_end;
            }
            break;
        default:
            snprintf(err_msg, sizeof(err_msg), "%d: Stored value type is incorrect after re-creation: %d", i, stored_vals[i].type);
            goto node_model_persistent_params_end;
            break;
        }
    }

    /* Clean-up: clear stored values and delete devices */
    err = esp_rmaker_device_clear_stored_values(old_device);
    if (err != ESP_RMAKER_OK) {
        snprintf(err_msg, sizeof(err_msg), "Failed to clear stored values for old device: %d", err);
        goto node_model_persistent_params_end;
    }
    err = esp_rmaker_device_delete(old_device);
    if (err != ESP_RMAKER_OK) {
        snprintf(err_msg, sizeof(err_msg), "Failed to delete old device: %d", err);
        goto node_model_persistent_params_end;
    }
    err = esp_rmaker_device_clear_stored_values(device);
    if (err != ESP_RMAKER_OK) {
        snprintf(err_msg, sizeof(err_msg), "Failed to clear stored values for device: %d", err);
        goto node_model_persistent_params_end;
    }
    err = esp_rmaker_device_delete(device);
    if (err != ESP_RMAKER_OK) {
        snprintf(err_msg, sizeof(err_msg), "Failed to delete device: %d", err);
        goto node_model_persistent_params_end;
    }

node_model_persistent_params_end:
    __teardown(node);

    if (err_msg[0] != '\0') {
        TEST_FAIL_MESSAGE(err_msg);
    } else {
        TEST_PASS();
    }
}

void test_node_model_persistent_params_across_deinit(void)
{
    esp_rmaker_node_t *node = __setup();

    esp_rmaker_error_t err;
    osal_err_t nvs_err;
    char err_msg[256] = {0};
    uint8_t param_flags = PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST;

    // Keep keys short to avoid NVS errors
    char test_param_id_format[] = "tpd_%d";
    char test_device_id[] = "td_persdeinit"; // 16 characters max

    /* Clear any existing stored values */
    nvs_err = __clear_nvs(test_device_id);
    if (nvs_err != OSAL_ERR_OK) {
        snprintf(err_msg, sizeof(err_msg), "Failed to clear NVS: %d", nvs_err);
        goto node_model_persistent_deinit_cleanup;
    }

    /* Create values */
    esp_rmaker_param_val_t vals[2] = {
        esp_rmaker_int(42),
        esp_rmaker_str("deinit_test"),
    };

    /* FIRST PHASE: Create device, parameters, and set values */

    /* Create parameters */
    esp_rmaker_param_t *params[2];
    for (int i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        char param_id[32];
        snprintf(param_id, sizeof(param_id), test_param_id_format, i);
        params[i] = esp_rmaker_param_create(param_id, "test_type", vals[i], param_flags);
        if (params[i] == NULL) {
            snprintf(err_msg, sizeof(err_msg), "Failed to create parameter: %s", param_id);
            goto node_model_persistent_deinit_cleanup;
        }
    }

    /* Create a device */
    esp_rmaker_device_t *device = esp_rmaker_device_create(test_device_id, "test_type", NULL);
    if (device == NULL) {
        snprintf(err_msg, sizeof(err_msg), "Failed to create device");
        goto node_model_persistent_deinit_cleanup;
    }

    /* Add device to node */
    err = esp_rmaker_node_add_device(node, device);
    if (err != ESP_RMAKER_OK) {
        snprintf(err_msg, sizeof(err_msg), "Failed to add device to node: %d", err);
        goto node_model_persistent_deinit_cleanup;
    }

    /* Add parameters to device */
    for (int i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
        err = esp_rmaker_device_add_param(device, params[i]);
        if (err != ESP_RMAKER_OK) {
            snprintf(err_msg, sizeof(err_msg), "Failed to add parameter %d to device: %d", i, err);
            goto node_model_persistent_deinit_cleanup;
        }
    }

    /* Update parameters with new values */
    esp_rmaker_param_val_t new_vals[2] = {
        esp_rmaker_int(99),
        esp_rmaker_str("updated_after_deinit"),
    };

    for (int i = 0; i < sizeof(new_vals) / sizeof(new_vals[0]); i++) {
        err = esp_rmaker_param_update(params[i], new_vals[i]);
        if (err != ESP_RMAKER_OK) {
            snprintf(err_msg, sizeof(err_msg), "Failed to update parameter %d: %d", i, err);
            goto node_model_persistent_deinit_cleanup;
        }
    }

    /* Verify the updates were stored */
    esp_rmaker_param_val_t stored_vals[2];
    for (int i = 0; i < sizeof(stored_vals) / sizeof(stored_vals[0]); i++) {
        err = esp_rmaker_param_get_stored_value((_esp_rmaker_param_t *)params[i], &stored_vals[i]);
        if (err != ESP_RMAKER_OK) {
            snprintf(err_msg, sizeof(err_msg), "Failed to get stored value for parameter %d: %d", i, err);
            goto node_model_persistent_deinit_cleanup;
        }
        if (i == 0) { // integer
            if (stored_vals[i].val.i != new_vals[i].val.i) {
                snprintf(err_msg, sizeof(err_msg), "Stored integer value incorrect: %d != %d", stored_vals[i].val.i, new_vals[i].val.i);
                goto node_model_persistent_deinit_cleanup;
            }
        } else { // string
            if (strcmp(stored_vals[i].val.s, new_vals[i].val.s) != 0) {
                snprintf(err_msg, sizeof(err_msg), "Stored string value incorrect: %s != %s", stored_vals[i].val.s, new_vals[i].val.s);
                goto node_model_persistent_deinit_cleanup;
            }
        }
    }

    /* Clean up first phase (deinit node) */
node_model_persistent_deinit_cleanup:
    if (err_msg[0] != '\0') {
        __teardown(node);
        goto node_model_persistent_deinit_end;
    }

    /* De-init only, no clearing of stored values to ensure the stored values are still present */
    err = esp_rmaker_node_deinit(node);
    if (err != ESP_RMAKER_OK) {
        snprintf(err_msg, sizeof(err_msg), "Failed to de-init node: %d", err);
        goto node_model_persistent_deinit_phase2_cleanup;
    }

    /* SECOND PHASE: Re-create node, device, parameters and check persistence */

    node = __setup();

    /* Re-create parameters with original values (they should be overridden by stored values) */
    for (int i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        char param_id[32];
        snprintf(param_id, sizeof(param_id), test_param_id_format, i);
        params[i] = esp_rmaker_param_create(param_id, "test_type", vals[i], param_flags);
        if (params[i] == NULL) {
            snprintf(err_msg, sizeof(err_msg), "Failed to recreate parameter: %s", param_id);
            goto node_model_persistent_deinit_phase2_cleanup;
        }
    }

    /* Re-create device */
    device = esp_rmaker_device_create(test_device_id, "test_type", NULL);
    if (device == NULL) {
        snprintf(err_msg, sizeof(err_msg), "Failed to recreate device");
        goto node_model_persistent_deinit_phase2_cleanup;
    }

    /* Add device to node */
    err = esp_rmaker_node_add_device(node, device);
    if (err != ESP_RMAKER_OK) {
        snprintf(err_msg, sizeof(err_msg), "Failed to re-add device to node: %d", err);
        goto node_model_persistent_deinit_phase2_cleanup;
    }

    /* Add parameters to device */
    for (int i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
        err = esp_rmaker_device_add_param(device, params[i]);
        if (err != ESP_RMAKER_OK) {
            snprintf(err_msg, sizeof(err_msg), "Failed to re-add parameter %d to device: %d", i, err);
            goto node_model_persistent_deinit_phase2_cleanup;
        }
    }

    /* Check that stored values persist across deinit/re-init */
    for (int i = 0; i < sizeof(stored_vals) / sizeof(stored_vals[0]); i++) {
        err = esp_rmaker_param_get_stored_value((_esp_rmaker_param_t *)params[i], &stored_vals[i]);
        if (err != ESP_RMAKER_OK) {
            snprintf(err_msg, sizeof(err_msg), "Failed to get stored value after re-init for parameter %d: %d", i, err);
            goto node_model_persistent_deinit_phase2_cleanup;
        }
        if (i == 0) { // integer - should be the updated value (99), not original (42)
            if (stored_vals[i].val.i != new_vals[i].val.i) {
                snprintf(err_msg, sizeof(err_msg), "Stored integer value after re-init incorrect: %d != %d", stored_vals[i].val.i, new_vals[i].val.i);
                goto node_model_persistent_deinit_phase2_cleanup;
            }
        } else { // string - should be the updated value, not original
            if (strcmp(stored_vals[i].val.s, new_vals[i].val.s) != 0) {
                snprintf(err_msg, sizeof(err_msg), "Stored string value after re-init incorrect: %s != %s", stored_vals[i].val.s, new_vals[i].val.s);
                goto node_model_persistent_deinit_phase2_cleanup;
            }
        }
    }

node_model_persistent_deinit_phase2_cleanup:
    __teardown(node);

node_model_persistent_deinit_end:
    if (err_msg[0] != '\0') {
        TEST_FAIL_MESSAGE(err_msg);
    } else {
        TEST_PASS();
    }
}

void test_node_config_basic(void)
{
    esp_rmaker_node_t *node = __setup();

    char *err_msg = NULL;
    esp_rmaker_error_t err;

    /* Create parameters */
    esp_rmaker_param_val_t vals[4] = {
        esp_rmaker_int(10),
        esp_rmaker_float(20.0),
        esp_rmaker_bool(true),
        esp_rmaker_str("test_str"),
    };
    uint8_t param_flags[4] = {
        PROP_FLAG_READ,
        PROP_FLAG_READ | PROP_FLAG_PERSIST,
        PROP_FLAG_WRITE | PROP_FLAG_INDEXED,
        PROP_FLAG_READ | PROP_FLAG_WRITE | PROP_FLAG_PERSIST,
    };
    esp_rmaker_param_t *params[4];
    for (int i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
        char param_id[32];
        snprintf(param_id, sizeof(param_id), "test_param_%d", i);
        params[i] = esp_rmaker_param_create(param_id, "test_type", vals[i], param_flags[i]);
        if (params[i] == NULL) {
            err_msg = "Failed to create parameter";
            goto node_config_basic_end;
        }
    }

    esp_rmaker_device_t *devices[2];
    for (int i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
        char device_id[32];
        snprintf(device_id, sizeof(device_id), "test_device_%d", i);
        devices[i] = esp_rmaker_device_create(device_id, "test_type", NULL);
        if (devices[i] == NULL) {
            err_msg = "Failed to create device";
            goto node_config_basic_end;
        }
    }

    /* Add parameters to devices */
    for (int i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
        uint8_t param_start = 2 * i;
        for (int j = param_start; j < param_start + 2; j++) {
            err = esp_rmaker_device_add_param(devices[i], params[j]);
            if (err != ESP_RMAKER_OK) {
                err_msg = "Failed to add parameter to device";
                goto node_config_basic_end;
            }
        }
    }

    /* Add devices to node */
    for (int i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
        err = esp_rmaker_node_add_device(node, devices[i]);
        if (err != ESP_RMAKER_OK) {
            err_msg = "Failed to add device to node";
            goto node_config_basic_end;
        }
    }

    /* Check config */
    char str_val[64];

    char *config_str = esp_rmaker_get_node_config();
    if (config_str == NULL) {
        err_msg = "Failed to get node config";
        goto node_config_basic_end;
    }

    jparse_ctx_t jctx;
    int ret = json_parse_start(&jctx, config_str, strlen(config_str));
    if (ret != OS_SUCCESS) {
        err_msg = "Failed to parse config";
        goto node_config_basic_end;
    }

    // Check thing name
    if (json_obj_get_string(&jctx, "node_id", str_val, sizeof(str_val)) != OS_SUCCESS) {
        err_msg = "Failed to get node_id";
        goto node_config_basic_end;
    }
    char *thing_name = NULL;
    err = esp_rmaker_credentials_get_thing_name(&thing_name);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to get thing name";
        goto node_config_basic_end;
    }
    if (strcmp(str_val, thing_name) != 0) {
        err_msg = "Node ID is incorrect";
        free(thing_name);
        goto node_config_basic_end;
    }
    free(thing_name);

    // Enter config object
    if (json_obj_get_object(&jctx, "config") != OS_SUCCESS) {
        err_msg = "Failed to enter config";
        goto node_config_basic_end;
    }

    // Check data model
    if (json_obj_get_string(&jctx, "data_model", str_val, sizeof(str_val)) != OS_SUCCESS) {
        err_msg = "Failed to get data model";
        goto node_config_basic_end;
    }

    if (strcmp(str_val, data_model_node_get_data_model_type()) != 0) {
        err_msg = "Data model is incorrect";
        goto node_config_basic_end;
    }

    // Check info
    if (json_obj_get_object(&jctx, "info") != OS_SUCCESS) {
        err_msg = "Failed to get info";
        goto node_config_basic_end;
    }
    if (json_obj_get_string(&jctx, "name", str_val, sizeof(str_val)) != OS_SUCCESS) {
        err_msg = "Failed to get info name";
        goto node_config_basic_end;
    }
    if (strcmp(str_val, test_node_name) != 0) {
        err_msg = "Info name is incorrect";
        goto node_config_basic_end;
    }
    if (json_obj_get_string(&jctx, "type", str_val, sizeof(str_val)) != OS_SUCCESS) {
        err_msg = "Failed to get info type";
        goto node_config_basic_end;
    }
    if (strcmp(str_val, test_node_type) != 0) {
        err_msg = "Info type is incorrect";
        goto node_config_basic_end;
    }
    if (json_obj_get_string(&jctx, "fw_version", str_val, sizeof(str_val)) != OS_SUCCESS) {
        err_msg = "Failed to get info fw_version";
        goto node_config_basic_end;
    }
    if (strcmp(str_val, osal_sysinfo_get_fw_version()) != 0) {
        err_msg = "Info fw_version is incorrect";
        goto node_config_basic_end;
    }
    if (json_obj_get_string(&jctx, "model", str_val, sizeof(str_val)) != OS_SUCCESS) {
        err_msg = "Failed to get info model";
        goto node_config_basic_end;
    }
    if (strcmp(str_val, osal_sysinfo_get_project_name()) != 0) {
        err_msg = "Info model is incorrect";
        goto node_config_basic_end;
    }
    json_obj_leave_object(&jctx);

    // Check devices
    int num_devices;
    if (json_obj_get_array(&jctx, "devices", &num_devices) != OS_SUCCESS) {
        err_msg = "Failed to get devices";
        goto node_config_basic_end;
    }
    if (num_devices != sizeof(devices) / sizeof(devices[0])) {
        err_msg = "Number of devices is incorrect";
        goto node_config_basic_end;
    }

    for (int i = 0; i < num_devices; i++) {
        if (json_arr_get_object(&jctx, i) != OS_SUCCESS) {
            err_msg = "Failed to get device";
            goto node_config_basic_end;
        }

        // Check device id and type
        if (json_obj_get_string(&jctx, "id", str_val, sizeof(str_val)) != OS_SUCCESS) {
            err_msg = "Failed to get device id";
            goto node_config_basic_end;
        }
        if (strcmp(str_val, esp_rmaker_device_get_id(devices[i])) != 0) {
            err_msg = "Device id is incorrect";
            goto node_config_basic_end;
        }
        if (json_obj_get_string(&jctx, "type", str_val, sizeof(str_val)) != OS_SUCCESS) {
            err_msg = "Failed to get device type";
            goto node_config_basic_end;
        }
        if (strcmp(str_val, esp_rmaker_device_get_type(devices[i])) != 0) {
            err_msg = "Device type is incorrect";
            goto node_config_basic_end;
        }

        // Check parameters
        int num_params;
        if (json_obj_get_array(&jctx, "params", &num_params) != OS_SUCCESS) {
            err_msg = "Failed to get parameters";
            goto node_config_basic_end;
        }
        if (num_params != 2) {
            err_msg = "Number of parameters is incorrect";
            goto node_config_basic_end;
        }

        uint8_t param_start = 2 * i;
        for (int j = param_start; j < param_start + 2; j++) {
            if (json_arr_get_object(&jctx, j - param_start) != OS_SUCCESS) {
                err_msg = "Failed to get parameter";
                goto node_config_basic_end;
            }

            char *param_id = ((_esp_rmaker_param_t *)params[j])->id;
            char *param_type = ((_esp_rmaker_param_t *)params[j])->type;
            uint8_t param_flags = ((_esp_rmaker_param_t *)params[j])->prop_flags;

            // Check parameter id and type
            if (json_obj_get_string(&jctx, "id", str_val, sizeof(str_val)) != OS_SUCCESS) {
                err_msg = "Failed to get parameter id";
                goto node_config_basic_end;
            }

            if (strcmp(str_val, param_id) != 0) {
                err_msg = "Parameter id is incorrect";
                goto node_config_basic_end;
            }

            if (json_obj_get_string(&jctx, "type", str_val, sizeof(str_val)) != OS_SUCCESS) {
                err_msg = "Failed to get parameter type";
                goto node_config_basic_end;
            }

            if (strcmp(str_val, param_type) != 0) {
                err_msg = "Parameter type is incorrect";
                goto node_config_basic_end;
            }

            // Check data type
            if (json_obj_get_string(&jctx, "data_type", str_val, sizeof(str_val)) != OS_SUCCESS) {
                err_msg = "Failed to get parameter data type";
                goto node_config_basic_end;
            }
            switch (vals[j].type) {
            case RMAKER_VAL_TYPE_BOOLEAN:
                if (strcmp(str_val, "bool") != 0) {
                    err_msg = "Parameter data type is incorrect, expected bool";
                    goto node_config_basic_end;
                }
                break;
            case RMAKER_VAL_TYPE_INTEGER:
                if (strcmp(str_val, "int") != 0) {
                    err_msg = "Parameter data type is incorrect, expected int";
                    goto node_config_basic_end;
                }
                break;
            case RMAKER_VAL_TYPE_FLOAT:
                if (strcmp(str_val, "float") != 0) {
                    err_msg = "Parameter data type is incorrect, expected float";
                    goto node_config_basic_end;
                }
                break;
            case RMAKER_VAL_TYPE_STRING:
                if (strcmp(str_val, "string") != 0) {
                    err_msg = "Parameter data type is incorrect, expected string";
                    goto node_config_basic_end;
                }
                break;
            default:
                err_msg = "Parameter has unknown data type, expected bool, int, float, or string";
                goto node_config_basic_end;
                break;
            }

            // Check parameter properties
            uint8_t param_flags_json = 0;
            int num_properties;
            if (json_obj_get_array(&jctx, "properties", &num_properties) != OS_SUCCESS) {
                err_msg = "Failed to get parameter properties";
                goto node_config_basic_end;
            }

            for (int k = 0; k < num_properties; k++) {
                if (json_arr_get_string(&jctx, k, str_val, sizeof(str_val)) != OS_SUCCESS) {
                    err_msg = "Failed to get property";
                    goto node_config_basic_end;
                }
                if (strcmp(str_val, "read") == 0) {
                    param_flags_json |= PROP_FLAG_READ;
                } else if (strcmp(str_val, "write") == 0) {
                    param_flags_json |= PROP_FLAG_WRITE;
                } else if (strcmp(str_val, "indexed") == 0) {
                    param_flags_json |= PROP_FLAG_INDEXED;
                } else if (strcmp(str_val, "persist") == 0) {
                    param_flags_json |= PROP_FLAG_PERSIST;
                } else {
                    err_msg = "Parameter has unknown property";
                    goto node_config_basic_end;
                }
            }

            if (param_flags_json != param_flags) {
                err_msg = "Parameter flags are incorrect";
                goto node_config_basic_end;
            }

            json_obj_leave_array(&jctx);
            json_arr_leave_object(&jctx);
        }

        json_obj_leave_array(&jctx);
        json_arr_leave_object(&jctx);
    }

    json_obj_leave_array(&jctx);
    json_obj_leave_object(&jctx);

    json_parse_end(&jctx);

node_config_basic_end:
    __teardown(node);

    if (err_msg) {
        TEST_FAIL_MESSAGE(err_msg);
    } else {
        TEST_PASS();
    }
}

void test_node_model_device_add_remove_and_errors(void)
{
    esp_rmaker_node_t *node = __setup();


    /* adding duplicate device id should fail */
    esp_rmaker_device_t *d1a = esp_rmaker_device_create("dup", "type", NULL);
    esp_rmaker_device_t *d1b = esp_rmaker_device_create("dup", "type", NULL);
    esp_rmaker_device_t *d2 = esp_rmaker_device_create("d2", "type", NULL);
    esp_rmaker_device_t *d3 = esp_rmaker_device_create("d3", "type", NULL);
    TEST_ASSERT_NOT_NULL(d1a);
    TEST_ASSERT_NOT_NULL(d1b);
    TEST_ASSERT_NOT_NULL(d2);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, d1a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, d2));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, d3));

    /* add devices with bad parameters should fail */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_device(node, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_device(NULL, d1a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_device(node, d1b));

    /* remove device with bad parameters should fail */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_remove_device(node, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_remove_device(NULL, d1a));

    /* remove non-existent device should fail */
    esp_rmaker_device_t *other = esp_rmaker_device_create("other", "type", NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_remove_device(node, other));

    /* remove existing device should succeed */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_remove_device(node, d2));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_remove_device(node, d1a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_remove_device(node, d3));

    /* delete devices */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_delete(d1a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_delete(d2));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_delete(d1b));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_delete(d3));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_delete(other));

    __teardown(node);
}

void test_node_model_attributes_and_tags(void)
{
    esp_rmaker_node_t *node = __setup();

    /* attributes: null checks */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_attribute(NULL, "k", "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_attribute(node, NULL, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_attribute(node, "k", NULL));

    /* add attributes (duplicate names are not allowed by implementation) */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_attribute(node, "a1", "v1"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_attribute(node, "a2", "v1"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_attribute(node, "a2", "vX"));

    /* delete attribute: unlink head and delete */
    _esp_rmaker_node_t *_node = (_esp_rmaker_node_t *)node;
    esp_rmaker_attr_t *attr = _node->attributes;
    TEST_ASSERT_NOT_NULL(attr);
    _node->attributes = attr->next;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_attribute_delete(attr));

    /* tags: null checks */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_tag(NULL, "t1", "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_tag(node, NULL, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_tag(node, "t1", NULL));
    TEST_ASSERT_NULL(esp_rmaker_node_get_tag_by_name(node, "t1"));

    /* tags: reserved tags are not allowed */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_tag(node, RMAKER_INFO_KEY_NAME, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_tag(node, RMAKER_INFO_KEY_TYPE, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_tag(node, RMAKER_INFO_KEY_FW_VERSION, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_tag(node, RMAKER_INFO_KEY_MODEL, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_update_tag(node, RMAKER_INFO_KEY_NAME, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_update_tag(node, RMAKER_INFO_KEY_TYPE, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_update_tag(node, RMAKER_INFO_KEY_FW_VERSION, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_update_tag(node, RMAKER_INFO_KEY_MODEL, "v"));

    /* add tag; update tag should overwrite and mark for report */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_tag(node, "t1", "v1"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_tag(node, "t2", "v2"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_tag(node, "t2", "vX"));

    TEST_ASSERT_EQUAL_STRING("vX", esp_rmaker_node_get_tag_by_name(node, "t2")->value);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_update_tag(node, "t1", "v2"));
    TEST_ASSERT_EQUAL_STRING("v2", esp_rmaker_node_get_tag_by_name(node, "t1")->value);

    /* create another tag to exercise delete path */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_tag(node, "t_del", "val"));
    esp_rmaker_tag_t *prev = NULL;
    esp_rmaker_tag_t *iter = _node->tags;
    while (iter && strcmp(iter->name, "t_del") != 0) {
        prev = iter;
        iter = iter->next;
    }
    TEST_ASSERT_NOT_NULL(iter);
    if (prev) {
        prev->next = iter->next;
    } else {
        _node->tags = iter->next;
    }
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_tag_delete(iter));

    __teardown(node);
}

void test_node_model_getters_and_info(void)
{
    esp_rmaker_node_t *node = __setup();

    /* info and getters */
    esp_rmaker_node_info_t *info = esp_rmaker_node_get_info(node);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_STRING(test_node_name, info->name);
    TEST_ASSERT_EQUAL_STRING(test_node_type, info->type);
    TEST_ASSERT_EQUAL_STRING(osal_sysinfo_get_fw_version(), info->fw_version);
    TEST_ASSERT_EQUAL_STRING(osal_sysinfo_get_project_name(), info->model);

    /* Get tags and check if the reserved tags are present */
    esp_rmaker_tag_t *tags = esp_rmaker_node_get_first_tag(node);
    TEST_ASSERT_NOT_NULL(tags);
    bool found_name = false;
    bool found_type = false;
    bool found_fw_version = false;
    bool found_model = false;
    while (tags) {
        if (strcmp(tags->name, RMAKER_INFO_KEY_NAME) == 0) {
            TEST_ASSERT_EQUAL_STRING(info->name, tags->value);
            found_name = true;
        } else if (strcmp(tags->name, RMAKER_INFO_KEY_TYPE) == 0) {
            TEST_ASSERT_EQUAL_STRING(info->type, tags->value);
            found_type = true;
        } else if (strcmp(tags->name, RMAKER_INFO_KEY_FW_VERSION) == 0) {
            TEST_ASSERT_EQUAL_STRING(info->fw_version, tags->value);
            found_fw_version = true;
        } else if (strcmp(tags->name, RMAKER_INFO_KEY_MODEL) == 0) {
            TEST_ASSERT_EQUAL_STRING(info->model, tags->value);
            found_model = true;
        }
        tags = tags->next;
    }
    TEST_ASSERT_TRUE(found_name && found_type && found_fw_version && found_model);

    /* device lookup */
    esp_rmaker_device_t *d1 = esp_rmaker_device_create("d1", "t", NULL);
    esp_rmaker_device_t *d2 = esp_rmaker_device_create("d2", "t", NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, d1));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, d2));
    TEST_ASSERT_EQUAL_PTR(d2, esp_rmaker_node_get_device_by_id(node, "d2"));
    TEST_ASSERT_EQUAL_PTR(d1, esp_rmaker_node_get_device_by_id(node, "d1"));
    TEST_ASSERT_NULL(esp_rmaker_node_get_device_by_id(node, "d3"));

    /* getters with null */
    TEST_ASSERT_NULL(esp_rmaker_node_get_info(NULL));
    TEST_ASSERT_NULL(esp_rmaker_node_get_first_device(NULL));
    TEST_ASSERT_NULL(esp_rmaker_node_get_first_attribute(NULL));
    TEST_ASSERT_NULL(esp_rmaker_node_get_first_tag(NULL));
    TEST_ASSERT_NULL(esp_rmaker_node_get_device_by_id(NULL, "d"));
    TEST_ASSERT_NULL(esp_rmaker_node_get_device_by_id(node, NULL));
    TEST_ASSERT_NULL(esp_rmaker_node_get_tag_by_name(NULL, "x"));
    TEST_ASSERT_NULL(esp_rmaker_node_get_tag_by_name(node, NULL));

    /* attribute lookup */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_attribute(node, "a1", "v1"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_attribute(node, "a2", "v2"));
    esp_rmaker_attr_t *attr = esp_rmaker_node_get_first_attribute(node);
    TEST_ASSERT_NOT_NULL(attr);
    TEST_ASSERT_EQUAL_STRING("a1", attr->name);
    TEST_ASSERT_EQUAL_STRING("v1", attr->value);
    TEST_ASSERT_NOT_NULL(attr->next);
    TEST_ASSERT_EQUAL_STRING("a2", attr->next->name);
    TEST_ASSERT_EQUAL_STRING("v2", attr->next->value);

    /* tag lookup */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_tag(node, "t1", "v1"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_tag(node, "t2", "v2"));
    esp_rmaker_tag_t *tag = esp_rmaker_node_get_tag_by_name(node, "t1");
    TEST_ASSERT_NOT_NULL(tag);
    TEST_ASSERT_EQUAL_STRING("t1", tag->name);
    TEST_ASSERT_EQUAL_STRING("v1", tag->value);
    tag = esp_rmaker_node_get_tag_by_name(node, "t2");
    TEST_ASSERT_NOT_NULL(tag);
    TEST_ASSERT_EQUAL_STRING("t2", tag->name);
    TEST_ASSERT_EQUAL_STRING("v2", tag->value);

    __teardown(node);
}

void test_device_model_error_paths(void)
{
    esp_rmaker_node_t *node = __setup();
    esp_rmaker_device_t *d1 = esp_rmaker_device_create("d1", "test_type", NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_device(NULL, d1));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_device(node, NULL));

    /* Add device to node */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, d1));

    /* Add duplicate device should fail */
    esp_rmaker_device_t *d2 = esp_rmaker_device_create("d1", "test_type2", NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_device(node, d2));

    /* Remove device from node */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_remove_device(NULL, d1));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_remove_device(node, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_remove_device(node, d1));

    /* Get params */
    TEST_ASSERT_NULL(esp_rmaker_device_get_param_by_id(d1, NULL));
    TEST_ASSERT_NULL(esp_rmaker_device_get_param_by_id(NULL, "p"));
    TEST_ASSERT_NULL(esp_rmaker_device_get_param_by_type(d1, NULL));
    TEST_ASSERT_NULL(esp_rmaker_device_get_param_by_type(NULL, "t"));

    /* Delete device */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_delete(d1));

    __teardown(node);
}

void test_device_model_delete_guard(void)
{
    esp_rmaker_node_t *node = __setup();
    esp_rmaker_device_t *d = esp_rmaker_device_create("guard_dev", "type", NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, d));
    /* deleting while attached should fail */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE, esp_rmaker_device_delete(d));
    /* remove from node then delete */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_remove_device(node, d));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_delete(d));
    __teardown(node);
}

static esp_rmaker_error_t __test_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        esp_rmaker_param_val_t val, void *priv, esp_rmaker_write_ctx_t *ctx)
{
    (void)device; (void)param; (void)val; (void)priv; (void)ctx;
    return ESP_RMAKER_OK;
}

void test_device_model_attrs_callbacks_getters(void)
{
    esp_rmaker_node_t *node = __setup();
    esp_rmaker_device_t *d = esp_rmaker_device_create("devX", "typeX", (void *)0x1234);
    TEST_ASSERT_NOT_NULL(d);

    /* getters */
    TEST_ASSERT_EQUAL_STRING("devX", esp_rmaker_device_get_id(d));
    TEST_ASSERT_EQUAL_STRING("typeX", esp_rmaker_device_get_type(d));
    TEST_ASSERT_EQUAL_PTR((void *)0x1234, esp_rmaker_device_get_priv_data(d));
    TEST_ASSERT_NULL(esp_rmaker_device_get_id(NULL));
    TEST_ASSERT_NULL(esp_rmaker_device_get_type(NULL));
    TEST_ASSERT_NULL(esp_rmaker_device_get_priv_data(NULL));

    /* callbacks */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_add_cb(NULL, __test_write_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_cb(d, __test_write_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_add_bulk_cb(NULL, NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_bulk_cb(d, NULL, NULL));

    /* attributes */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_add_attribute(NULL, "a", "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_add_attribute(d, NULL, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_add_attribute(d, "a", NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_attribute(d, "a1", "v1"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_attribute(d, "a2", "v2"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_attribute(d, "a3", "v3"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_add_attribute(d, "a2", "dup"));

    /* add to node to exercise nested param and persistent handling */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, d));

    __teardown(node);
}

void test_param_model_getters(void)
{
    const char *param_id = "tp";
    const char *param_type = "tt";
    const esp_rmaker_param_val_t val = esp_rmaker_int(1);
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_id, param_type, val, 0);
    TEST_ASSERT_NOT_NULL(param);
    TEST_ASSERT_EQUAL_STRING(param_id, esp_rmaker_param_get_id(param));
    TEST_ASSERT_EQUAL_STRING(param_type, esp_rmaker_param_get_type(param));
    TEST_ASSERT_EQUAL_INT(1, esp_rmaker_param_get_val(param)->val.i);
    TEST_ASSERT_NULL(esp_rmaker_param_get_id(NULL));
    TEST_ASSERT_NULL(esp_rmaker_param_get_type(NULL));
    TEST_ASSERT_NULL(esp_rmaker_param_get_val(NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_delete(param));
}

void test_param_model_create_bounds_and_update(void)
{
    esp_rmaker_node_t *node = __setup();
    esp_rmaker_device_t *d = esp_rmaker_device_create("pd", "t", NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, d));

    /* create error paths */
    TEST_ASSERT_EQUAL(NULL, esp_rmaker_param_create(NULL, "t", esp_rmaker_int(1), 0));
    /* time_series not allowed for object/array */
    TEST_ASSERT_EQUAL(NULL, esp_rmaker_param_create("pobj", "t", esp_rmaker_obj("{}"), PROP_FLAG_TIME_SERIES));
    TEST_ASSERT_EQUAL(NULL, esp_rmaker_param_create("parr", "t", esp_rmaker_array("[]"), PROP_FLAG_TIME_SERIES));

    /* create params */
    esp_rmaker_param_t *pi = esp_rmaker_param_create("pi", "t", esp_rmaker_int(5), PROP_FLAG_READ | PROP_FLAG_PERSIST);
    esp_rmaker_param_t *pf = esp_rmaker_param_create("pf", "t", esp_rmaker_float(1.5f), PROP_FLAG_READ);
    _esp_rmaker_param_t *_pi = (_esp_rmaker_param_t *)pi;
    _esp_rmaker_param_t *_pf = (_esp_rmaker_param_t *)pf;
    esp_rmaker_param_t *ps = esp_rmaker_param_create("ps", "t", esp_rmaker_str("s"), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(pi); TEST_ASSERT_NOT_NULL(pf); TEST_ASSERT_NOT_NULL(ps);

    /* device add param wires parent for persistence */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(d, pi));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(d, pf));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(d, ps));

    /* bounds */
    /* bad parameter */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_add_bounds(NULL, esp_rmaker_int(0), esp_rmaker_int(10), esp_rmaker_int(1)));
    /* wrong type for bounds holder */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_add_bounds(ps, esp_rmaker_int(0), esp_rmaker_int(10), esp_rmaker_int(1)));
    /* type mismatch among min/max/step */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_add_bounds(pi, esp_rmaker_float(-1.2f), esp_rmaker_int(2), esp_rmaker_int(1)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_add_bounds(pi, esp_rmaker_int(0), esp_rmaker_float(2.0f), esp_rmaker_int(1)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_add_bounds(pi, esp_rmaker_int(0), esp_rmaker_int(300), esp_rmaker_float(1.0f)));
    /* success for int and float */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_add_bounds(pi, esp_rmaker_int(-10), esp_rmaker_int(20), esp_rmaker_int(2)));
    TEST_ASSERT_EQUAL_INT(-10, _pi->bounds->min.val.i);
    TEST_ASSERT_EQUAL_INT(20, _pi->bounds->max.val.i);
    TEST_ASSERT_EQUAL_INT(2, _pi->bounds->step.val.i);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_add_bounds(pi, esp_rmaker_int(0), esp_rmaker_int(10), esp_rmaker_int(1)));
    TEST_ASSERT_EQUAL_INT(0, _pi->bounds->min.val.i);
    TEST_ASSERT_EQUAL_INT(10, _pi->bounds->max.val.i);
    TEST_ASSERT_EQUAL_INT(1, _pi->bounds->step.val.i);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_add_bounds(pf, esp_rmaker_float(0.0f), esp_rmaker_float(2.0f), esp_rmaker_float(0.1f)));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, _pf->bounds->min.val.f);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, _pf->bounds->max.val.f);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, _pf->bounds->step.val.f);

    /* update: null */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update(NULL, esp_rmaker_int(1)));
    /* type mismatch */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update(pi, esp_rmaker_bool(true)));
    /* equal value: returns OK (no longer short-circuits the downstream report/automation;
     * only the NVS write-back is skipped - see test_param_model_same_value_update_skips_nvs) */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(pi, esp_rmaker_int(5)));
    /* out of bounds */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update(pi, esp_rmaker_int(20)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update(pi, esp_rmaker_int(-10)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update(pf, esp_rmaker_float(-1.0f)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update(pf, esp_rmaker_float(2.1f)));
    /* valid updates */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(pi, esp_rmaker_int(6)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(pf, esp_rmaker_float(1.6f)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(ps, esp_rmaker_str("s2")));

    /* update_and_report: propagate OK, and failure path when update fails */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update_and_report(pi, esp_rmaker_int(7)));
    TEST_ASSERT_NOT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update_and_report(pi, esp_rmaker_bool(true)));

    /* update_and_notify: propagates the update result. The notify side effect (topic, payload,
     * best-effort on publish failure) is covered by
     * test_notify_param_update_and_notify_publishes_push. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update_and_notify(pf, esp_rmaker_float(1.7f)));

    __teardown(node);
}

static void __check_val_equal(esp_rmaker_param_val_t expected, esp_rmaker_param_val_t *actual);

/* Plant a sentinel value directly into a param's NVS slot, bypassing esp_rmaker_param_update.
 * Used to detect whether a subsequent update writes NVS (sentinel overwritten) or skips it
 * (sentinel preserved). Mirrors the storage format used by esp_rmaker_param_store_value. */
static void __plant_int_in_nvs(const char *device_id, const char *param_id, int sentinel)
{
    osal_storage_handle_t handle;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_open(RMAKER_NVS_PART_NAME, device_id, OSAL_STORAGE_OPEN_READWRITE, &handle));
    esp_rmaker_param_val_t val = esp_rmaker_int(sentinel);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_set(handle, param_id, &val, sizeof(val), OSAL_STORAGE_TYPE_BINARY));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_commit(handle));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_close(handle));
}

/* The "ignore equal value" optimization now only guards the NVS write-back (to avoid flash
 * wear), not the whole update. Verify: a changed value writes NVS, an equal value does not,
 * and a subsequent changed value writes again. */
void test_param_model_same_value_update_skips_nvs(void)
{
    esp_rmaker_node_t *node = __setup();

    const char *device_id = "nvs_dev";
    esp_rmaker_device_t *device = esp_rmaker_device_create(device_id, "type", NULL);
    TEST_ASSERT_NOT_NULL(device);
    esp_rmaker_param_t *param = esp_rmaker_param_create("pp", "itype", esp_rmaker_int(5), PROP_FLAG_PERSIST);
    TEST_ASSERT_NOT_NULL(param);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(device, param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, device));

    esp_rmaker_param_val_t stored;

    /* Changed value (5 -> 10): NVS must be written. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(param, esp_rmaker_int(10)));
    __check_val_equal(esp_rmaker_int(10), esp_rmaker_param_get_val(param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_get_stored_value((_esp_rmaker_param_t *)param, &stored));
    __check_val_equal(esp_rmaker_int(10), &stored);

    /* Plant a sentinel in NVS that diverges from the in-RAM value (still 10). */
    __plant_int_in_nvs(device_id, "pp", 777);

    /* Equal value (10 -> 10): the write-back must be skipped, so the sentinel survives.
     * The call still succeeds and the in-RAM value is unchanged. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(param, esp_rmaker_int(10)));
    __check_val_equal(esp_rmaker_int(10), esp_rmaker_param_get_val(param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_get_stored_value((_esp_rmaker_param_t *)param, &stored));
    __check_val_equal(esp_rmaker_int(777), &stored);

    /* Changed value (10 -> 20): NVS must be written again, overwriting the sentinel. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(param, esp_rmaker_int(20)));
    __check_val_equal(esp_rmaker_int(20), esp_rmaker_param_get_val(param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_get_stored_value((_esp_rmaker_param_t *)param, &stored));
    __check_val_equal(esp_rmaker_int(20), &stored);

    __teardown(node);
}

void test_param_model_store_get_invalid_parent(void)
{
    /* invalid: no parent */
    _esp_rmaker_param_t orphan;
    memset(&orphan, 0, sizeof(orphan));
    esp_rmaker_param_val_t v;
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_get_stored_value(&orphan, &v));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_store_value(&orphan));
}

void test_param_model_error_paths(void)
{
    /* NULL param handle checks */
    esp_rmaker_param_t *param = NULL;
    TEST_ASSERT_NULL(esp_rmaker_param_get_id(param));
    TEST_ASSERT_NULL(esp_rmaker_param_get_type(param));
    TEST_ASSERT_NULL(esp_rmaker_param_get_val(param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_add_bounds(param, esp_rmaker_int(0), esp_rmaker_int(10), esp_rmaker_int(1)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update(param, esp_rmaker_int(1)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update_and_report(param, esp_rmaker_int(1)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update_and_notify(param, esp_rmaker_int(1)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_get_stored_value((_esp_rmaker_param_t *)param, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_store_value((_esp_rmaker_param_t *)param));
}

static esp_rmaker_error_t __test_param_model_handle_set_payload_device_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param, const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    return esp_rmaker_param_update(param, val);
}

static void __check_val_equal(esp_rmaker_param_val_t expected, esp_rmaker_param_val_t *actual)
{
    TEST_ASSERT_EQUAL(expected.type, actual->type);
    switch (expected.type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        TEST_ASSERT_EQUAL(expected.val.b, actual->val.b);
        break;
    case RMAKER_VAL_TYPE_INTEGER:
        TEST_ASSERT_EQUAL(expected.val.i, actual->val.i);
        break;
    case RMAKER_VAL_TYPE_FLOAT:
        TEST_ASSERT_EQUAL(expected.val.f, actual->val.f);
        break;
    case RMAKER_VAL_TYPE_STRING:
    case RMAKER_VAL_TYPE_OBJECT:
    case RMAKER_VAL_TYPE_ARRAY:
        TEST_ASSERT_EQUAL_STRING(expected.val.s, actual->val.s);
        break;
    default:
        break;
    }
}

void test_param_model_handle_set_payload(void)
{
    esp_rmaker_node_t *node = __setup();

    const char *device_id = "device";
    const char *device_type = "type";
    esp_rmaker_device_t *device = esp_rmaker_device_create(device_id, device_type, NULL);

    /* Make parameter of every value type */
    esp_rmaker_param_t *params[6];
    params[0] = esp_rmaker_param_create("p1", "type1", esp_rmaker_int(1), PROP_FLAG_READ);
    params[1] = esp_rmaker_param_create("p2", "type2", esp_rmaker_float(1.0f), PROP_FLAG_READ);
    params[2] = esp_rmaker_param_create("p3", "type3", esp_rmaker_bool(true), PROP_FLAG_READ);
    params[3] = esp_rmaker_param_create("p4", "type4", esp_rmaker_str("str"), PROP_FLAG_READ);
    params[4] = esp_rmaker_param_create("p5", "type5", esp_rmaker_obj("{\"k\":\"v\"}"), PROP_FLAG_READ);
    params[5] = esp_rmaker_param_create("p6", "type6", esp_rmaker_array("[1,2,3]"), PROP_FLAG_READ);

    /* Add parameters to device */
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(device, params[i]));
    }

    /* Add write callback to device */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_cb(device, __test_param_model_handle_set_payload_device_cb, NULL));

    /* Add device to node */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, device));

    /* Set correct payload */
    char payload[1024];
    snprintf(payload, sizeof(payload), "{\"%s\":{\"p1\":2,\"p2\":2.0,\"p3\":false,\"p4\":\"str2\",\"p5\":{\"k\":\"v2\"},\"p6\":[1,2,3,4]}}", device_id);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_handle_update_payload_json(esp_rmaker_get_node(), payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD));

    /* Check values */
    __check_val_equal(esp_rmaker_int(2), esp_rmaker_param_get_val(params[0]));
    __check_val_equal(esp_rmaker_float(2.0f), esp_rmaker_param_get_val(params[1]));
    __check_val_equal(esp_rmaker_bool(false), esp_rmaker_param_get_val(params[2]));
    __check_val_equal(esp_rmaker_str("str2"), esp_rmaker_param_get_val(params[3]));
    __check_val_equal(esp_rmaker_obj("{\"k\":\"v2\"}"), esp_rmaker_param_get_val(params[4]));
    __check_val_equal(esp_rmaker_array("[1,2,3,4]"), esp_rmaker_param_get_val(params[5]));

    /* Retry payload and check no update */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_handle_update_payload_json(esp_rmaker_get_node(), payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD));
    __check_val_equal(esp_rmaker_int(2), esp_rmaker_param_get_val(params[0]));
    __check_val_equal(esp_rmaker_float(2.0f), esp_rmaker_param_get_val(params[1]));
    __check_val_equal(esp_rmaker_bool(false), esp_rmaker_param_get_val(params[2]));
    __check_val_equal(esp_rmaker_str("str2"), esp_rmaker_param_get_val(params[3]));
    __check_val_equal(esp_rmaker_obj("{\"k\":\"v2\"}"), esp_rmaker_param_get_val(params[4]));
    __check_val_equal(esp_rmaker_array("[1,2,3,4]"), esp_rmaker_param_get_val(params[5]));

    /* Invalid JSON testing */
    snprintf(payload, sizeof(payload), "{\"%s\":{\"p5\":{\"k\":\"v2},\"p6\":[1,2,,],\"p7\":{\"k\":\"v\"},\"p8\":[4,5]}}", device_id);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, data_model_state_handle_update_payload_json(esp_rmaker_get_node(), payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD));

    /* Type mismatch testing */
    snprintf(payload, sizeof(payload), "{\"%s\":{\"p1\":\"2\",\"p2\":\"2.0\",\"p3\":\"false\",\"p4\":25,\"p5\":{\"k\":\"v2\"},\"p6\":[1,2,3,4]}}", device_id);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_handle_update_payload_json(esp_rmaker_get_node(), payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD));

    /* Check values */
    __check_val_equal(esp_rmaker_int(2), esp_rmaker_param_get_val(params[0]));
    __check_val_equal(esp_rmaker_float(2.0f), esp_rmaker_param_get_val(params[1]));
    __check_val_equal(esp_rmaker_bool(false), esp_rmaker_param_get_val(params[2]));
    __check_val_equal(esp_rmaker_str("str2"), esp_rmaker_param_get_val(params[3]));
    __check_val_equal(esp_rmaker_obj("{\"k\":\"v2\"}"), esp_rmaker_param_get_val(params[4]));
    __check_val_equal(esp_rmaker_array("[1,2,3,4]"), esp_rmaker_param_get_val(params[5]));

    /* Remove bulk write callback and try again */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_bulk_cb(device, NULL, NULL));
    snprintf(payload, sizeof(payload), "{\"%s\":{\"p1\":3,\"p2\":4.1,\"p3\":true,\"p4\":\"str3\",\"p5\":{\"k\":\"v3\"},\"p6\":[1,2,3,4,5]}}", device_id);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_handle_update_payload_json(esp_rmaker_get_node(), payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD));

    /* Check values */
    __check_val_equal(esp_rmaker_int(2), esp_rmaker_param_get_val(params[0]));
    __check_val_equal(esp_rmaker_float(2.0f), esp_rmaker_param_get_val(params[1]));
    __check_val_equal(esp_rmaker_bool(false), esp_rmaker_param_get_val(params[2]));
    __check_val_equal(esp_rmaker_str("str2"), esp_rmaker_param_get_val(params[3]));
    __check_val_equal(esp_rmaker_obj("{\"k\":\"v2\"}"), esp_rmaker_param_get_val(params[4]));
    __check_val_equal(esp_rmaker_array("[1,2,3,4]"), esp_rmaker_param_get_val(params[5]));

    /* Clean up */
    __teardown(node);
}

void test_param_model_handle_set_payload_typed(void)
{
    esp_rmaker_node_t *node = __setup();

    /* Two devices, distinct types */
    const char *dev_a_type = "esp.device.light";
    const char *dev_b_type = "esp.device.fan";
    esp_rmaker_device_t *dev_a = esp_rmaker_device_create("light_1", dev_a_type, NULL);
    esp_rmaker_device_t *dev_b = esp_rmaker_device_create("fan_1", dev_b_type, NULL);

    /* Distinct param types per device */
    esp_rmaker_param_t *a_power = esp_rmaker_param_create("Power", "esp.param.power", esp_rmaker_bool(false), PROP_FLAG_READ);
    esp_rmaker_param_t *a_brightness = esp_rmaker_param_create("Brightness", "esp.param.brightness", esp_rmaker_int(10), PROP_FLAG_READ);
    esp_rmaker_param_t *a_name = esp_rmaker_param_create("Name", "esp.param.name", esp_rmaker_str("a"), PROP_FLAG_READ);
    esp_rmaker_param_t *b_speed = esp_rmaker_param_create("Speed", "esp.param.speed", esp_rmaker_int(0), PROP_FLAG_READ);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev_a, a_power));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev_a, a_brightness));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev_a, a_name));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev_b, b_speed));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_cb(dev_a, __test_param_model_handle_set_payload_device_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_cb(dev_b, __test_param_model_handle_set_payload_device_cb, NULL));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, dev_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, dev_b));

    char payload[512];

    /* Apply only to device-type A: power, brightness, name. Device B untouched. */
    snprintf(payload, sizeof(payload),
             "{\"%s\":{\"params\":{\"esp.param.power\":true,\"esp.param.brightness\":75,\"esp.param.name\":\"b\"}}}",
             dev_a_type);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_handle_update_payload_json_group(esp_rmaker_get_node(), payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD));

    __check_val_equal(esp_rmaker_bool(true), esp_rmaker_param_get_val(a_power));
    __check_val_equal(esp_rmaker_int(75), esp_rmaker_param_get_val(a_brightness));
    __check_val_equal(esp_rmaker_str("b"), esp_rmaker_param_get_val(a_name));
    __check_val_equal(esp_rmaker_int(0), esp_rmaker_param_get_val(b_speed));

    /* Retry: idempotent */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_handle_update_payload_json_group(esp_rmaker_get_node(), payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD));
    __check_val_equal(esp_rmaker_bool(true), esp_rmaker_param_get_val(a_power));
    __check_val_equal(esp_rmaker_int(75), esp_rmaker_param_get_val(a_brightness));
    __check_val_equal(esp_rmaker_str("b"), esp_rmaker_param_get_val(a_name));
    __check_val_equal(esp_rmaker_int(0), esp_rmaker_param_get_val(b_speed));

    /* Mismatched device-type: no device matches; nothing changes. */
    snprintf(payload, sizeof(payload),
             "{\"esp.device.unknown\":{\"params\":{\"esp.param.power\":false}}}");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_handle_update_payload_json_group(esp_rmaker_get_node(), payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD));
    __check_val_equal(esp_rmaker_bool(true), esp_rmaker_param_get_val(a_power));
    __check_val_equal(esp_rmaker_int(75), esp_rmaker_param_get_val(a_brightness));
    __check_val_equal(esp_rmaker_int(0), esp_rmaker_param_get_val(b_speed));

    /* Multi-type payload: B updates, A unchanged on speed (not its param). */
    snprintf(payload, sizeof(payload),
             "{\"%s\":{\"params\":{\"esp.param.speed\":3}},\"%s\":{\"params\":{\"esp.param.power\":false}}}",
             dev_b_type, dev_a_type);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_handle_update_payload_json_group(esp_rmaker_get_node(), payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD));
    __check_val_equal(esp_rmaker_int(3), esp_rmaker_param_get_val(b_speed));
    __check_val_equal(esp_rmaker_bool(false), esp_rmaker_param_get_val(a_power));

    /* Type-mismatch values on a known device-type: safe, no change. */
    snprintf(payload, sizeof(payload),
             "{\"%s\":{\"params\":{\"esp.param.power\":\"yes\",\"esp.param.brightness\":\"high\"}}}",
             dev_a_type);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_handle_update_payload_json_group(esp_rmaker_get_node(), payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD));
    __check_val_equal(esp_rmaker_bool(false), esp_rmaker_param_get_val(a_power));
    __check_val_equal(esp_rmaker_int(75), esp_rmaker_param_get_val(a_brightness));

    /* Missing "params" envelope: no change. */
    snprintf(payload, sizeof(payload),
             "{\"%s\":{\"esp.param.power\":true}}", dev_a_type);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, data_model_state_handle_update_payload_json_group(esp_rmaker_get_node(), payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD));
    __check_val_equal(esp_rmaker_bool(false), esp_rmaker_param_get_val(a_power));

    /* Invalid JSON: error. */
    snprintf(payload, sizeof(payload),
             "{\"%s\":{\"params\":{\"esp.param.power\":true}}", dev_a_type);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, data_model_state_handle_update_payload_json_group(esp_rmaker_get_node(), payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD));

    __teardown(node);
}

void test_node_model_get_node_params_multiple_data_types(void)
{
    esp_rmaker_node_t *node = __setup();

    char *err_msg = NULL;
    esp_rmaker_error_t err;
    int ret;

    /* Create devices */
    esp_rmaker_device_t *device1 = esp_rmaker_device_create("device1", "test_type", NULL);
    if (device1 == NULL) {
        err_msg = "Failed to create device1";
        goto test_get_node_params_end;
    }

    esp_rmaker_device_t *device2 = esp_rmaker_device_create("device2", "test_type", NULL);
    if (device2 == NULL) {
        err_msg = "Failed to create device2";
        goto test_get_node_params_end;
    }

    /* Create parameters with different data types */
    esp_rmaker_param_t *param_int = esp_rmaker_param_create("param_int", "int_type", esp_rmaker_int(42), PROP_FLAG_READ);
    if (param_int == NULL) {
        err_msg = "Failed to create integer parameter";
        goto test_get_node_params_end;
    }

    esp_rmaker_param_t *param_float = esp_rmaker_param_create("param_float", "float_type", esp_rmaker_float(3.14f), PROP_FLAG_READ);
    if (param_float == NULL) {
        err_msg = "Failed to create float parameter";
        goto test_get_node_params_end;
    }

    esp_rmaker_param_t *param_bool = esp_rmaker_param_create("param_bool", "bool_type", esp_rmaker_bool(true), PROP_FLAG_READ);
    if (param_bool == NULL) {
        err_msg = "Failed to create boolean parameter";
        goto test_get_node_params_end;
    }

    esp_rmaker_param_t *param_str = esp_rmaker_param_create("param_str", "str_type", esp_rmaker_str("hello world"), PROP_FLAG_READ);
    if (param_str == NULL) {
        err_msg = "Failed to create string parameter";
        goto test_get_node_params_end;
    }

    esp_rmaker_param_t *param_obj = esp_rmaker_param_create("param_obj", "obj_type", esp_rmaker_obj("{\"key\":\"value\"}"), PROP_FLAG_READ);
    if (param_obj == NULL) {
        err_msg = "Failed to create object parameter";
        goto test_get_node_params_end;
    }

    esp_rmaker_param_t *param_arr = esp_rmaker_param_create("param_arr", "arr_type", esp_rmaker_array("[1,2,3]"), PROP_FLAG_READ);
    if (param_arr == NULL) {
        err_msg = "Failed to create array parameter";
        goto test_get_node_params_end;
    }

    /* Add parameters to devices */
    err = esp_rmaker_device_add_param(device1, param_int);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add integer parameter to device1";
        goto test_get_node_params_end;
    }
    err = esp_rmaker_device_add_param(device1, param_float);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add float parameter to device1";
        goto test_get_node_params_end;
    }
    err = esp_rmaker_device_add_param(device1, param_bool);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add boolean parameter to device1";
        goto test_get_node_params_end;
    }

    err = esp_rmaker_device_add_param(device2, param_str);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add string parameter to device2";
        goto test_get_node_params_end;
    }
    err = esp_rmaker_device_add_param(device2, param_obj);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add object parameter to device2";
        goto test_get_node_params_end;
    }
    err = esp_rmaker_device_add_param(device2, param_arr);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add array parameter to device2";
        goto test_get_node_params_end;
    }

    /* Add devices to node */
    err = esp_rmaker_node_add_device(node, device1);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add device1 to node";
        goto test_get_node_params_end;
    }
    err = esp_rmaker_node_add_device(node, device2);
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to add device2 to node";
        goto test_get_node_params_end;
    }

    /* Get node parameters */
    char *node_params = data_model_node_get_node_params();
    if (node_params == NULL) {
        err_msg = "Failed to get node parameters";
        goto test_get_node_params_end;
    }

    /* Parse the JSON response */
    jparse_ctx_t jctx;
    ret = json_parse_start(&jctx, node_params, strlen(node_params));
    if (ret != OS_SUCCESS) {
        err_msg = "Failed to parse node parameters JSON";
        goto test_get_node_params_end;
    }

    /* Check device1 parameters */
    if (json_obj_get_object(&jctx, "device1") != OS_SUCCESS) {
        err_msg = "Failed to get device1 object";
        goto test_get_node_params_parse_end;
    }

    /* Check integer parameter */
    int int_val;
    if (json_obj_get_int(&jctx, "param_int", &int_val) != OS_SUCCESS || int_val != 42) {
        err_msg = "Integer parameter value is incorrect";
        goto test_get_node_params_parse_end;
    }

    /* Check float parameter */
    float float_val;
    if (json_obj_get_float(&jctx, "param_float", &float_val) != OS_SUCCESS || float_val != 3.14f) {
        err_msg = "Float parameter value is incorrect";
        goto test_get_node_params_parse_end;
    }

    /* Check boolean parameter */
    bool bool_val;
    if (json_obj_get_bool(&jctx, "param_bool", &bool_val) != OS_SUCCESS || bool_val != true) {
        err_msg = "Boolean parameter value is incorrect";
        goto test_get_node_params_parse_end;
    }

    json_obj_leave_object(&jctx);

    /* Check device2 parameters */
    if (json_obj_get_object(&jctx, "device2") != OS_SUCCESS) {
        err_msg = "Failed to get device2 object";
        goto test_get_node_params_parse_end;
    }

    /* Check string parameter */
    char str_val[64];
    if (json_obj_get_string(&jctx, "param_str", str_val, sizeof(str_val)) != OS_SUCCESS ||
            strcmp(str_val, "hello world") != 0) {
        err_msg = "String parameter value is incorrect";
        goto test_get_node_params_parse_end;
    }

    /* Check object parameter */
    char obj_val[64];
    if (json_obj_get_object_str(&jctx, "param_obj", obj_val, sizeof(obj_val)) != OS_SUCCESS ||
            strcmp(obj_val, "{\"key\":\"value\"}") != 0) {
        err_msg = "Object parameter value is incorrect";
        goto test_get_node_params_parse_end;
    }

    /* Check array parameter */
    char arr_val[64];
    if (json_obj_get_array_str(&jctx, "param_arr", arr_val, sizeof(arr_val)) != OS_SUCCESS ||
            strcmp(arr_val, "[1,2,3]") != 0) {
        err_msg = "Array parameter value is incorrect";
        goto test_get_node_params_parse_end;
    }

    json_obj_leave_object(&jctx);

test_get_node_params_parse_end:
    json_parse_end(&jctx);

    /* Free the node parameters string */
    free(node_params);

test_get_node_params_end:
    __teardown(node);

    if (err_msg) {
        TEST_FAIL_MESSAGE(err_msg);
    } else {
        TEST_PASS();
    }
}

void test_device_create_rejects_path_separator(void)
{
    esp_rmaker_device_t *device = esp_rmaker_device_create("dev.bad", "test_type", NULL);
    TEST_ASSERT_NULL_MESSAGE(device, "Device create should reject id containing path separator");
}

void test_param_create_rejects_path_separator(void)
{
    esp_rmaker_param_t *param = esp_rmaker_param_create("par.bad", "test_type", esp_rmaker_int(0), PROP_FLAG_READ);
    TEST_ASSERT_NULL_MESSAGE(param, "Param create should reject id containing path separator");
}

void test_path_round_trip(void)
{
    esp_rmaker_node_t *node = __setup();

    esp_rmaker_device_t *device = esp_rmaker_device_create("LightDev", "test_type", NULL);
    TEST_ASSERT_NOT_NULL(device);
    esp_rmaker_param_t *param = esp_rmaker_param_create("Power", "test_type", esp_rmaker_bool(false), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(param);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(device, param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, device));

    esp_rmaker_state_update_id_t uid = esp_rmaker_state_update_id_create(param);
    TEST_ASSERT_NOT_NULL(uid);

    char *path = data_model_update_id_to_path(uid);
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_STRING("LightDev.Power", path);

    esp_rmaker_state_update_id_t parsed = data_model_path_to_update_id(path);
    TEST_ASSERT_NOT_NULL(parsed);
    TEST_ASSERT_EQUAL(0, data_model_state_update_id_compare(uid, parsed));

    free(path);
    data_model_state_update_id_release(uid);
    data_model_state_update_id_release(parsed);
    __teardown(node);
}

void test_path_to_update_id_invalid(void)
{
    esp_rmaker_node_t *node = __setup();
    TEST_ASSERT_NULL(data_model_path_to_update_id(NULL));
    TEST_ASSERT_NULL(data_model_path_to_update_id("no_separator"));
    TEST_ASSERT_NULL(data_model_path_to_update_id(".missing_device"));
    TEST_ASSERT_NULL(data_model_path_to_update_id("missing_param."));
    TEST_ASSERT_NULL(data_model_path_to_update_id("nope.also_nope"));
    __teardown(node);
}
