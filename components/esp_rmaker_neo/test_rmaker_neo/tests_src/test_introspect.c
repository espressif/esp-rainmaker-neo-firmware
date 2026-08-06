/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_introspect.c
 * @brief Tests for data model introspection, param update observer and write injection.
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "esp_rmaker_flow.h"
#include "esp_rmaker_data_model.h"
#include "esp_rmaker_data_model_introspect.h"
#include "esp_rmaker_node.h"

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
    esp_rmaker_node_clear_stored_values(node);
    esp_rmaker_node_deinit(node);
}

/* Enumeration *****************************************************************/

typedef struct {
    int total;
    int services;
    int stop_after;
} __enum_ctx_t;

static esp_rmaker_error_t __count_device_visitor(const esp_rmaker_device_t *device, void *priv)
{
    __enum_ctx_t *ctx = (__enum_ctx_t *)priv;
    ctx->total++;
    if (esp_rmaker_device_is_service(device)) {
        ctx->services++;
    }
    if (ctx->stop_after && ctx->total >= ctx->stop_after) {
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __count_param_visitor(const esp_rmaker_param_t *param, void *priv)
{
    __enum_ctx_t *ctx = (__enum_ctx_t *)priv;
    (void)param;
    ctx->total++;
    if (ctx->stop_after && ctx->total >= ctx->stop_after) {
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

/* The walk visits a snapshot with the node lock released, so a visitor may call
 * APIs that take that lock. */
static esp_rmaker_error_t __locking_param_visitor(const esp_rmaker_param_t *param, void *priv)
{
    __enum_ctx_t *ctx = (__enum_ctx_t *)priv;
    ctx->total++;
    esp_rmaker_param_val_t *val = esp_rmaker_param_get_val((esp_rmaker_param_t *)param);
    TEST_ASSERT_NOT_NULL(val);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(param, *val));
    return ESP_RMAKER_OK;
}

void test_introspect_enumeration(void)
{
    esp_rmaker_node_t *node = __setup();

    esp_rmaker_device_t *light = esp_rmaker_device_create("Light", "esp.device.lightbulb", NULL);
    TEST_ASSERT_NOT_NULL(light);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light,
                      esp_rmaker_param_create("Power", "esp.param.power", esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light,
                      esp_rmaker_param_create("Brightness", "esp.param.brightness", esp_rmaker_int(50), PROP_FLAG_READ | PROP_FLAG_WRITE)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light,
                      esp_rmaker_param_create("CCT", "esp.param.cct", esp_rmaker_int(4000), PROP_FLAG_READ | PROP_FLAG_WRITE)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, light));

    esp_rmaker_device_t *service = esp_rmaker_service_create("TestService", "esp.service.test", NULL);
    TEST_ASSERT_NOT_NULL(service);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, service));

    /* Count devices, distinguishing services */
    __enum_ctx_t dev_ctx = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_for_each_device(node, __count_device_visitor, &dev_ctx));
    TEST_ASSERT_EQUAL(2, dev_ctx.total);
    TEST_ASSERT_EQUAL(1, dev_ctx.services);

    /* Early stop propagates the visitor's return value */
    __enum_ctx_t stop_ctx = {.stop_after = 1};
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_node_for_each_device(node, __count_device_visitor, &stop_ctx));
    TEST_ASSERT_EQUAL(1, stop_ctx.total);

    /* Count params */
    __enum_ctx_t param_ctx = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_for_each_param(light, __count_param_visitor, &param_ctx));
    TEST_ASSERT_EQUAL(3, param_ctx.total);

    /* A visitor may call APIs that take the node lock */
    __enum_ctx_t locking_ctx = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_for_each_param(light, __locking_param_visitor, &locking_ctx));
    TEST_ASSERT_EQUAL(3, locking_ctx.total);

    /* Invalid args */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_for_each_device(NULL, __count_device_visitor, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_for_each_device(node, NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_for_each_param(NULL, __count_param_visitor, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_for_each_param(light, NULL, NULL));

    __teardown(node);
}

/* Metadata getters ************************************************************/

void test_introspect_metadata_getters(void)
{
    esp_rmaker_node_t *node = __setup();

    esp_rmaker_device_t *light = esp_rmaker_device_create("Light", "esp.device.lightbulb", NULL);
    TEST_ASSERT_NOT_NULL(light);
    esp_rmaker_param_t *cct = esp_rmaker_param_create("CCT", "esp.param.cct", esp_rmaker_int(4000), PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_NOT_NULL(cct);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_add_bounds(cct, esp_rmaker_int(2700), esp_rmaker_int(6500), esp_rmaker_int(100)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_add_ui_type(cct, "esp.ui.slider"));
    esp_rmaker_param_t *power = esp_rmaker_param_create("Power", "esp.param.power", esp_rmaker_bool(false), PROP_FLAG_WRITE);
    TEST_ASSERT_NOT_NULL(power);

    /* Before the param is added to a device, it has no device */
    TEST_ASSERT_NULL(esp_rmaker_param_get_device(cct));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, cct));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, power));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, light));

    /* Bounds copy-out, with NULL out-pointers allowed */
    esp_rmaker_param_val_t min, max, step;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_get_bounds(cct, &min, &max, &step));
    TEST_ASSERT_EQUAL(2700, min.val.i);
    TEST_ASSERT_EQUAL(6500, max.val.i);
    TEST_ASSERT_EQUAL(100, step.val.i);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_get_bounds(cct, &min, NULL, NULL));

    /* No bounds on the power param */
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, esp_rmaker_param_get_bounds(power, &min, &max, &step));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_get_bounds(NULL, &min, &max, &step));

    /* UI type */
    TEST_ASSERT_EQUAL_STRING("esp.ui.slider", esp_rmaker_param_get_ui_type(cct));
    TEST_ASSERT_NULL(esp_rmaker_param_get_ui_type(power));
    TEST_ASSERT_NULL(esp_rmaker_param_get_ui_type(NULL));

    /* Prop flags */
    TEST_ASSERT_EQUAL(PROP_FLAG_READ | PROP_FLAG_WRITE, esp_rmaker_param_get_prop_flags(cct));
    TEST_ASSERT_EQUAL(PROP_FLAG_WRITE, esp_rmaker_param_get_prop_flags(power));
    TEST_ASSERT_EQUAL(0, esp_rmaker_param_get_prop_flags(NULL));

    /* Parent device */
    TEST_ASSERT_EQUAL_PTR(light, esp_rmaker_param_get_device(cct));
    TEST_ASSERT_NULL(esp_rmaker_param_get_device(NULL));

    /* Service check */
    TEST_ASSERT_FALSE(esp_rmaker_device_is_service(light));
    TEST_ASSERT_FALSE(esp_rmaker_device_is_service(NULL));

    __teardown(node);
}

/* Param update observer *******************************************************/

typedef struct {
    int count;
    const esp_rmaker_device_t *last_device;
    const esp_rmaker_param_t *last_param;
    esp_rmaker_param_val_t last_val;
} __observer_ctx_t;

static void __test_observer(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                            const esp_rmaker_param_val_t *val, void *priv)
{
    __observer_ctx_t *ctx = (__observer_ctx_t *)priv;
    ctx->count++;
    ctx->last_device = device;
    ctx->last_param = param;
    ctx->last_val = *val;
}

static void __test_observer_secondary(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                                      const esp_rmaker_param_val_t *val, void *priv)
{
    __test_observer(device, param, val, priv);
}

void test_introspect_param_update_observer(void)
{
    esp_rmaker_node_t *node = __setup();

    esp_rmaker_device_t *light = esp_rmaker_device_create("Light", "esp.device.lightbulb", NULL);
    TEST_ASSERT_NOT_NULL(light);
    esp_rmaker_param_t *brightness = esp_rmaker_param_create("Brightness", "esp.param.brightness",
                                     esp_rmaker_int(50), PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_NOT_NULL(brightness);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, brightness));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, light));

    __observer_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update_observer_register(NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update_observer_register(__test_observer, &ctx));
    TEST_ASSERT_EQUAL(ESP_RMAKER_ALREADY_EXISTS, esp_rmaker_param_update_observer_register(__test_observer, &ctx));

    /* Update fires the observer with device, param and applied value */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(brightness, esp_rmaker_int(70)));
    TEST_ASSERT_EQUAL(1, ctx.count);
    TEST_ASSERT_EQUAL_PTR(light, ctx.last_device);
    TEST_ASSERT_EQUAL_PTR(brightness, ctx.last_param);
    TEST_ASSERT_EQUAL(RMAKER_VAL_TYPE_INTEGER, ctx.last_val.type);
    TEST_ASSERT_EQUAL(70, ctx.last_val.val.i);

    /* Equal-value update still fires (per the observer contract) */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(brightness, esp_rmaker_int(70)));
    TEST_ASSERT_EQUAL(2, ctx.count);

    /* Rejected update (type mismatch) does not fire */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update(brightness, esp_rmaker_bool(true)));
    TEST_ASSERT_EQUAL(2, ctx.count);

    /* Multiple observers both fire */
    __observer_ctx_t ctx2 = {0};
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update_observer_register(__test_observer_secondary, &ctx2));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(brightness, esp_rmaker_int(30)));
    TEST_ASSERT_EQUAL(3, ctx.count);
    TEST_ASSERT_EQUAL(1, ctx2.count);

    /* Unregister stops notifications */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update_observer_unregister(__test_observer));
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, esp_rmaker_param_update_observer_unregister(__test_observer));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(brightness, esp_rmaker_int(40)));
    TEST_ASSERT_EQUAL(3, ctx.count);
    TEST_ASSERT_EQUAL(2, ctx2.count);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update_observer_unregister(__test_observer_secondary));

    __teardown(node);
}

/* Write injection *************************************************************/

typedef struct {
    int count;
    uint8_t last_num_params;
    esp_rmaker_req_src_t last_src;
    esp_rmaker_param_val_t last_val;
} __write_ctx_t;

static __write_ctx_t __bulk_ctx;

static esp_rmaker_error_t __test_bulk_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
        uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    (void)device;
    (void)priv_data;
    __bulk_ctx.count++;
    __bulk_ctx.last_num_params = count;
    __bulk_ctx.last_src = ctx->src;
    if (count > 0) {
        __bulk_ctx.last_val = write_req[0].val;
    }
    return ESP_RMAKER_OK;
}

static __write_ctx_t __single_ctx;

static esp_rmaker_error_t __test_single_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    (void)device;
    (void)param;
    (void)priv_data;
    __single_ctx.count++;
    __single_ctx.last_src = ctx->src;
    __single_ctx.last_val = val;
    return ESP_RMAKER_OK;
}

void test_introspect_device_write_params(void)
{
    esp_rmaker_node_t *node = __setup();
    memset(&__bulk_ctx, 0, sizeof(__bulk_ctx));
    memset(&__single_ctx, 0, sizeof(__single_ctx));

    /* Device with a bulk write callback */
    esp_rmaker_device_t *light = esp_rmaker_device_create("Light", "esp.device.lightbulb", NULL);
    TEST_ASSERT_NOT_NULL(light);
    esp_rmaker_param_t *power = esp_rmaker_param_create("Power", "esp.param.power", esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_t *brightness = esp_rmaker_param_create("Brightness", "esp.param.brightness",
                                     esp_rmaker_int(50), PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, power));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, brightness));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_bulk_cb(light, __test_bulk_write_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, light));

    /* Second device, used for the ownership check */
    esp_rmaker_device_t *other = esp_rmaker_device_create("Other", "esp.device.other", NULL);
    TEST_ASSERT_NOT_NULL(other);
    esp_rmaker_param_t *other_param = esp_rmaker_param_create("Power", "esp.param.power", esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(other, other_param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, other));

    /* Basic injection reaches the bulk callback with the given source */
    esp_rmaker_param_write_req_t req[2] = {
        {.param = power, .val = esp_rmaker_bool(true)},
        {.param = brightness, .val = esp_rmaker_int(80)},
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_write_params(light, req, 2, ESP_RMAKER_REQ_SRC_EXTERNAL));
    TEST_ASSERT_EQUAL(1, __bulk_ctx.count);
    TEST_ASSERT_EQUAL(2, __bulk_ctx.last_num_params);
    TEST_ASSERT_EQUAL(ESP_RMAKER_REQ_SRC_EXTERNAL, __bulk_ctx.last_src);
    TEST_ASSERT_TRUE(__bulk_ctx.last_val.val.b);

    /* Param belonging to another device is rejected */
    esp_rmaker_param_write_req_t bad_owner[1] = {
        {.param = other_param, .val = esp_rmaker_bool(true)},
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_write_params(light, bad_owner, 1, ESP_RMAKER_REQ_SRC_EXTERNAL));

    /* Value type mismatch is rejected */
    esp_rmaker_param_write_req_t bad_type[1] = {
        {.param = power, .val = esp_rmaker_int(1)},
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_write_params(light, bad_type, 1, ESP_RMAKER_REQ_SRC_EXTERNAL));

    /* Invalid args */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_write_params(NULL, req, 1, ESP_RMAKER_REQ_SRC_EXTERNAL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_write_params(light, NULL, 1, ESP_RMAKER_REQ_SRC_EXTERNAL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_write_params(light, req, 0, ESP_RMAKER_REQ_SRC_EXTERNAL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_write_params(light, req, 1, ESP_RMAKER_REQ_SRC_MAX));

    TEST_ASSERT_EQUAL(1, __bulk_ctx.count);

    /* Device registered with a per-param write callback is reached via the
     * default bulk callback wrapper */
    esp_rmaker_device_t *single = esp_rmaker_device_create("Single", "esp.device.single", NULL);
    TEST_ASSERT_NOT_NULL(single);
    esp_rmaker_param_t *single_power = esp_rmaker_param_create("Power", "esp.param.power", esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(single, single_power));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_cb(single, __test_single_write_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, single));

    esp_rmaker_param_write_req_t single_req[1] = {
        {.param = single_power, .val = esp_rmaker_bool(true)},
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_write_params(single, single_req, 1, ESP_RMAKER_REQ_SRC_EXTERNAL));
    TEST_ASSERT_EQUAL(1, __single_ctx.count);
    TEST_ASSERT_EQUAL(ESP_RMAKER_REQ_SRC_EXTERNAL, __single_ctx.last_src);
    TEST_ASSERT_TRUE(__single_ctx.last_val.val.b);

    __teardown(node);
}
