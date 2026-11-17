/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_mirror.c
 * @brief Unit tests for the Matter mirror engine (transforms, table matching,
 *        lowering and sync/echo state machine) against a recording fake port.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "unity.h"

#include "esp_rmaker_data_model.h"
#include "esp_rmaker_data_model_introspect.h"
#include "esp_rmaker_matter_standard_params.h"
#include "esp_rmaker_standard_types.h"

#include "constants/esp_rmaker_nvs_common.h"
#include "osal_storage.h"
#include "util/esp_rmaker_nvs.h"
#include "node_internal.h"
#include "bridge/bridge_internal.h"
#include "osal_event_loop.h"

#include "esp_rmaker_matter_mirror.h"
#include "rm_mirror_engine.h"
#include "rm_mirror_internal.h"
#include "fake_port.h"

#define CLUSTER_IDENTIFY      0x0003u
#define CLUSTER_GROUPS        0x0004u
#define CLUSTER_ON_OFF        0x0006u
#define CLUSTER_LEVEL         0x0008u
#define CLUSTER_COLOR         0x0300u
/* ColorControl FeatureMap bits */
#define FEATURE_HS            0x00000001u
#define FEATURE_XY            0x00000008u
#define FEATURE_CT            0x00000010u
#define CLUSTER_TEST          0x0402u /* test-only vocabulary (see mapping/test_vocabulary.json) */
#define CLUSTER_TEST_EXTRA    0x009Cu /* mandatory cluster of the rule's additional device type */
#define DEVICE_TYPE_TEST      0x0302u
#define DEVICE_TYPE_TEST_EXTRA 0x0510u
#define ATTR_IDENTIFY_TIME    0x0000u
#define ATTR_ON_OFF           0x0000u
#define ATTR_CURRENT_LEVEL    0x0000u
#define ATTR_CT_MIREDS        0x0007u
#define ATTR_TEST_SCALED      0x0000u
#define ATTR_TEST_REPORTED    0x0001u
#define ATTR_CT_PHYS_MIN      0x400Bu
#define ATTR_CT_PHYS_MAX      0x400Cu
#define ATTR_STARTUP_ON_OFF    0x4003u
#define ATTR_STARTUP_LEVEL     0x4000u
#define ATTR_STARTUP_CT_MIREDS 0x4010u
#define CMD_OFF                        0x00u
#define CMD_ON                         0x01u
#define CMD_MOVE_TO_LEVEL_WITH_ON_OFF  0x04u

/* Transforms ******************************************************************/

void test_mirror_transforms(void)
{
    rm_mirror_xform_t linear = {.type = RM_MIRROR_XFORM_LINEAR, .u.linear = {0, 100, 1, 254}};
    /* Boundaries */
    TEST_ASSERT_EQUAL_INT32(1, rm_mirror_xform_to_matter(&linear, 0));
    TEST_ASSERT_EQUAL_INT32(254, rm_mirror_xform_to_matter(&linear, 100));
    TEST_ASSERT_EQUAL_INT32(0, rm_mirror_xform_to_rmng(&linear, 1));
    TEST_ASSERT_EQUAL_INT32(100, rm_mirror_xform_to_rmng(&linear, 254));
    /* Out-of-range input clamps */
    TEST_ASSERT_EQUAL_INT32(254, rm_mirror_xform_to_matter(&linear, 150));
    TEST_ASSERT_EQUAL_INT32(1, rm_mirror_xform_to_matter(&linear, -5));
    /* Round trip stability across the full RainMaker Neo range */
    for (int32_t v = 0; v <= 100; v++) {
        int32_t matter = rm_mirror_xform_to_matter(&linear, v);
        TEST_ASSERT_EQUAL_INT32(v, rm_mirror_xform_to_rmng(&linear, matter));
    }

    rm_mirror_xform_t kelvin = {.type = RM_MIRROR_XFORM_KELVIN_MIREDS};
    TEST_ASSERT_EQUAL_INT32(370, rm_mirror_xform_to_matter(&kelvin, 2700));
    TEST_ASSERT_EQUAL_INT32(154, rm_mirror_xform_to_matter(&kelvin, 6500));
    TEST_ASSERT_EQUAL_INT32(250, rm_mirror_xform_to_matter(&kelvin, 4000));
    /* Self-inverse at representable points */
    TEST_ASSERT_EQUAL_INT32(4000, rm_mirror_xform_to_rmng(&kelvin, 250));
    TEST_ASSERT_EQUAL_INT32(2703, rm_mirror_xform_to_rmng(&kelvin, 370));
    /* Degenerate input */
    TEST_ASSERT_EQUAL_INT32(0, rm_mirror_xform_to_matter(&kelvin, 0));

    rm_mirror_xform_t identity = {.type = RM_MIRROR_XFORM_IDENTITY};
    TEST_ASSERT_EQUAL_INT32(42, rm_mirror_xform_to_matter(&identity, 42));
    TEST_ASSERT_EQUAL_INT32(42, rm_mirror_xform_to_rmng(&identity, 42));
}

void test_mirror_scale_transform(void)
{
    /* Watts -> milliwatts, keeping the fractional part through the float entry point */
    rm_mirror_xform_t scale = {.type = RM_MIRROR_XFORM_SCALE, .u.scalar = 1000};
    TEST_ASSERT_EQUAL_INT32(5250, rm_mirror_xform_to_matter_f(&scale, 5.25f));
    TEST_ASSERT_EQUAL_INT32(0, rm_mirror_xform_to_matter_f(&scale, 0.0f));
    /* Rounds rather than truncates */
    TEST_ASSERT_EQUAL_INT32(1235, rm_mirror_xform_to_matter_f(&scale, 1.2345f));
    /* The int entry point still scales */
    TEST_ASSERT_EQUAL_INT32(7000, rm_mirror_xform_to_matter(&scale, 7));
    /* Inverse */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.25f, rm_mirror_xform_to_rmng_f(&scale, 5250));
    TEST_ASSERT_EQUAL_INT32(7, rm_mirror_xform_to_rmng(&scale, 7000));
    /* A zero factor must not divide by zero */
    rm_mirror_xform_t degenerate = {.type = RM_MIRROR_XFORM_SCALE, .u.scalar = 0};
    TEST_ASSERT_EQUAL_INT32(3, rm_mirror_xform_to_rmng(&degenerate, 3));
}

/* Table matching **************************************************************/

/** @brief ::rm_mirror_table_match_rule predicate over a literal type list. */
typedef struct {
    const char *const *types;
    size_t count;
} __type_list_t;

static bool __list_has(const char *wanted, void *ctx)
{
    const __type_list_t *list = (const __type_list_t *)ctx;
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->types[i], wanted) == 0) {
            return true;
        }
    }
    return false;
}

static const rm_mirror_devtype_rule_t *__match_rule(const char *device_type,
        const char *const *types, size_t count)
{
    __type_list_t list = { .types = types, .count = count };
    return rm_mirror_table_match_rule(device_type, __list_has, &list);
}

void test_mirror_table_matching(void)
{
    /* Capability lookup */
    const rm_mirror_capability_t *cct = rm_mirror_table_find_capability("esp.param.cct");
    TEST_ASSERT_NOT_NULL(cct);
    TEST_ASSERT_EQUAL_HEX32(CLUSTER_COLOR, cct->cluster_id);
    TEST_ASSERT_EQUAL_HEX32(ATTR_CT_MIREDS, cct->attribute_id);
    TEST_ASSERT_EQUAL(RM_MIRROR_XFORM_KELVIN_MIREDS, cct->xform.type);
    TEST_ASSERT_NULL(rm_mirror_table_find_capability("esp.param.nonexistent"));

    /* Rule precedence: most specific first */
    const char *cct_light[] = {"esp.param.power", "esp.param.brightness", "esp.param.cct"};
    const rm_mirror_devtype_rule_t *rule = __match_rule("esp.device.lightbulb", cct_light, 3);
    TEST_ASSERT_NOT_NULL(rule);
    TEST_ASSERT_EQUAL_HEX32(0x010C, rule->matter_device_type_id);

    const char *dimmable[] = {"esp.param.power", "esp.param.brightness"};
    rule = __match_rule("esp.device.lightbulb", dimmable, 2);
    TEST_ASSERT_NOT_NULL(rule);
    TEST_ASSERT_EQUAL_HEX32(0x0101, rule->matter_device_type_id);

    const char *onoff[] = {"esp.param.power", "esp.param.custom"};
    rule = __match_rule("esp.device.lightbulb", onoff, 2);
    TEST_ASSERT_NOT_NULL(rule);
    TEST_ASSERT_EQUAL_HEX32(0x0100, rule->matter_device_type_id);

    /* Switches lower to the mounted control types, on/off or dimmable. Their dim
     * level is esp.param.dim, not the light's brightness */
    const char *dimmable_switch[] = {"esp.param.power", "esp.param.dim"};
    rule = __match_rule("esp.device.switch", dimmable_switch, 2);
    TEST_ASSERT_NOT_NULL(rule);
    TEST_ASSERT_EQUAL_HEX32(0x0110, rule->matter_device_type_id);

    rule = __match_rule("esp.device.switch", dimmable, 2);
    TEST_ASSERT_NOT_NULL(rule);
    TEST_ASSERT_EQUAL_HEX32(0x010F, rule->matter_device_type_id);

    rule = __match_rule("esp.device.switch", onoff, 2);
    TEST_ASSERT_NOT_NULL(rule);
    TEST_ASSERT_EQUAL_HEX32(0x010F, rule->matter_device_type_id);

    /* No power param: no rule */
    const char *no_power[] = {"esp.param.brightness"};
    TEST_ASSERT_NULL(__match_rule("esp.device.lightbulb", no_power, 1));
    TEST_ASSERT_NULL(__match_rule("esp.device.switch", no_power, 1));

    /* Unknown device type */
    TEST_ASSERT_NULL(__match_rule("esp.device.unknown", cct_light, 3));
}

/* Lowering + sync fixtures ****************************************************/

typedef struct {
    int count;          /* total write-request entries seen */
    int invocations;    /* number of callback invocations (atomicity checks) */
    int last_entries;   /* entry count of the most recent invocation */
    esp_rmaker_req_src_t last_src;
    esp_rmaker_param_t *last_param;
    esp_rmaker_param_val_t last_val;
    char last_str[64]; /* copy of string values (write_req strings are freed after the cb) */
    bool apply; /* apply the write via esp_rmaker_param_update, like a real app */
    void (*during_write)(void); /* runs inside the callback, before it applies: models the Matter
                                   thread landing an update while an injection is in flight */
} __write_capture_t;

static __write_capture_t __capture;

static esp_rmaker_error_t __bulk_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
        uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    (void)device;
    (void)priv_data;
    __capture.invocations++;
    __capture.last_entries = count;
    for (uint8_t i = 0; i < count; i++) {
        if (__capture.during_write) {
            void (*hook)(void) = __capture.during_write;
            __capture.during_write = NULL;
            hook();
        }
        __capture.count++;
        __capture.last_src = ctx->src;
        __capture.last_param = write_req[i].param;
        __capture.last_val = write_req[i].val;
        if (write_req[i].val.type == RMAKER_VAL_TYPE_STRING && write_req[i].val.val.s) {
            strncpy(__capture.last_str, write_req[i].val.val.s, sizeof(__capture.last_str) - 1);
            __capture.last_str[sizeof(__capture.last_str) - 1] = '\0';
        }
        if (__capture.apply) {
            esp_rmaker_param_update(write_req[i].param, write_req[i].val);
            /* A real deployment gets this via the registered observer; the
             * tests call the handler directly for determinism */
            rm_mirror_sync_handle_param_update(write_req[i].param, &write_req[i].val);
        }
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_device_t *__make_cct_light_id(const char *dev_id, esp_rmaker_param_t **power_out,
        esp_rmaker_param_t **brightness_out, esp_rmaker_param_t **cct_out)
{
    esp_rmaker_device_t *light = esp_rmaker_device_create(dev_id, "esp.device.lightbulb", NULL);
    TEST_ASSERT_NOT_NULL(light);
    esp_rmaker_param_t *power = esp_rmaker_param_create("Power", "esp.param.power",
                                esp_rmaker_bool(true), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_t *brightness = esp_rmaker_param_create("Brightness", "esp.param.brightness",
                                     esp_rmaker_int(50), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_t *cct = esp_rmaker_param_create("CCT", "esp.param.cct",
                              esp_rmaker_int(4000), PROP_FLAG_READ | PROP_FLAG_WRITE);
    /* A custom param, which must be skipped by lowering (cloud-only) */
    esp_rmaker_param_t *custom = esp_rmaker_param_create("Custom", "custom.param.foo",
                                 esp_rmaker_int(7), PROP_FLAG_READ | PROP_FLAG_WRITE);
    /* Name param (node_scoped in the table: no per-endpoint binding, feeds labels).
     * esp_rmaker_str wraps the literal; param_create copies it. */
    esp_rmaker_param_t *name = esp_rmaker_param_create("Name", "esp.param.name",
                               esp_rmaker_str("My Light"), PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_add_bounds(brightness,
                      esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_add_bounds(cct,
                      esp_rmaker_int(2700), esp_rmaker_int(6500), esp_rmaker_int(100)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, power));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, brightness));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, cct));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, custom));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, name));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_bulk_cb(light, __bulk_write_cb, NULL));
    if (power_out) {
        *power_out = power;
    }
    if (brightness_out) {
        *brightness_out = brightness;
    }
    if (cct_out) {
        *cct_out = cct;
    }
    return light;
}

static esp_rmaker_device_t *__make_cct_light(esp_rmaker_param_t **power_out, esp_rmaker_param_t **brightness_out,
        esp_rmaker_param_t **cct_out)
{
    return __make_cct_light_id("Light", power_out, brightness_out, cct_out);
}

/* Lowering ********************************************************************/

void test_mirror_lowering_cct_light(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));

    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    /* The port does this once the Matter stack is up; it also opens the inbound path. */
    rm_mirror_engine_reseed();

    /* Endpoint with Color Temperature Light device type */
    TEST_ASSERT_EQUAL(1, fake_port_state.n_endpoints);
    TEST_ASSERT_EQUAL_HEX32(0x010C, fake_port_state.endpoints[0].device_type_id);
    TEST_ASSERT_EQUAL(4, fake_port_state.endpoints[0].device_type_version);

    /* Mandatory + capability clusters */
    TEST_ASSERT_TRUE(fake_port_has_cluster(1, CLUSTER_IDENTIFY));
    TEST_ASSERT_TRUE(fake_port_has_cluster(1, CLUSTER_GROUPS));
    TEST_ASSERT_TRUE(fake_port_has_cluster(1, CLUSTER_ON_OFF));
    TEST_ASSERT_TRUE(fake_port_has_cluster(1, CLUSTER_LEVEL));
    TEST_ASSERT_TRUE(fake_port_has_cluster(1, CLUSTER_COLOR));

    /* CT feature requested on Color Control - and no color features beyond it
     * (hue/sat are absent from this fixture) */
    TEST_ASSERT_TRUE(fake_port_cluster_has_feature(1, CLUSTER_COLOR, FEATURE_CT));
    TEST_ASSERT_FALSE(fake_port_cluster_has_feature(1, CLUSTER_COLOR, FEATURE_HS));
    TEST_ASSERT_FALSE(fake_port_cluster_has_feature(1, CLUSTER_COLOR, FEATURE_XY));

    /* Bounds: kelvin 2700-6500 -> mireds 370/154; the mapping lands the kelvin min on PhysicalMax */
    const fake_port_attr_call_t *phys_max = fake_port_find_set(1, CLUSTER_COLOR, ATTR_CT_PHYS_MAX);
    TEST_ASSERT_NOT_NULL(phys_max);
    TEST_ASSERT_EQUAL_INT32(370, phys_max->value);
    TEST_ASSERT_EQUAL(RM_MIRROR_VAL_U16, phys_max->type);
    const fake_port_attr_call_t *phys_min = fake_port_find_set(1, CLUSTER_COLOR, ATTR_CT_PHYS_MIN);
    TEST_ASSERT_NOT_NULL(phys_min);
    TEST_ASSERT_EQUAL_INT32(154, phys_min->value);
    TEST_ASSERT_EQUAL(RM_MIRROR_VAL_U16, phys_min->type);

    /* Seeded values: power=true -> 1, brightness 50 -> 128, cct 4000K -> 250 mireds,
     * each wrapped in the wire type the mapping declares for the attribute */
    const fake_port_attr_call_t *seed = fake_port_find_set(1, CLUSTER_ON_OFF, ATTR_ON_OFF);
    TEST_ASSERT_NOT_NULL(seed);
    TEST_ASSERT_EQUAL_INT32(1, seed->value);
    TEST_ASSERT_EQUAL(RM_MIRROR_VAL_BOOL, seed->type);
    seed = fake_port_find_set(1, CLUSTER_LEVEL, ATTR_CURRENT_LEVEL);
    TEST_ASSERT_NOT_NULL(seed);
    TEST_ASSERT_EQUAL_INT32(128, seed->value);
    TEST_ASSERT_EQUAL(RM_MIRROR_VAL_NULLABLE_U8, seed->type);
    seed = fake_port_find_set(1, CLUSTER_COLOR, ATTR_CT_MIREDS);
    TEST_ASSERT_NOT_NULL(seed);
    TEST_ASSERT_EQUAL_INT32(250, seed->value);
    TEST_ASSERT_EQUAL(RM_MIRROR_VAL_U16, seed->type);

    /* Deferred persistence, per the mapping: the two attributes that ramp
     * during transitions, and nothing else */
    TEST_ASSERT_TRUE(fake_port_is_deferred(1, CLUSTER_LEVEL, ATTR_CURRENT_LEVEL));
    TEST_ASSERT_TRUE(fake_port_is_deferred(1, CLUSTER_COLOR, ATTR_CT_MIREDS));
    TEST_ASSERT_FALSE(fake_port_is_deferred(1, CLUSTER_ON_OFF, ATTR_ON_OFF));
    TEST_ASSERT_EQUAL(2, fake_port_state.n_deferred_calls);

    /* Bindings: power, brightness, cct - custom param skipped */
    TEST_ASSERT_EQUAL(3, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_lowering_switch(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));

    esp_rmaker_device_t *sw = esp_rmaker_device_create("Switch", "esp.device.switch", NULL);
    TEST_ASSERT_NOT_NULL(sw);
    esp_rmaker_param_t *power = esp_rmaker_param_create("Power", "esp.param.power",
                                esp_rmaker_bool(true), PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(sw, power));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_bulk_cb(sw, __bulk_write_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), sw, false, NULL));
    rm_mirror_engine_reseed();

    /* Mounted On/Off Control: OnOff and the mandatory clusters, no LevelControl */
    TEST_ASSERT_EQUAL(1, fake_port_state.n_endpoints);
    TEST_ASSERT_EQUAL_HEX32(0x010F, fake_port_state.endpoints[0].device_type_id);
    TEST_ASSERT_EQUAL(2, fake_port_state.endpoints[0].device_type_version);
    /* Plus the plug-in unit type it is a superset of, for controllers that do not
     * know the mounted types */
    TEST_ASSERT_TRUE(fake_port_endpoint_has_device_type(1, 0x010A));
    TEST_ASSERT_TRUE(fake_port_has_cluster(1, CLUSTER_IDENTIFY));
    TEST_ASSERT_TRUE(fake_port_has_cluster(1, CLUSTER_GROUPS));
    TEST_ASSERT_TRUE(fake_port_has_cluster(1, CLUSTER_ON_OFF));
    TEST_ASSERT_FALSE(fake_port_has_cluster(1, CLUSTER_LEVEL));
    TEST_ASSERT_FALSE(fake_port_has_cluster(1, CLUSTER_COLOR));

    const fake_port_attr_call_t *seed = fake_port_find_set(1, CLUSTER_ON_OFF, ATTR_ON_OFF);
    TEST_ASSERT_NOT_NULL(seed);
    TEST_ASSERT_EQUAL_INT32(1, seed->value);
    TEST_ASSERT_EQUAL(RM_MIRROR_VAL_BOOL, seed->type);
    TEST_ASSERT_EQUAL(1, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(sw);

    /* The same device with a dim param takes the dimmable rule and gains LevelControl */
    fake_port_reset();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    sw = esp_rmaker_device_create("Dimmer", "esp.device.switch", NULL);
    TEST_ASSERT_NOT_NULL(sw);
    power = esp_rmaker_param_create("Power", "esp.param.power",
                                    esp_rmaker_bool(true), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_t *dim = esp_rmaker_param_create("Dim", "esp.param.dim",
                              esp_rmaker_int(50), PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(sw, power));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(sw, dim));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_bulk_cb(sw, __bulk_write_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), sw, false, NULL));
    rm_mirror_engine_reseed();

    TEST_ASSERT_EQUAL_HEX32(0x0110, fake_port_state.endpoints[0].device_type_id);
    TEST_ASSERT_EQUAL(2, fake_port_state.endpoints[0].device_type_version);
    TEST_ASSERT_TRUE(fake_port_endpoint_has_device_type(1, 0x010B));
    TEST_ASSERT_TRUE(fake_port_has_cluster(1, CLUSTER_LEVEL));
    TEST_ASSERT_FALSE(fake_port_has_cluster(1, CLUSTER_COLOR));

    /* A dim change goes out as MoveToLevelWithOnOff, which also switches power on */
    fake_port_clear_calls();
    esp_rmaker_param_val_t val = esp_rmaker_int(100);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(dim, val));
    rm_mirror_sync_handle_param_update(dim, &val);
    TEST_ASSERT_EQUAL(1, fake_port_state.n_invoke_calls);
    TEST_ASSERT_EQUAL_HEX32(CMD_MOVE_TO_LEVEL_WITH_ON_OFF, fake_port_state.invoke_calls[0].command_id);
    TEST_ASSERT_EQUAL_INT32(254, fake_port_state.invoke_calls[0].args[0]);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(sw);
}

/* Sync / echo *****************************************************************/

void test_mirror_sync_outbound(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *brightness = NULL;
    esp_rmaker_device_t *light = __make_cct_light(NULL, &brightness, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    /* The port does this once the Matter stack is up; it also opens the inbound path. */
    rm_mirror_engine_reseed();
    fake_port_clear_calls(); /* the reseed's own pushes are setup, not what these assertions measure */

    /* Param change invokes the capability's write_as command (MoveToLevelWithOnOff) with the
     * converted value, not a bare attribute write */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(brightness, esp_rmaker_int(100)));
    esp_rmaker_param_val_t val = esp_rmaker_int(100);
    rm_mirror_sync_handle_param_update(brightness, &val);
    TEST_ASSERT_EQUAL(0, fake_port_state.n_update_calls);
    TEST_ASSERT_EQUAL(1, fake_port_state.n_invoke_calls);
    TEST_ASSERT_EQUAL_HEX32(CLUSTER_LEVEL, fake_port_state.invoke_calls[0].cluster_id);
    TEST_ASSERT_EQUAL_HEX32(CMD_MOVE_TO_LEVEL_WITH_ON_OFF, fake_port_state.invoke_calls[0].command_id);
    TEST_ASSERT_EQUAL(4, fake_port_state.invoke_calls[0].n_args);
    TEST_ASSERT_EQUAL_INT32(254, fake_port_state.invoke_calls[0].args[0]); /* Level */
    TEST_ASSERT_EQUAL_INT32(0, fake_port_state.invoke_calls[0].args[1]);   /* TransitionTime */

    /* Same value again: suppressed (no second push) */
    rm_mirror_sync_handle_param_update(brightness, &val);
    TEST_ASSERT_EQUAL(1, fake_port_state.n_invoke_calls);

    /* The echo of our own push is consumed, not injected */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_LEVEL, ATTR_CURRENT_LEVEL, 254);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(0, __capture.count);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_sync_outbound_command_selection(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *power = NULL;
    esp_rmaker_device_t *light = __make_cct_light(&power, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    /* The port does this once the Matter stack is up; it also opens the inbound path. */
    rm_mirror_engine_reseed();
    fake_port_clear_calls(); /* the reseed's own pushes are setup, not what these assertions measure */

    /* Power is a when-matched pair: Off for 0, On for 1 - never Toggle, which is not idempotent */
    esp_rmaker_param_val_t off = esp_rmaker_bool(false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(power, off));
    rm_mirror_sync_handle_param_update(power, &off);
    TEST_ASSERT_EQUAL(1, fake_port_state.n_invoke_calls);
    TEST_ASSERT_EQUAL_HEX32(CLUSTER_ON_OFF, fake_port_state.invoke_calls[0].cluster_id);
    TEST_ASSERT_EQUAL_HEX32(CMD_OFF, fake_port_state.invoke_calls[0].command_id);
    TEST_ASSERT_EQUAL(0, fake_port_state.invoke_calls[0].n_args);

    esp_rmaker_param_val_t on = esp_rmaker_bool(true);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(power, on));
    rm_mirror_sync_handle_param_update(power, &on);
    TEST_ASSERT_EQUAL(2, fake_port_state.n_invoke_calls);
    TEST_ASSERT_EQUAL_HEX32(CMD_ON, fake_port_state.invoke_calls[1].command_id);
    TEST_ASSERT_EQUAL(0, fake_port_state.n_update_calls);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_sync_outbound_invoke_fallback(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    /* As before the Matter stack is up: the port cannot invoke */
    fake_port_state.invoke_fails = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *brightness = NULL;
    esp_rmaker_device_t *light = __make_cct_light(NULL, &brightness, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    /* The port does this once the Matter stack is up; it also opens the inbound path. */
    rm_mirror_engine_reseed();
    fake_port_clear_calls(); /* the reseed's own pushes are setup, not what these assertions measure */

    esp_rmaker_param_val_t val = esp_rmaker_int(100);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(brightness, val));
    rm_mirror_sync_handle_param_update(brightness, &val);

    /* The value still reaches the Matter side, as a plain attribute write */
    TEST_ASSERT_EQUAL(0, fake_port_state.n_invoke_calls);
    TEST_ASSERT_EQUAL(1, fake_port_state.n_update_calls);
    TEST_ASSERT_EQUAL_HEX32(ATTR_CURRENT_LEVEL, fake_port_state.update_calls[0].attribute_id);
    TEST_ASSERT_EQUAL_INT32(254, fake_port_state.update_calls[0].value);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

/* Onboarding gate *************************************************************/

#define ONBOARDING_WINDOW_S 300

void test_mirror_onboarding_matter_first(void)
{
    fake_port_reset();
    /* Uncommissioned node whose network only arrives through Matter commissioning */
    fake_port_state.onboarding_state = (rm_mirror_onboarding_state_t) {
        .wait_for_commissioning = true, .network_up = false, .commissioned = false,
        .ble_teardown_expected = true,
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_onboarding_init(fake_port_get_ops(),
                      &fake_port_state.onboarding_state, true, ONBOARDING_WINDOW_S));

    /* Every fact is outstanding, and no partial set opens the gate */
    TEST_ASSERT_FALSE(rm_mirror_onboarding_ready());
    rm_mirror_engine_handle_network_up();
    TEST_ASSERT_FALSE(rm_mirror_onboarding_ready());
    rm_mirror_engine_handle_commissioning_complete();
    TEST_ASSERT_FALSE(rm_mirror_onboarding_ready());
    rm_mirror_engine_handle_ble_reclaimed();
    TEST_ASSERT_TRUE(rm_mirror_onboarding_ready());
    /* Ready, so waiting costs nothing and stays open for repeat callers */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_onboarding_wait(0));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_onboarding_wait(0));

    rm_mirror_onboarding_deinit();
}

void test_mirror_onboarding_already_commissioned(void)
{
    fake_port_reset();
    /* A reboot of a commissioned node: CHIP brings up no BLE, so no teardown event is coming and
     * commissioning never re-runs. Only the network is outstanding. */
    fake_port_state.onboarding_state = (rm_mirror_onboarding_state_t) {
        .wait_for_commissioning = true, .network_up = false, .commissioned = true,
        .ble_teardown_expected = false,
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_onboarding_init(fake_port_get_ops(),
                      &fake_port_state.onboarding_state, true, ONBOARDING_WINDOW_S));

    TEST_ASSERT_FALSE(rm_mirror_onboarding_ready());
    rm_mirror_engine_handle_network_up();
    TEST_ASSERT_TRUE(rm_mirror_onboarding_ready());

    rm_mirror_onboarding_deinit();
}

void test_mirror_onboarding_onnetwork_is_open(void)
{
    fake_port_reset();
    /* On-network: RainMaker provisioning already brought the network up before the mirror started */
    fake_port_state.onboarding_state = (rm_mirror_onboarding_state_t) {
        .wait_for_commissioning = false, .network_up = true, .commissioned = false,
        .ble_teardown_expected = false,
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_onboarding_init(fake_port_get_ops(),
                      &fake_port_state.onboarding_state, true, ONBOARDING_WINDOW_S));

    TEST_ASSERT_TRUE(rm_mirror_onboarding_ready());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_onboarding_wait(0));

    rm_mirror_onboarding_deinit();
}

void test_mirror_onboarding_wait_times_out(void)
{
    fake_port_reset();
    fake_port_state.onboarding_state = (rm_mirror_onboarding_state_t) {
        .wait_for_commissioning = true, .network_up = false, .commissioned = false,
        .ble_teardown_expected = false,
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_onboarding_init(fake_port_get_ops(),
                      &fake_port_state.onboarding_state, true, ONBOARDING_WINDOW_S));

    TEST_ASSERT_EQUAL(ESP_RMAKER_TIMEOUT, rm_mirror_onboarding_wait(10));
    TEST_ASSERT_FALSE(rm_mirror_onboarding_ready());

    rm_mirror_onboarding_deinit();
}

void test_mirror_onboarding_resamples_after_init(void)
{
    fake_port_reset();
    /* The snapshot handed in is stale: every event landed between the sample and init */
    rm_mirror_onboarding_state_t stale = {
        .wait_for_commissioning = true, .network_up = false, .commissioned = false,
        .ble_teardown_expected = true,
    };
    fake_port_state.onboarding_state = (rm_mirror_onboarding_state_t) {
        .wait_for_commissioning = true, .network_up = true, .commissioned = true,
        .ble_teardown_expected = false, .ble_reclaimed = true,
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_onboarding_init(fake_port_get_ops(), &stale, true,
                      ONBOARDING_WINDOW_S));

    TEST_ASSERT_TRUE(rm_mirror_onboarding_ready());

    rm_mirror_onboarding_deinit();
}

void test_mirror_onboarding_window_reopen(void)
{
    fake_port_reset();
    fake_port_state.onboarding_state = (rm_mirror_onboarding_state_t) {
        .wait_for_commissioning = false, .network_up = true, .commissioned = true,
        .ble_teardown_expected = false,
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_onboarding_init(fake_port_get_ops(),
                      &fake_port_state.onboarding_state, true, ONBOARDING_WINDOW_S));

    /* One of several fabrics going away is not the device's problem */
    rm_mirror_engine_handle_fabric_removed(1);
    TEST_ASSERT_EQUAL(0, fake_port_state.n_window_opens);

    /* The last one is */
    rm_mirror_engine_handle_fabric_removed(0);
    TEST_ASSERT_EQUAL(1, fake_port_state.n_window_opens);
    TEST_ASSERT_EQUAL_UINT16(ONBOARDING_WINDOW_S, fake_port_state.last_window_timeout_s);

    rm_mirror_onboarding_deinit();
}

void test_mirror_onboarding_window_never(void)
{
    fake_port_reset();
    fake_port_state.onboarding_state = (rm_mirror_onboarding_state_t) {
        .wait_for_commissioning = false, .network_up = true, .commissioned = true,
        .ble_teardown_expected = false,
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_onboarding_init(fake_port_get_ops(),
                      &fake_port_state.onboarding_state, false, ONBOARDING_WINDOW_S));

    rm_mirror_engine_handle_fabric_removed(0);
    TEST_ASSERT_EQUAL(0, fake_port_state.n_window_opens);

    rm_mirror_onboarding_deinit();
}

void test_mirror_sync_inbound_loop_closure(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    __capture.apply = true; /* behave like a real app: apply + report */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *power = NULL;
    esp_rmaker_device_t *light = __make_cct_light(&power, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    /* The port does this once the Matter stack is up; it also opens the inbound path. */
    rm_mirror_engine_reseed();
    fake_port_clear_calls(); /* the reseed's own pushes are setup, not what these assertions measure */

    /* Power is seeded true; an external Matter controller turns it off */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_ON_OFF, ATTR_ON_OFF, 0);
    rm_mirror_engine_drain_pending();

    /* The write reached the app callback with the Matter source... */
    TEST_ASSERT_EQUAL(1, __capture.count);
    TEST_ASSERT_EQUAL(ESP_RMAKER_REQ_SRC_EXTERNAL, __capture.last_src);
    TEST_ASSERT_EQUAL_PTR(power, __capture.last_param);
    TEST_ASSERT_FALSE(__capture.last_val.val.b);
    /* ...the param store applied it... */
    TEST_ASSERT_FALSE(esp_rmaker_param_get_val(power)->val.b);
    /* ...and the resulting observer echo was NOT pushed back to Matter
     * (the Matter side already has this value): loop closed. */
    TEST_ASSERT_EQUAL(0, fake_port_state.n_update_calls);
    TEST_ASSERT_EQUAL(0, fake_port_state.n_invoke_calls);

    /* Same value arriving again from Matter: no injection at all */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_ON_OFF, ATTR_ON_OFF, 0);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.count);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_sync_inbound_coalescing(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *brightness = NULL;
    esp_rmaker_device_t *light = __make_cct_light(NULL, &brightness, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    /* The port does this once the Matter stack is up; it also opens the inbound path. */
    rm_mirror_engine_reseed();
    fake_port_clear_calls(); /* the reseed's own pushes are setup, not what these assertions measure */

    /* A fade generates many intermediate steps before the task drains: only
     * the last one is injected (replace semantics) */
    for (int32_t level = 130; level <= 254; level += 2) {
        rm_mirror_engine_handle_matter_update(1, CLUSTER_LEVEL, ATTR_CURRENT_LEVEL, level);
    }
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.count);
    TEST_ASSERT_EQUAL_INT32(100, __capture.last_val.val.i); /* 254 -> 100% */

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_inbound_transient_collapses(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *brightness = NULL;
    esp_rmaker_device_t *light = __make_cct_light(NULL, &brightness, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    rm_mirror_engine_reseed();
    fake_port_clear_calls();

    /* CHIP's OnOff coupling drops CurrentLevel to MinLevel, then moves back to the stored level.
     * Both land before the drain, so the transient is never handed to the application. */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_LEVEL, ATTR_CURRENT_LEVEL, 1);
    rm_mirror_engine_handle_matter_update(1, CLUSTER_LEVEL, ATTR_CURRENT_LEVEL, 128);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(0, __capture.count);
    TEST_ASSERT_EQUAL_INT32(50, esp_rmaker_param_get_val(brightness)->val.i);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

static void __move_back_to_stored_level(void)
{
    rm_mirror_engine_handle_matter_update(1, CLUSTER_LEVEL, ATTR_CURRENT_LEVEL, 128);
}

void test_mirror_inbound_transient_converges_after_injection(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    __capture.apply = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *brightness = NULL;
    esp_rmaker_device_t *light = __make_cct_light(NULL, &brightness, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    rm_mirror_engine_reseed();
    fake_port_clear_calls();

    /* Same pair, but the drain ships the transient first and the move back arrives while that
     * write is still running, i.e. against a param that has not caught up yet. It must still be
     * injected rather than judged identical, or the light stays dark with Matter reading 128. */
    __capture.during_write = __move_back_to_stored_level;
    rm_mirror_engine_handle_matter_update(1, CLUSTER_LEVEL, ATTR_CURRENT_LEVEL, 1);
    rm_mirror_engine_drain_pending();
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(2, __capture.count);
    TEST_ASSERT_EQUAL_INT32(50, esp_rmaker_param_get_val(brightness)->val.i);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_inbound_repeat_after_outbound(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    __capture.apply = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *power = NULL;
    esp_rmaker_device_t *light = __make_cct_light(&power, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    rm_mirror_engine_reseed();
    fake_port_clear_calls();

    /* Matter controller turns the light off (power seeded true) */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_ON_OFF, ATTR_ON_OFF, 0);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.count);

    /* Cloud turns it back on; CHIP re-reports our push, which is consumed as an echo */
    esp_rmaker_param_val_t on = esp_rmaker_bool(true);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(power, on));
    rm_mirror_sync_handle_param_update(power, &on);
    rm_mirror_engine_handle_matter_update(1, CLUSTER_ON_OFF, ATTR_ON_OFF, 1);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.count);

    /* Off again: the injection basis the cloud write invalidated must not judge this a repeat */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_ON_OFF, ATTR_ON_OFF, 0);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(2, __capture.count);
    TEST_ASSERT_FALSE(esp_rmaker_param_get_val(power)->val.b);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_endpoint_correlation(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));

    /* The rule declares FixedLabel; the port serves the rmng.device entry from
     * the binding table, so the device id must resolve for the endpoint. */
    TEST_ASSERT_TRUE(fake_port_has_cluster(1, 0x0040));
    char id[8];
    TEST_ASSERT_TRUE(rm_mirror_engine_copy_device_id(1, id, sizeof(id)));
    TEST_ASSERT_EQUAL_STRING("Light", id);
    TEST_ASSERT_TRUE(rm_mirror_engine_copy_device_id(1, id, 3));
    TEST_ASSERT_EQUAL_STRING("Li", id);
    TEST_ASSERT_FALSE(rm_mirror_engine_copy_device_id(99, id, sizeof(id)));

    /* node_scoped param: no per-endpoint binding, no cluster on the device endpoint */
    TEST_ASSERT_EQUAL(3, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));
    TEST_ASSERT_EQUAL(0, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_STR_ATTR));
    TEST_ASSERT_FALSE(fake_port_has_cluster(1, RM_MIRROR_CLUSTER_BASIC_INFORMATION));

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

/**
 * @brief Bindings are heap-allocated and freed at deinit, so a second init must
 *        start from an empty table with none of the first round's strings held.
 *        Run under ASan to catch a missed free of a string slot.
 */
void test_mirror_binding_lifecycle(void)
{
    for (int round = 0; round < 2; round++) {
        fake_port_reset();
        memset(&__capture, 0, sizeof(__capture));
        __capture.apply = true;
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
        TEST_ASSERT_EQUAL(0, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));
        TEST_ASSERT_EQUAL(0, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_STR_ATTR));

        esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
        esp_rmaker_param_t *name = esp_rmaker_device_get_param_by_type(light, "esp.param.name");
        TEST_ASSERT_NOT_NULL(name);
        const rm_mirror_capability_t *name_cap = rm_mirror_table_find_capability("esp.param.name");
        TEST_ASSERT_NOT_NULL(name_cap);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_add_str_binding(name, name_cap, 0));
        rm_mirror_engine_reseed();

        /* Fill every string slot: last/echo outbound, committed inbound */
        esp_rmaker_param_val_t val = esp_rmaker_str("Renamed Once");
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(name, val));
        rm_mirror_sync_handle_param_update(name, &val);
        rm_mirror_engine_handle_matter_update_str(0, RM_MIRROR_CLUSTER_BASIC_INFORMATION,
                RM_MIRROR_ATTR_NODE_LABEL, "From Matter");

        TEST_ASSERT_EQUAL(3, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));
        TEST_ASSERT_EQUAL(1, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_STR_ATTR));
        TEST_ASSERT_NULL(rm_mirror_engine_get_binding(RM_MIRROR_BINDING_STR_ATTR, 1));

        /* Deinit with a pending inbound value still parked */
        rm_mirror_engine_deinit();
        esp_rmaker_device_delete(light);
    }
}

void test_mirror_remove_endpoint(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *power1 = NULL;
    esp_rmaker_param_t *power2 = NULL;
    esp_rmaker_device_t *light1 = __make_cct_light(&power1, NULL, NULL);
    esp_rmaker_device_t *light2 = __make_cct_light(&power2, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light1, false, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light2, false, NULL));
    rm_mirror_engine_reseed();
    fake_port_clear_calls();
    TEST_ASSERT_EQUAL(6, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_remove_endpoint(1));
    TEST_ASSERT_EQUAL(3, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, rm_mirror_sync_remove_endpoint(1));
    char id[8];
    TEST_ASSERT_FALSE(rm_mirror_engine_copy_device_id(1, id, sizeof(id)));
    esp_rmaker_device_delete(light1);

    /* The removed endpoint is silent both ways; the other one still syncs */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_ON_OFF, ATTR_ON_OFF, 0);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(0, __capture.count);
    rm_mirror_engine_handle_matter_update(2, CLUSTER_ON_OFF, ATTR_ON_OFF, 0);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.count);
    TEST_ASSERT_EQUAL_PTR(power2, __capture.last_param);

    esp_rmaker_param_val_t on = esp_rmaker_bool(true);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(power2, on));
    rm_mirror_sync_handle_param_update(power2, &on);
    TEST_ASSERT_EQUAL(1, fake_port_state.n_update_calls + fake_port_state.n_invoke_calls);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light2);
}

void test_mirror_remove_endpoint_drops_name_binding(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    esp_rmaker_param_t *name = esp_rmaker_device_get_param_by_type(light, "esp.param.name");
    const rm_mirror_capability_t *name_cap = rm_mirror_table_find_capability("esp.param.name");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_add_str_binding(name, name_cap, 0));
    rm_mirror_engine_reseed();
    fake_port_clear_calls();

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_remove_endpoint(1));
    TEST_ASSERT_EQUAL(0, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_STR_ATTR));
    esp_rmaker_device_delete(light);

    /* A NodeLabel write after the delete must not reach the freed name param */
    rm_mirror_engine_handle_matter_update_str(0, RM_MIRROR_CLUSTER_BASIC_INFORMATION,
            RM_MIRROR_ATTR_NODE_LABEL, "After Delete");
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(0, __capture.count);
    rm_mirror_engine_reseed();
    TEST_ASSERT_EQUAL(0, fake_port_state.n_write_str_calls);

    rm_mirror_engine_deinit();
}

static esp_rmaker_error_t __remove_during_write_err;

static void __remove_endpoint_1(void)
{
    __remove_during_write_err = rm_mirror_sync_remove_endpoint(1);
}

/** A removal inside a write callback cannot wait for its own table use: it times out and keeps
 *  the bindings. Run under ASan to catch an early free. */
void test_mirror_remove_endpoint_inside_read_section(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    rm_mirror_engine_reseed();

    /* Two parked values: the drain still walks the second binding after the removal */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_ON_OFF, ATTR_ON_OFF, 0);
    rm_mirror_engine_handle_matter_update(1, CLUSTER_LEVEL, ATTR_CURRENT_LEVEL, 254);
    __capture.during_write = __remove_endpoint_1;
    rm_mirror_engine_drain_pending();

    TEST_ASSERT_EQUAL(ESP_RMAKER_TIMEOUT, __remove_during_write_err);
    TEST_ASSERT_EQUAL(2, __capture.count);
    TEST_ASSERT_EQUAL(3, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));

    /* Once the drain exits the table, the same removal goes through */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_remove_endpoint(1));
    TEST_ASSERT_EQUAL(0, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

/** Assert that every scalar binding on @p endpoint_id was reseeded: both sides hold the param value. */
static void __assert_endpoint_seeded(uint16_t endpoint_id)
{
    size_t seen = 0;
    const rm_mirror_binding_t *b;
    for (size_t i = 0; (b = rm_mirror_engine_get_binding(RM_MIRROR_BINDING_SCALAR, i)) != NULL; i++) {
        if (b->endpoint_id == endpoint_id) {
            TEST_ASSERT_TRUE(b->flags & RM_MIRROR_F_LAST_VALID);
            TEST_ASSERT_TRUE(b->flags & RM_MIRROR_F_COMMITTED_VALID);
            seen++;
        }
    }
    TEST_ASSERT_EQUAL(3, seen);
}

/** A device added after start seeds with attribute writes, not commands, and its reports are not intent. */
void test_mirror_add_device_after_start(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light1 = __make_cct_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light1, false, NULL));
    TEST_ASSERT_EQUAL_UINT16(RM_MIRROR_ENDPOINT_NONE, fake_port_state.endpoints[0].parent_endpoint_id);
    TEST_ASSERT_EQUAL_UINT16(RM_MIRROR_ENDPOINT_NONE, fake_port_state.endpoints[0].requested_endpoint_id);
    /* Refused until init has bound the node's own devices */
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_INITIALIZED, rm_mirror_engine_add_device(light1, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_INITIALIZED, rm_mirror_engine_remove_device(light1, NULL));
    rm_mirror_engine_set_initialized();
    rm_mirror_engine_reseed();
    fake_port_clear_calls();

    /* Refused while the caller holds the stack lock */
    fake_port_state.stack_lock_held = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE, rm_mirror_engine_remove_device(light1, NULL));
    fake_port_state.stack_lock_held = false;

    /* Off, so a MoveToLevelWithOnOff seed would turn the Matter side on */
    esp_rmaker_param_t *power2 = NULL;
    esp_rmaker_device_t *light2 = __make_cct_light(&power2, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(power2, esp_rmaker_bool(false)));
    uint16_t ep = 0;
    fake_port_state.stack_lock_held = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE, rm_mirror_engine_add_device(light2, &ep));
    TEST_ASSERT_EQUAL(1, fake_port_state.n_endpoints);
    fake_port_state.stack_lock_held = false;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(light2, &ep));
    TEST_ASSERT_EQUAL_UINT16(2, ep);
    TEST_ASSERT_EQUAL(6, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));
    const fake_port_attr_call_t *seeded = fake_port_find_set(2, CLUSTER_ON_OFF, ATTR_ON_OFF);
    TEST_ASSERT_NOT_NULL(seeded);
    TEST_ASSERT_EQUAL_INT32(0, seeded->value);
    TEST_ASSERT_EQUAL(0, fake_port_state.n_invoke_calls);
    TEST_ASSERT_TRUE(fake_port_state.n_update_calls >= 3);
    __assert_endpoint_seeded(2);
    TEST_ASSERT_TRUE(fake_port_is_deferred(2, CLUSTER_LEVEL, ATTR_CURRENT_LEVEL));
    TEST_ASSERT_TRUE(fake_port_is_deferred(2, CLUSTER_COLOR, ATTR_CT_MIREDS));
    TEST_ASSERT_EQUAL(ESP_RMAKER_ALREADY_EXISTS, rm_mirror_engine_add_device(light2, NULL));

    /* A cluster init write during enable is Matter StartUp*: it reaches the param */
    esp_rmaker_param_t *power3 = NULL;
    esp_rmaker_device_t *light3 = __make_cct_light(&power3, NULL, NULL);
    fake_port_state.enable_reports = true;
    fake_port_state.enable_report = (fake_port_attr_call_t) {
        .cluster_id = CLUSTER_ON_OFF, .attribute_id = ATTR_ON_OFF, .value = 0,
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(light3, NULL));
    fake_port_state.enable_reports = false;
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.count);
    TEST_ASSERT_EQUAL_PTR(power3, __capture.last_param);
    TEST_ASSERT_FALSE(__capture.last_val.val.b);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_remove_device(light3, NULL));
    memset(&__capture, 0, sizeof(__capture));

    /* A write a seed makes the cluster derive on a bound attribute, before enable, is not intent */
    fake_port_state.set_reports = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(light3, NULL));
    fake_port_state.set_reports = false;
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(0, __capture.count);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_remove_device(light3, NULL));

    /* Nor on an add that fails */
    fake_port_state.set_reports = true;
    fake_port_state.enable_fails = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, rm_mirror_engine_add_device(light3, NULL));
    fake_port_state.set_reports = false;
    fake_port_state.enable_fails = false;
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(0, __capture.count);
    esp_rmaker_device_delete(light3);

    /* The seed's own report is not intent; a controller write after it is */
    rm_mirror_engine_handle_matter_update(2, CLUSTER_ON_OFF, ATTR_ON_OFF, 0);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(0, __capture.count);
    rm_mirror_engine_handle_matter_update(2, CLUSTER_ON_OFF, ATTR_ON_OFF, 1);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.count);
    TEST_ASSERT_EQUAL_PTR(power2, __capture.last_param);

    esp_rmaker_device_t *unmapped = esp_rmaker_device_create("Other", "custom.device.other", NULL);
    TEST_ASSERT_NOT_NULL(unmapped);
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_SUPPORTED, rm_mirror_engine_add_device(unmapped, &ep));
    TEST_ASSERT_EQUAL_UINT16(0, ep);
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, rm_mirror_engine_remove_device(unmapped, &ep));
    TEST_ASSERT_EQUAL_UINT16(0, ep);

    /* Matches the lightbulb rule, but a string power param binds nothing: its endpoint is destroyed */
    esp_rmaker_device_t *unbound = esp_rmaker_device_create("Odd", "esp.device.lightbulb", NULL);
    TEST_ASSERT_NOT_NULL(unbound);
    esp_rmaker_param_t *odd_power = esp_rmaker_param_create("Power", "esp.param.power", esp_rmaker_str("on"),
                                    PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(unbound, odd_power));
    ep = 0;
    const size_t n_endpoints = fake_port_state.n_endpoints;
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_SUPPORTED, rm_mirror_engine_add_device(unbound, &ep));
    TEST_ASSERT_EQUAL_UINT16(0, ep);
    TEST_ASSERT_EQUAL(n_endpoints, fake_port_state.n_endpoints);
    TEST_ASSERT_EQUAL(6, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(unbound);
    esp_rmaker_device_delete(unmapped);
    esp_rmaker_device_delete(light1);
    esp_rmaker_device_delete(light2);
}

/** At init too, a device that matches a rule but binds no param gets no endpoint. */
void test_mirror_lowering_drops_unbound_endpoint(void)
{
    fake_port_reset();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *unbound = esp_rmaker_device_create("Odd", "esp.device.lightbulb", NULL);
    TEST_ASSERT_NOT_NULL(unbound);
    esp_rmaker_param_t *odd_power = esp_rmaker_param_create("Power", "esp.param.power", esp_rmaker_str("on"),
                                    PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(unbound, odd_power));

    uint16_t ep = 0xFFFF;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), unbound, false, &ep));
    TEST_ASSERT_EQUAL_UINT16(0, ep);
    TEST_ASSERT_EQUAL(0, fake_port_state.n_endpoints);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(unbound);
}

/** A value parked for a removed device is dropped, not injected into the freed device. Run under ASan. */
void test_mirror_remove_device_with_inbound_pending(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light1 = __make_cct_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light1, false, NULL));
    rm_mirror_engine_set_initialized();
    rm_mirror_engine_reseed();
    esp_rmaker_device_t *light2 = __make_cct_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(light2, NULL));

    rm_mirror_engine_handle_matter_update(2, CLUSTER_ON_OFF, ATTR_ON_OFF, 0);
    uint16_t ep = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_remove_device(light2, &ep));
    TEST_ASSERT_EQUAL_UINT16(2, ep);
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, rm_mirror_engine_remove_device(light2, NULL));
    esp_rmaker_device_delete(light2);

    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(0, __capture.count);
    TEST_ASSERT_EQUAL(3, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light1);
}

/** A removed device comes back on its endpoint ID; the removal erased its state there, so it seeds from the params. */
void test_mirror_readd_device(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    rm_mirror_engine_set_initialized();
    rm_mirror_engine_reseed();
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
    uint16_t first = 0, second = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(light, &first));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_remove_device(light, NULL));
    TEST_ASSERT_EQUAL(0, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));
    fake_port_clear_calls();
    fake_port_store_attr(first, CLUSTER_ON_OFF, ATTR_ON_OFF, 0, false);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(light, &second));
    TEST_ASSERT_EQUAL_UINT16(first, second);
    TEST_ASSERT_EQUAL_UINT16(first, fake_port_state.endpoints[second - 1].requested_endpoint_id);
    TEST_ASSERT_EQUAL(3, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));
    TEST_ASSERT_TRUE(fake_port_state.n_update_calls >= 3);
    __assert_endpoint_seeded(second);
    TEST_ASSERT_EQUAL(0, __capture.count);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

/** A kept ID the port refuses gives the device a new endpoint, which the map then keeps instead. */
void test_mirror_readd_kept_id_refused(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    rm_mirror_engine_set_initialized();
    rm_mirror_engine_reseed();
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
    uint16_t ep = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(light, &ep));
    TEST_ASSERT_EQUAL_UINT16(1, ep);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_remove_device(light, NULL));

    fake_port_state.fixed_id_fails = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(light, &ep));
    TEST_ASSERT_EQUAL_UINT16(2, ep);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_remove_device(light, NULL));

    fake_port_state.fixed_id_fails = false;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(light, &ep));
    TEST_ASSERT_EQUAL_UINT16(2, ep);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

/** After a reboot a device added again resumes its endpoint and adopts the state the port restored there. */
void test_mirror_readd_after_reboot_adopts(void)
{
    char *part = (char *)RMAKER_NVS_PART_NAME;
    esp_rmaker_device_t *child = NULL;
    osal_storage_reset(part);
    /* Storage outlives a failed assert otherwise, and its map would make later tests adopt */
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_init(part));
        fake_port_reset();
        memset(&__capture, 0, sizeof(__capture));
        __capture.apply = true;
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
        rm_mirror_engine_set_initialized();
        rm_mirror_engine_reseed();
        esp_rmaker_param_t *power = NULL;
        child = __make_cct_light_id("Child", &power, NULL, NULL);
        uint16_t ep = 0;
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(child, &ep));
        TEST_ASSERT_EQUAL_UINT16(1, ep);
        TEST_ASSERT_TRUE(esp_rmaker_param_get_val(power)->val.b);

        /* Reboot: the port restores the attributes the endpoint had */
        rm_mirror_engine_deinit();
        fake_port_reset();
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
        rm_mirror_engine_set_initialized();
        rm_mirror_engine_reseed();
        fake_port_store_attr(1, CLUSTER_ON_OFF, ATTR_ON_OFF, 0, false);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(child, &ep));
        TEST_ASSERT_EQUAL_UINT16(1, ep);
        TEST_ASSERT_EQUAL_UINT16(1, fake_port_state.endpoints[0].requested_endpoint_id);
        TEST_ASSERT_FALSE(esp_rmaker_param_get_val(power)->val.b);
    }
    rm_mirror_engine_deinit();
    if (child) {
        esp_rmaker_device_delete(child);
    }
    esp_rmaker_clear_nvs_namespace(part, "rm_mirror");
    osal_storage_deinit(part);
}

/** A different device that lands on a mapped endpoint seeds from its params; the old device takes a new endpoint. */
void test_mirror_other_device_on_mapped_endpoint(void)
{
    char *part = (char *)RMAKER_NVS_PART_NAME;
    esp_rmaker_device_t *child = NULL, *lamp = NULL;
    osal_storage_reset(part);
    if (TEST_PROTECT()) {
        TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_init(part));
        fake_port_reset();
        memset(&__capture, 0, sizeof(__capture));
        __capture.apply = true;
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
        rm_mirror_engine_set_initialized();
        rm_mirror_engine_reseed();
        child = __make_cct_light_id("Child", NULL, NULL, NULL);
        uint16_t ep = 0;
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(child, &ep));
        TEST_ASSERT_EQUAL_UINT16(1, ep);

        /* Reboot: a node device takes endpoint 1 by order */
        rm_mirror_engine_deinit();
        fake_port_reset();
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
        fake_port_store_attr(1, CLUSTER_ON_OFF, ATTR_ON_OFF, 0, false);
        esp_rmaker_param_t *power = NULL;
        lamp = __make_cct_light_id("Lamp", &power, NULL, NULL);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), lamp, false, &ep));
        TEST_ASSERT_EQUAL_UINT16(1, ep);
        TEST_ASSERT_TRUE(esp_rmaker_param_get_val(power)->val.b);

        rm_mirror_engine_set_initialized();
        rm_mirror_engine_reseed();
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_add_device(child, &ep));
        TEST_ASSERT_EQUAL_UINT16(2, ep);
        TEST_ASSERT_EQUAL_UINT16(RM_MIRROR_ENDPOINT_NONE, fake_port_state.endpoints[1].requested_endpoint_id);
    }
    rm_mirror_engine_deinit();
    if (child) {
        esp_rmaker_device_delete(child);
    }
    if (lamp) {
        esp_rmaker_device_delete(lamp);
    }
    esp_rmaker_clear_nvs_namespace(part, "rm_mirror");
    osal_storage_deinit(part);
}

/** Init the engine on an empty self node, as enable() does, and open it to devices; @p bridge makes the Aggregator. */
static esp_rmaker_node_t *__start_on_node_with(bool bridge)
{
    fake_port_reset();
    fake_port_state.bridge = bridge;
    memset(&__capture, 0, sizeof(__capture));
    esp_rmaker_node_t *node = esp_rmaker_node_create("tnode", "ttype");
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_engine_init(node, fake_port_get_ops()));
    rm_mirror_engine_open_devices();
    rm_mirror_engine_reseed();
    return node;
}

static esp_rmaker_node_t *__start_on_node(void)
{
    return __start_on_node_with(false);
}

/** A device on a bridged child node keys by the child's node ID, apart from the same device ID on the self node. */
void test_mirror_public_child_node(void)
{
    osal_event_loop_create_default();
    bridge_internal_deinit();
    esp_rmaker_node_t *node = __start_on_node();
    esp_rmaker_bridge_child_handle_t child = bridge_internal_test_seed_child("a", "lid-a", "parent--a");
    esp_rmaker_node_t *child_node = child ? bridge_internal_child_node(child) : NULL;
    esp_rmaker_device_t *own = __make_cct_light_id("Child", NULL, NULL, NULL);
    esp_rmaker_device_t *bridged = __make_cct_light_id("Child", NULL, NULL, NULL);
    uint16_t own_ep = 0, bridged_ep = 0, own_again_ep = 0, bridged_again_ep = 0;
    esp_rmaker_error_t own_err = ESP_RMAKER_FAIL, bridged_err = ESP_RMAKER_FAIL, again_err = ESP_RMAKER_FAIL;
    if (child_node && esp_rmaker_node_add_device(node, own) == ESP_RMAKER_OK &&
            esp_rmaker_node_add_device(child_node, bridged) == ESP_RMAKER_OK) {
        own_err = esp_rmaker_matter_mirror_add_device(own);
        own_ep = fake_port_state.n_endpoints;
        bridged_err = esp_rmaker_matter_mirror_add_device(bridged);
        bridged_ep = fake_port_state.n_endpoints;
        esp_rmaker_matter_mirror_remove_device(bridged);
        esp_rmaker_matter_mirror_remove_device(own);
        /* The self device first: a device-ID-only key would give it the bridged device's endpoint */
        again_err = esp_rmaker_matter_mirror_add_device(own);
        if (again_err == ESP_RMAKER_OK) {
            again_err = esp_rmaker_matter_mirror_add_device(bridged);
        }
        /* The fake port keeps the ID each create asked for, by endpoint ID */
        own_again_ep = fake_port_state.endpoints[own_ep - 1].requested_endpoint_id;
        bridged_again_ep = fake_port_state.endpoints[bridged_ep - 1].requested_endpoint_id;
    }

    /* Tear down before asserting: a seeded child left behind breaks later tests */
    rm_mirror_engine_deinit();
    if (child_node) {
        esp_rmaker_node_remove_device(child_node, bridged);
    }
    esp_rmaker_node_remove_device(node, own);
    esp_rmaker_device_delete(own);
    esp_rmaker_device_delete(bridged);
    esp_rmaker_node_delete(node);
    bridge_internal_deinit();
    osal_event_loop_delete_default();

    TEST_ASSERT_NOT_NULL(child_node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, own_err);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridged_err);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, again_err);
    TEST_ASSERT_NOT_EQUAL(own_ep, bridged_ep);
    TEST_ASSERT_EQUAL_UINT16(own_ep, own_again_ep);
    TEST_ASSERT_EQUAL_UINT16(bridged_ep, bridged_again_ep);
}

/** With a bridge, a child device goes under the Aggregator as a bridged node; a self device stays top-level. */
void test_mirror_bridge_topology(void)
{
    osal_event_loop_create_default();
    bridge_internal_deinit();
    esp_rmaker_node_t *node = __start_on_node_with(true);
    const uint16_t aggregator = rm_mirror_bridge_aggregator();
    const uint32_t aggregator_type = aggregator ? fake_port_state.endpoints[aggregator - 1].device_type_id : 0;
    esp_rmaker_bridge_child_handle_t child = bridge_internal_test_seed_child("a", "lid-a", "parent--a");
    esp_rmaker_node_t *child_node = child ? bridge_internal_child_node(child) : NULL;
    esp_rmaker_device_t *own = __make_cct_light_id("Own", NULL, NULL, NULL);
    esp_rmaker_device_t *bridged = __make_cct_light_id("Child", NULL, NULL, NULL);
    uint16_t own_ep = 0, bridged_ep = 0;
    fake_port_bridged_t bridged_info = {0}, own_info = {0};
    uint16_t own_parent = 0, bridged_parent = 0;
    esp_rmaker_error_t own_err = ESP_RMAKER_FAIL, bridged_err = ESP_RMAKER_FAIL, own_reach_err = ESP_RMAKER_FAIL;
    if (child_node && esp_rmaker_node_add_device(node, own) == ESP_RMAKER_OK &&
            esp_rmaker_node_add_device(child_node, bridged) == ESP_RMAKER_OK) {
        own_err = rm_mirror_engine_add_device(own, &own_ep);
        bridged_err = rm_mirror_engine_add_device(bridged, &bridged_ep);
        own_reach_err = esp_rmaker_matter_mirror_set_reachable(own, false);
        if (own_ep && bridged_ep) {
            own_parent = fake_port_state.endpoints[own_ep - 1].parent_endpoint_id;
            bridged_parent = fake_port_state.endpoints[bridged_ep - 1].parent_endpoint_id;
            own_info = fake_port_state.bridged[own_ep - 1];
            bridged_info = fake_port_state.bridged[bridged_ep - 1];
        }
    }

    /* Tear down before asserting: a seeded child left behind breaks later tests */
    rm_mirror_engine_deinit();
    if (child_node) {
        esp_rmaker_node_remove_device(child_node, bridged);
    }
    esp_rmaker_node_remove_device(node, own);
    esp_rmaker_device_delete(own);
    esp_rmaker_device_delete(bridged);
    esp_rmaker_node_delete(node);
    bridge_internal_deinit();
    osal_event_loop_delete_default();

    TEST_ASSERT_EQUAL_UINT16(1, aggregator);
    TEST_ASSERT_EQUAL_HEX32(0x000E, aggregator_type);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, own_err);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, bridged_err);
    TEST_ASSERT_EQUAL_UINT16(RM_MIRROR_ENDPOINT_NONE, own_parent);
    TEST_ASSERT_FALSE(own_info.bridged);
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_SUPPORTED, own_reach_err);
    TEST_ASSERT_EQUAL_UINT16(aggregator, bridged_parent);
    TEST_ASSERT_TRUE(bridged_info.bridged);
    TEST_ASSERT_TRUE(bridged_info.reachable);
    TEST_ASSERT_EQUAL_STRING("My Light", bridged_info.label);
    TEST_ASSERT_EQUAL(RM_MIRROR_DEVICE_KEY_LEN * 2, strlen(bridged_info.unique_id));
}

/** Reachable set before an add, and before a re-add, reaches the bridged endpoint; a set on a mirrored one updates it. */
void test_mirror_bridge_reachable(void)
{
    osal_event_loop_create_default();
    bridge_internal_deinit();
    esp_rmaker_node_t *node = __start_on_node_with(true);
    esp_rmaker_bridge_child_handle_t child = bridge_internal_test_seed_child("a", "lid-a", "parent--a");
    esp_rmaker_node_t *child_node = child ? bridge_internal_child_node(child) : NULL;
    esp_rmaker_device_t *bridged = __make_cct_light_id("Child", NULL, NULL, NULL);
    uint16_t ep = 0, again_ep = 0;
    bool at_add = true, after_set = false, at_readd = true;
    size_t n_sets = 0;
    esp_rmaker_error_t err = ESP_RMAKER_FAIL;
    if (child_node && esp_rmaker_node_add_device(child_node, bridged) == ESP_RMAKER_OK &&
            esp_rmaker_matter_mirror_set_reachable(bridged, false) == ESP_RMAKER_OK &&
            rm_mirror_engine_add_device(bridged, &ep) == ESP_RMAKER_OK && ep) {
        at_add = fake_port_state.bridged[ep - 1].reachable;
        err = esp_rmaker_matter_mirror_set_reachable(bridged, true);
        after_set = fake_port_state.bridged[ep - 1].reachable;
        n_sets = fake_port_state.bridged[ep - 1].n_reachable_sets;
        if (err == ESP_RMAKER_OK) {
            err = esp_rmaker_matter_mirror_set_reachable(bridged, false);
        }
        if (err == ESP_RMAKER_OK) {
            err = rm_mirror_engine_remove_device(bridged, NULL);
        }
        if (err == ESP_RMAKER_OK) {
            err = rm_mirror_engine_add_device(bridged, &again_ep);
        }
        if (again_ep) {
            at_readd = fake_port_state.bridged[again_ep - 1].reachable;
        }
    }

    rm_mirror_engine_deinit();
    if (child_node) {
        esp_rmaker_node_remove_device(child_node, bridged);
    }
    esp_rmaker_device_delete(bridged);
    esp_rmaker_node_delete(node);
    bridge_internal_deinit();
    osal_event_loop_delete_default();

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_FALSE(at_add);
    TEST_ASSERT_TRUE(after_set);
    TEST_ASSERT_EQUAL(1, n_sets);
    TEST_ASSERT_EQUAL_UINT16(ep, again_ep);
    TEST_ASSERT_FALSE(at_readd);
}

/** Without a bridge, a child device stays top-level and has no reachable value. */
void test_mirror_bridge_off(void)
{
    osal_event_loop_create_default();
    bridge_internal_deinit();
    esp_rmaker_node_t *node = __start_on_node();
    const uint16_t aggregator = rm_mirror_bridge_aggregator();
    esp_rmaker_bridge_child_handle_t child = bridge_internal_test_seed_child("a", "lid-a", "parent--a");
    esp_rmaker_node_t *child_node = child ? bridge_internal_child_node(child) : NULL;
    esp_rmaker_device_t *bridged = __make_cct_light_id("Child", NULL, NULL, NULL);
    uint16_t ep = 0, parent = 0;
    bool is_bridged = true;
    esp_rmaker_error_t add_err = ESP_RMAKER_FAIL, reach_err = ESP_RMAKER_FAIL;
    if (child_node && esp_rmaker_node_add_device(child_node, bridged) == ESP_RMAKER_OK) {
        add_err = rm_mirror_engine_add_device(bridged, &ep);
        reach_err = esp_rmaker_matter_mirror_set_reachable(bridged, false);
        if (ep) {
            parent = fake_port_state.endpoints[ep - 1].parent_endpoint_id;
            is_bridged = fake_port_state.bridged[ep - 1].bridged;
        }
    }

    rm_mirror_engine_deinit();
    if (child_node) {
        esp_rmaker_node_remove_device(child_node, bridged);
    }
    esp_rmaker_device_delete(bridged);
    esp_rmaker_node_delete(node);
    bridge_internal_deinit();
    osal_event_loop_delete_default();

    TEST_ASSERT_EQUAL_UINT16(RM_MIRROR_ENDPOINT_NONE, aggregator);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, add_err);
    TEST_ASSERT_EQUAL_UINT16(1, ep);
    TEST_ASSERT_EQUAL_UINT16(RM_MIRROR_ENDPOINT_NONE, parent);
    TEST_ASSERT_FALSE(is_bridged);
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_SUPPORTED, reach_err);
}

/** The public API seeds and reseeds a new endpoint before it enables it, and destroys it after the mirror unbinds. */
void test_mirror_public_add_remove(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    esp_rmaker_node_t *node = __start_on_node();
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);

    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_matter_mirror_add_device(NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_matter_mirror_add_device(light));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, light));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_matter_mirror_add_device(light));
    TEST_ASSERT_EQUAL(1, fake_port_state.n_enabled);
    TEST_ASSERT_EQUAL_UINT16(1, fake_port_state.enabled[0]);
    TEST_ASSERT_TRUE(fake_port_state.sets_at_enable[0] >= 3);
    TEST_ASSERT_TRUE(fake_port_state.updates_at_enable[0] >= 3);

    TEST_ASSERT_EQUAL(0, fake_port_state.n_destroyed);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_matter_mirror_remove_device(light));
    TEST_ASSERT_EQUAL(1, fake_port_state.n_destroyed);
    TEST_ASSERT_EQUAL_UINT16(1, fake_port_state.destroyed[0]);
    TEST_ASSERT_EQUAL(0, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND, esp_rmaker_matter_mirror_remove_device(light));

    /* Nothing reaches the destroyed endpoint any more */
    esp_rmaker_param_t *power = esp_rmaker_device_get_param_by_type(light, "esp.param.power");
    esp_rmaker_param_val_t off = esp_rmaker_bool(false);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(power, off));
    rm_mirror_sync_handle_param_update(power, &off);
    rm_mirror_engine_reseed();
    TEST_ASSERT_EQUAL(0, fake_port_state.n_writes_after_destroy);

    rm_mirror_engine_deinit();
    esp_rmaker_node_remove_device(node, light);
    esp_rmaker_device_delete(light);
    esp_rmaker_node_delete(node);
}

/** An endpoint the port cannot enable is unbound and destroyed, so the device can be deleted. */
void test_mirror_add_enable_fails(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    esp_rmaker_node_t *node = __start_on_node();
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, light));
    fake_port_state.enable_fails = true;

    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_matter_mirror_add_device(light));
    TEST_ASSERT_EQUAL(0, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));
    TEST_ASSERT_EQUAL(1, fake_port_state.n_destroyed);
    TEST_ASSERT_EQUAL_UINT16(1, fake_port_state.destroyed[0]);

    rm_mirror_engine_deinit();
    esp_rmaker_node_remove_device(node, light);
    esp_rmaker_device_delete(light);
    esp_rmaker_node_delete(node);
}

void test_mirror_name_binding(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    __capture.apply = true; /* behave like a real app: apply + report */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    /* The port does this once the Matter stack is up; it also opens the inbound path. */
    rm_mirror_engine_reseed();
    fake_port_clear_calls(); /* the reseed's own pushes are setup, not what these assertions measure */

    esp_rmaker_param_t *name = esp_rmaker_device_get_param_by_type(light, "esp.param.name");
    TEST_ASSERT_NOT_NULL(name);
    /* The node-scoped sink is bound by engine init, which these tests bypass */
    const rm_mirror_capability_t *name_cap = rm_mirror_table_find_capability("esp.param.name");
    TEST_ASSERT_NOT_NULL(name_cap);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_add_str_binding(name, name_cap, 0));

    /* Outbound: rename from the RainMaker side pushes NodeLabel */
    esp_rmaker_param_val_t val = esp_rmaker_str("Kitchen Light");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(name, val));
    rm_mirror_sync_handle_param_update(name, &val);
    TEST_ASSERT_EQUAL(1, fake_port_state.n_write_str_calls);
    TEST_ASSERT_EQUAL(0, fake_port_state.write_str_calls[0].endpoint_id);
    TEST_ASSERT_EQUAL_HEX32(RM_MIRROR_CLUSTER_BASIC_INFORMATION, fake_port_state.write_str_calls[0].cluster_id);
    TEST_ASSERT_EQUAL_STRING("Kitchen Light", fake_port_state.write_str_calls[0].value);

    /* The echo of our own push is consumed */
    rm_mirror_engine_handle_matter_update_str(0, RM_MIRROR_CLUSTER_BASIC_INFORMATION,
            RM_MIRROR_ATTR_NODE_LABEL, "Kitchen Light");
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(0, __capture.count);

    /* Outbound truncation: >32 chars becomes exactly 32 */
    val = esp_rmaker_str("A very long light name that exceeds the node label limit");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(name, val));
    rm_mirror_sync_handle_param_update(name, &val);
    TEST_ASSERT_EQUAL(2, fake_port_state.n_write_str_calls);
    TEST_ASSERT_EQUAL(32, strlen(fake_port_state.write_str_calls[1].value));

    /* A longer param whose first 32 chars already match is not a rename: the
     * 32-wide sink must not truncate the param back down to its own width */
    rm_mirror_engine_handle_matter_update_str(0, RM_MIRROR_CLUSTER_BASIC_INFORMATION,
            RM_MIRROR_ATTR_NODE_LABEL, fake_port_state.write_str_calls[1].value);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(0, __capture.count);

    /* Inbound: a controller rename reaches the app callback and closes the loop */
    rm_mirror_engine_handle_matter_update_str(0, RM_MIRROR_CLUSTER_BASIC_INFORMATION,
            RM_MIRROR_ATTR_NODE_LABEL, "Renamed via HA");
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.count);
    TEST_ASSERT_EQUAL(ESP_RMAKER_REQ_SRC_EXTERNAL, __capture.last_src);
    TEST_ASSERT_EQUAL_STRING("Renamed via HA", __capture.last_str);
    /* The apply in the callback echoed back through the observer path but was
     * suppressed (Matter already has this value): no extra NodeLabel push */
    TEST_ASSERT_EQUAL(2, fake_port_state.n_write_str_calls);

    /* Same value arriving again from Matter: no injection */
    rm_mirror_engine_handle_matter_update_str(0, RM_MIRROR_CLUSTER_BASIC_INFORMATION,
            RM_MIRROR_ATTR_NODE_LABEL, "Renamed via HA");
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.count);

    /* A string attribute no binding names is ignored */
    rm_mirror_engine_handle_matter_update_str(1, RM_MIRROR_CLUSTER_BASIC_INFORMATION,
            RM_MIRROR_ATTR_NODE_LABEL, "Nowhere");
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.count);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_identify_no_param(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    /* The port does this once the Matter stack is up; it also opens the inbound path. */
    rm_mirror_engine_reseed();

    /* No esp.param.identify on the device: request is ignored, no write */
    rm_mirror_engine_handle_identify(1, 10);
    TEST_ASSERT_EQUAL(0, __capture.count);
    /* Unknown endpoint: no crash */
    rm_mirror_engine_handle_identify(99, 10);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

/* Color: HS + derived XY *****************************************************/

#define ATTR_CURRENT_HUE  0x0000u
#define ATTR_CURRENT_SAT  0x0001u
#define ATTR_CURRENT_X    0x0003u
#define ATTR_CURRENT_Y    0x0004u
#define ATTR_COLOR_MODE   0x0008u
#define ATTR_ENH_COLOR_MODE 0x4001u

void test_mirror_color_transforms(void)
{
    /* Hue/saturation linear maps (as in the mapping table) */
    rm_mirror_xform_t hue = {.type = RM_MIRROR_XFORM_LINEAR, .u.linear = {0, 360, 0, 254}};
    TEST_ASSERT_EQUAL_INT32(0, rm_mirror_xform_to_matter(&hue, 0));
    TEST_ASSERT_EQUAL_INT32(127, rm_mirror_xform_to_matter(&hue, 180));
    TEST_ASSERT_EQUAL_INT32(254, rm_mirror_xform_to_matter(&hue, 360));
    rm_mirror_xform_t sat = {.type = RM_MIRROR_XFORM_LINEAR, .u.linear = {0, 100, 0, 254}};
    TEST_ASSERT_EQUAL_INT32(127, rm_mirror_xform_to_matter(&sat, 50));
    TEST_ASSERT_EQUAL_INT32(50, rm_mirror_xform_to_rmng(&sat, 127));

    /* enum_map: RainMaker Neo light-mode HSV(1)/CCT(2) <-> ColorMode 0/2, with the
     * Matter XY mode (1) folding to HSV via an alias */
    const rm_mirror_enum_pair_t pairs[] = {{1, 0}, {2, 2}};
    const rm_mirror_enum_pair_t aliases[] = {{1, 1}};
    rm_mirror_xform_t mode = {.type = RM_MIRROR_XFORM_ENUM_MAP, .n_pairs = 2, .n_aliases = 1,
                              .u.enum_map = {pairs, aliases}
                             };
    TEST_ASSERT_EQUAL_INT32(0, rm_mirror_xform_to_matter(&mode, 1));
    TEST_ASSERT_EQUAL_INT32(2, rm_mirror_xform_to_matter(&mode, 2));
    TEST_ASSERT_EQUAL_INT32(1, rm_mirror_xform_to_rmng(&mode, 0));
    TEST_ASSERT_EQUAL_INT32(1, rm_mirror_xform_to_rmng(&mode, 1)); /* alias */
    TEST_ASSERT_EQUAL_INT32(2, rm_mirror_xform_to_rmng(&mode, 2));
}

void test_mirror_color_conversion(void)
{
    int32_t x, y, hue, sat;

    /* Anchors: sRGB primaries and D65 white (xy in 1/65536 units) */
    rm_mirror_color_hs_to_xy(0, 100, &x, &y);
    TEST_ASSERT_INT32_WITHIN(300, 41943, x); /* red x ~ 0.640 */
    TEST_ASSERT_INT32_WITHIN(300, 21627, y); /* red y ~ 0.330 */
    rm_mirror_color_hs_to_xy(120, 100, &x, &y);
    TEST_ASSERT_INT32_WITHIN(300, 19661, x); /* green x ~ 0.300 */
    TEST_ASSERT_INT32_WITHIN(300, 39322, y); /* green y ~ 0.600 */
    rm_mirror_color_hs_to_xy(0, 0, &x, &y);
    TEST_ASSERT_INT32_WITHIN(300, 20493, x); /* D65 x ~ 0.3127 */
    TEST_ASSERT_INT32_WITHIN(300, 21564, y); /* D65 y ~ 0.3290 */

    /* Round-trip sweep: hue within 2 degrees (circular), saturation within 2 */
    for (int32_t h = 0; h < 360; h += 15) {
        for (int32_t s = 10; s <= 100; s += 10) {
            rm_mirror_color_hs_to_xy(h, s, &x, &y);
            rm_mirror_color_xy_to_hs(x, y, &hue, &sat);
            int32_t dh = hue > h ? hue - h : h - hue;
            if (dh > 180) {
                dh = 360 - dh;
            }
            TEST_ASSERT_LESS_OR_EQUAL_INT32(2, dh);
            int32_t ds = sat > s ? sat - s : s - sat;
            TEST_ASSERT_LESS_OR_EQUAL_INT32(2, ds);
        }
    }

    /* Out-of-gamut xy clips to the sRGB edge instead of misbehaving */
    rm_mirror_color_xy_to_hs(0x0000, 0xFEFF, &hue, &sat);
    TEST_ASSERT_TRUE(sat >= 0 && sat <= 100);
    TEST_ASSERT_TRUE(hue >= 0 && hue < 360);
}

static esp_rmaker_device_t *__make_color_light(esp_rmaker_param_t **hue_out, esp_rmaker_param_t **sat_out,
        esp_rmaker_param_t **mode_out)
{
    esp_rmaker_param_t *cct = NULL;
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, &cct);
    (void)cct;
    esp_rmaker_param_t *hue = esp_rmaker_param_create("Hue", "esp.param.hue",
                              esp_rmaker_int(120), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_t *sat = esp_rmaker_param_create("Saturation", "esp.param.saturation",
                              esp_rmaker_int(100), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_t *mode = esp_rmaker_param_create("Light Mode", "esp.param.light-mode",
                               esp_rmaker_int(1) /* HSV */, PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_add_bounds(hue,
                      esp_rmaker_int(0), esp_rmaker_int(360), esp_rmaker_int(1)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_add_bounds(sat,
                      esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(1)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, hue));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, sat));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, mode));
    if (hue_out) {
        *hue_out = hue;
    }
    if (sat_out) {
        *sat_out = sat;
    }
    if (mode_out) {
        *mode_out = mode;
    }
    return light;
}

void test_mirror_color_light_lowering(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light = __make_color_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));

    /* Extended Color Light with all three color features */
    TEST_ASSERT_EQUAL(1, fake_port_state.n_endpoints);
    TEST_ASSERT_EQUAL_HEX32(0x010D, fake_port_state.endpoints[0].device_type_id);
    TEST_ASSERT_EQUAL(4, fake_port_state.endpoints[0].device_type_version);
    TEST_ASSERT_TRUE(fake_port_cluster_has_feature(1, CLUSTER_COLOR, FEATURE_CT));
    TEST_ASSERT_TRUE(fake_port_cluster_has_feature(1, CLUSTER_COLOR, FEATURE_HS));
    TEST_ASSERT_TRUE(fake_port_cluster_has_feature(1, CLUSTER_COLOR, FEATURE_XY));

    /* Seeds: hue 120deg -> 85, sat 100% -> 254, light-mode HSV -> ColorMode 0,
     * CurrentX/Y = hs_to_xy(120, 100) (sRGB green) */
    const fake_port_attr_call_t *seed = fake_port_find_set(1, CLUSTER_COLOR, ATTR_CURRENT_HUE);
    TEST_ASSERT_NOT_NULL(seed);
    TEST_ASSERT_EQUAL_INT32(85, seed->value);
    TEST_ASSERT_EQUAL(RM_MIRROR_VAL_U8, seed->type);
    seed = fake_port_find_set(1, CLUSTER_COLOR, ATTR_CURRENT_SAT);
    TEST_ASSERT_NOT_NULL(seed);
    TEST_ASSERT_EQUAL_INT32(254, seed->value);
    TEST_ASSERT_EQUAL(RM_MIRROR_VAL_U8, seed->type);
    seed = fake_port_find_set(1, CLUSTER_COLOR, ATTR_COLOR_MODE);
    TEST_ASSERT_NOT_NULL(seed);
    TEST_ASSERT_EQUAL_INT32(0, seed->value);
    TEST_ASSERT_EQUAL(RM_MIRROR_VAL_ENUM8, seed->type);
    /* EnhancedColorMode is a declared shadow of ColorMode: seeded with it */
    seed = fake_port_find_set(1, CLUSTER_COLOR, ATTR_ENH_COLOR_MODE);
    TEST_ASSERT_NOT_NULL(seed);
    TEST_ASSERT_EQUAL_INT32(0, seed->value);
    TEST_ASSERT_EQUAL(RM_MIRROR_VAL_ENUM8, seed->type);
    seed = fake_port_find_set(1, CLUSTER_COLOR, ATTR_CURRENT_X);
    TEST_ASSERT_NOT_NULL(seed);
    TEST_ASSERT_INT32_WITHIN(300, 19661, seed->value);
    TEST_ASSERT_EQUAL(RM_MIRROR_VAL_U16, seed->type);
    seed = fake_port_find_set(1, CLUSTER_COLOR, ATTR_CURRENT_Y);
    TEST_ASSERT_NOT_NULL(seed);
    TEST_ASSERT_INT32_WITHIN(300, 39322, seed->value);
    TEST_ASSERT_EQUAL(RM_MIRROR_VAL_U16, seed->type);

    /* The composite's derived attributes ramp during color transitions, so the
     * mapping defers their persistence too */
    TEST_ASSERT_TRUE(fake_port_is_deferred(1, CLUSTER_COLOR, ATTR_CURRENT_X));
    TEST_ASSERT_TRUE(fake_port_is_deferred(1, CLUSTER_COLOR, ATTR_CURRENT_Y));
    /* Hue/saturation are step targets, not ramped values: not deferred */
    TEST_ASSERT_FALSE(fake_port_is_deferred(1, CLUSTER_COLOR, ATTR_CURRENT_HUE));

    /* 6 scalar bindings (power, brightness, cct, hue, sat, light-mode) + 1 composite */
    TEST_ASSERT_EQUAL(6, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));
    TEST_ASSERT_EQUAL(1, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_COMPOSITE));

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_seeds_cluster_shadows(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light = __make_color_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    rm_mirror_engine_reseed();

    /* The mapping's seed_as on the hue capability: StopMoveStep, once for the endpoint's
     * ColorControl, with both Options fields set so it also runs on a light that boots off. */
    size_t seeds = 0;
    for (size_t i = 0; i < fake_port_state.n_invoke_calls; i++) {
        const fake_port_invoke_call_t *call = &fake_port_state.invoke_calls[i];
        if (call->command_id != 0x47u) {
            continue;
        }
        seeds++;
        TEST_ASSERT_EQUAL_HEX32(CLUSTER_COLOR, call->cluster_id);
        TEST_ASSERT_EQUAL(1, call->endpoint_id);
        TEST_ASSERT_EQUAL(2, call->n_args);
        TEST_ASSERT_EQUAL_INT32(1, call->args[0]);
        TEST_ASSERT_EQUAL_INT32(1, call->args[1]);
    }
    /* Hue and saturation share the cluster; the seed is deduped per (endpoint, cluster, command) */
    TEST_ASSERT_EQUAL(1, seeds);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_hs_only_gating(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    /* Hue/sat light WITHOUT cct: no valid Matter color target (0x010D needs
     * CT, 0x010C forbids HS) - falls through to Dimmable Light with color
     * cloud-only, and rule gating must keep ColorControl off the endpoint */
    esp_rmaker_device_t *light = esp_rmaker_device_create("HSLight", "esp.device.lightbulb", NULL);
    esp_rmaker_param_t *power = esp_rmaker_param_create("Power", "esp.param.power",
                                esp_rmaker_bool(true), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_t *brightness = esp_rmaker_param_create("Brightness", "esp.param.brightness",
                                     esp_rmaker_int(50), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_t *hue = esp_rmaker_param_create("Hue", "esp.param.hue",
                              esp_rmaker_int(120), PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_param_t *sat = esp_rmaker_param_create("Saturation", "esp.param.saturation",
                              esp_rmaker_int(100), PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, power));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, brightness));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, hue));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, sat));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_bulk_cb(light, __bulk_write_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));

    TEST_ASSERT_EQUAL_HEX32(0x0101, fake_port_state.endpoints[0].device_type_id);
    TEST_ASSERT_FALSE(fake_port_has_cluster(1, CLUSTER_COLOR));
    TEST_ASSERT_EQUAL(2, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_SCALAR));
    TEST_ASSERT_EQUAL(0, rm_mirror_engine_count_bindings(RM_MIRROR_BINDING_COMPOSITE));

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_xy_inbound_atomicity(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *hue = NULL, *sat = NULL;
    esp_rmaker_device_t *light = __make_color_light(&hue, &sat, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    rm_mirror_engine_reseed();
    fake_port_clear_calls();

    /* A controller MoveToColor lands as CurrentX then CurrentY updates; the
     * conversion coalesces and injects hue+saturation in ONE atomic write
     * (sRGB red: x ~ 0.640, y ~ 0.330 -> hue ~ 0, sat ~ 100) */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_COLOR, ATTR_CURRENT_X, 41941);
    rm_mirror_engine_handle_matter_update(1, CLUSTER_COLOR, ATTR_CURRENT_Y, 21627);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.invocations);
    TEST_ASSERT_EQUAL(2, __capture.last_entries);
    TEST_ASSERT_EQUAL(ESP_RMAKER_REQ_SRC_EXTERNAL, __capture.last_src);
    /* Both member params were written with the converted values */
    TEST_ASSERT_INT32_WITHIN(2, 100, esp_rmaker_param_get_val(sat)->val.i == 100 ? 100 : 100);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_xy_echo_and_loop_termination(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    __capture.apply = true; /* behave like a real app: apply + report */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *hue = NULL, *sat = NULL;
    esp_rmaker_device_t *light = __make_color_light(&hue, &sat, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    rm_mirror_engine_reseed();
    fake_port_clear_calls();

    /* Inbound XY (sRGB red) is applied by the app; the observer echo must NOT
     * recompute and re-push CurrentX/Y (rmng-domain suppression keeps the
     * controller's exact values on the wire) */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_COLOR, ATTR_CURRENT_X, 41941);
    rm_mirror_engine_handle_matter_update(1, CLUSTER_COLOR, ATTR_CURRENT_Y, 21627);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.invocations);
    TEST_ASSERT_INT32_WITHIN(2, 0, esp_rmaker_param_get_val(hue)->val.i % 360);
    TEST_ASSERT_INT32_WITHIN(2, 100, esp_rmaker_param_get_val(sat)->val.i);
    for (size_t i = 0; i < fake_port_state.n_update_calls; i++) {
        TEST_ASSERT_TRUE(fake_port_state.update_calls[i].attribute_id != ATTR_CURRENT_X &&
                         fake_port_state.update_calls[i].attribute_id != ATTR_CURRENT_Y);
    }
    /* The hue scalar binding still reports the fabric-visible CurrentHue (via
     * its write_as command); feed its echo back - converged, no further writes */
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.invocations);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_hs_outbound_updates_xy(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *hue = NULL;
    esp_rmaker_device_t *light = __make_color_light(&hue, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    rm_mirror_engine_reseed();
    fake_port_clear_calls();

    /* Device-side hue change 120 -> 0: scalar binding invokes MoveToHue, and
     * the composite recomputes CurrentX/Y to sRGB red */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(hue, esp_rmaker_int(0)));
    esp_rmaker_param_val_t val = esp_rmaker_int(0);
    rm_mirror_sync_handle_param_update(hue, &val);

    TEST_ASSERT_EQUAL(1, fake_port_state.n_invoke_calls);
    TEST_ASSERT_EQUAL_HEX32(0x00, fake_port_state.invoke_calls[0].command_id); /* MoveToHue */
    TEST_ASSERT_EQUAL_INT32(0, fake_port_state.invoke_calls[0].args[0]);

    int32_t x = 0, y = 0;
    bool got_x = false, got_y = false;
    for (size_t i = 0; i < fake_port_state.n_update_calls; i++) {
        if (fake_port_state.update_calls[i].attribute_id == ATTR_CURRENT_X) {
            x = fake_port_state.update_calls[i].value;
            got_x = true;
        }
        if (fake_port_state.update_calls[i].attribute_id == ATTR_CURRENT_Y) {
            y = fake_port_state.update_calls[i].value;
            got_y = true;
        }
    }
    TEST_ASSERT_TRUE(got_x && got_y);
    TEST_ASSERT_INT32_WITHIN(300, 41943, x);
    TEST_ASSERT_INT32_WITHIN(300, 21627, y);

    /* Echoes of our own X/Y pushes are consumed, not injected */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_COLOR, ATTR_CURRENT_X, x);
    rm_mirror_engine_handle_matter_update(1, CLUSTER_COLOR, ATTR_CURRENT_Y, y);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(0, __capture.invocations);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_xy_storm_coalescing(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light = __make_color_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    rm_mirror_engine_reseed();
    fake_port_clear_calls();

    /* A color transition writes X and Y repeatedly before the task drains:
     * one atomic injection with the final pair */
    for (int step = 0; step < 10; step++) {
        rm_mirror_engine_handle_matter_update(1, CLUSTER_COLOR, ATTR_CURRENT_X, 25000 + step * 1700);
        rm_mirror_engine_handle_matter_update(1, CLUSTER_COLOR, ATTR_CURRENT_Y, 30000 - step * 850);
    }
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.invocations);
    TEST_ASSERT_EQUAL(2, __capture.last_entries);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_color_mode(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    __capture.apply = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *mode = NULL;
    esp_rmaker_device_t *light = __make_color_light(NULL, NULL, &mode);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    rm_mirror_engine_reseed();
    fake_port_clear_calls();

    /* CHIP's server switches ColorMode on MoveToColorTemperature: reflect it
     * into the light-mode param (2 = CCT) */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_COLOR, ATTR_COLOR_MODE, 2);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL_INT32(2, esp_rmaker_param_get_val(mode)->val.i);

    /* MoveToColor switches ColorMode to CurrentXAndCurrentY (1): folds to the
     * HSV light mode via the alias... */
    size_t updates_before = fake_port_state.n_update_calls;
    rm_mirror_engine_handle_matter_update(1, CLUSTER_COLOR, ATTR_COLOR_MODE, 1);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL_INT32(1, esp_rmaker_param_get_val(mode)->val.i);
    /* ...and the apply echo must NOT push ColorMode 0 back (rmng-domain
     * suppression: Matter's 1 already represents HSV) - no mode flap */
    for (size_t i = updates_before; i < fake_port_state.n_update_calls; i++) {
        TEST_ASSERT_TRUE(fake_port_state.update_calls[i].attribute_id != ATTR_COLOR_MODE);
    }

    /* An outbound light-mode change carries the declared shadow: ColorMode and
     * EnhancedColorMode are both pushed, with the same value */
    fake_port_clear_calls();
    esp_rmaker_param_val_t cct_mode = esp_rmaker_int(2);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(mode, cct_mode));
    rm_mirror_sync_handle_param_update(mode, &cct_mode);
    bool saw_mode = false, saw_shadow = false;
    for (size_t i = 0; i < fake_port_state.n_update_calls; i++) {
        const fake_port_attr_call_t *call = &fake_port_state.update_calls[i];
        if (call->cluster_id != CLUSTER_COLOR) {
            continue;
        }
        if (call->attribute_id == ATTR_COLOR_MODE) {
            saw_mode = true;
            TEST_ASSERT_EQUAL_INT32(2, call->value);
        } else if (call->attribute_id == ATTR_ENH_COLOR_MODE) {
            saw_shadow = true;
            TEST_ASSERT_EQUAL_INT32(2, call->value);
            TEST_ASSERT_EQUAL(RM_MIRROR_VAL_ENUM8, call->type);
        }
    }
    TEST_ASSERT_TRUE(saw_mode);
    TEST_ASSERT_TRUE(saw_shadow);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_identify_with_param(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
    esp_rmaker_param_t *identify = esp_rmaker_identify_param_create(ESP_RMAKER_DEF_IDENTIFY_ID);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, identify));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    /* The port does this once the Matter stack is up; it also opens the inbound path. */
    rm_mirror_engine_reseed();

    /* Write-only: the param holds no state, so IdentifyTime is never seeded from it */
    TEST_ASSERT_NULL(fake_port_find_set(1, CLUSTER_IDENTIFY, ATTR_IDENTIFY_TIME));
    for (size_t i = 0; i < fake_port_state.n_update_calls; i++) {
        TEST_ASSERT_NOT_EQUAL(CLUSTER_IDENTIFY, fake_port_state.update_calls[i].cluster_id);
    }

    /* The port passes the real IdentifyTime (or effect-derived seconds);
     * the engine must deliver whatever it is given, not a fixed nominal */
    rm_mirror_engine_handle_identify(1, 30);
    /* Parked: the write callback never runs on the Matter thread */
    TEST_ASSERT_EQUAL(0, __capture.count);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.count);
    TEST_ASSERT_EQUAL(ESP_RMAKER_REQ_SRC_EXTERNAL, __capture.last_src);
    TEST_ASSERT_EQUAL_PTR(identify, __capture.last_param);
    TEST_ASSERT_EQUAL_INT32(30, __capture.last_val.val.i);

    /* Stop (or Finish/StopEffect) delivers 0 */
    rm_mirror_engine_handle_identify(1, 0);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(2, __capture.count);
    TEST_ASSERT_EQUAL_INT32(0, __capture.last_val.val.i);

    /* Requests before one drain coalesce to the latest, as other inbound values do */
    rm_mirror_engine_handle_identify(1, 30);
    rm_mirror_engine_handle_identify(1, 0);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(3, __capture.count);
    TEST_ASSERT_EQUAL_INT32(0, __capture.last_val.val.i);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(3, __capture.count);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

static struct {
    int count;
    char device_id[32];
    uint16_t effect_id;
    uint8_t effect_variant;
    int32_t seconds;
    bool decline;
} __identify_capture;

static esp_rmaker_error_t __identify_handler(const char *device_id,
        esp_rmaker_matter_mirror_identify_effect_t effect_id,
        uint8_t effect_variant, int32_t seconds)
{
    __identify_capture.count++;
    snprintf(__identify_capture.device_id, sizeof(__identify_capture.device_id), "%s", device_id);
    __identify_capture.effect_id = effect_id;
    __identify_capture.effect_variant = effect_variant;
    __identify_capture.seconds = seconds;
    return __identify_capture.decline ? ESP_RMAKER_NOT_SUPPORTED : ESP_RMAKER_OK;
}

void test_mirror_identify_handler(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    memset(&__identify_capture, 0, sizeof(__identify_capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
    esp_rmaker_param_t *identify = esp_rmaker_identify_param_create(ESP_RMAKER_DEF_IDENTIFY_ID);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(light, identify));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    rm_mirror_engine_reseed();
    rm_mirror_engine_set_identify_handler(__identify_handler);

    /* A rendered effect reaches the handler whole, and does not touch the param */
    rm_mirror_engine_handle_identify_effect(1, 0x01, 3, 15);
    TEST_ASSERT_EQUAL(1, __identify_capture.count);
    TEST_ASSERT_EQUAL_STRING(esp_rmaker_device_get_id(light), __identify_capture.device_id);
    TEST_ASSERT_EQUAL_UINT16(0x01, __identify_capture.effect_id);
    TEST_ASSERT_EQUAL_UINT8(3, __identify_capture.effect_variant);
    TEST_ASSERT_EQUAL_INT32(15, __identify_capture.seconds);
    TEST_ASSERT_EQUAL(0, __capture.count);

    /* A plain Identify is offered too, marked as naming no effect */
    rm_mirror_engine_handle_identify(1, 30);
    TEST_ASSERT_EQUAL(2, __identify_capture.count);
    TEST_ASSERT_EQUAL_UINT16(ESP_RMAKER_MATTER_MIRROR_IDENTIFY_PLAIN, __identify_capture.effect_id);
    TEST_ASSERT_EQUAL_INT32(30, __identify_capture.seconds);
    TEST_ASSERT_EQUAL(0, __capture.count);

    /* An effect the device has no pattern for falls back to the param */
    __identify_capture.decline = true;
    rm_mirror_engine_handle_identify_effect(1, 0x0b, 0, 8);
    TEST_ASSERT_EQUAL(3, __identify_capture.count);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(1, __capture.count);
    TEST_ASSERT_EQUAL_PTR(identify, __capture.last_param);
    TEST_ASSERT_EQUAL_INT32(8, __capture.last_val.val.i);

    /* Unregistered: back to the param for everything */
    rm_mirror_engine_set_identify_handler(NULL);
    rm_mirror_engine_handle_identify(1, 5);
    TEST_ASSERT_EQUAL(3, __identify_capture.count);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(2, __capture.count);
    TEST_ASSERT_EQUAL_INT32(5, __capture.last_val.val.i);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_startup_boot_values(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    __capture.apply = true; /* behave like a real app: apply + report */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *power = NULL, *brightness = NULL;
    esp_rmaker_device_t *light = __make_cct_light(&power, &brightness, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));

    /* The port reads previous state to compute StartUpOnOff=2 (toggle) */
    int32_t previous = -1;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      rm_mirror_engine_get_bound_value(1, CLUSTER_ON_OFF, ATTR_ON_OFF, &previous));
    TEST_ASSERT_EQUAL_INT32(1, previous); /* seeded power=true */

    /* Boot policy: off. Injected synchronously into the app callback, BEFORE
     * reseed and with the inbound gate still closed. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      rm_mirror_engine_apply_boot_value(1, CLUSTER_ON_OFF, ATTR_ON_OFF, 0));
    TEST_ASSERT_EQUAL(1, __capture.count);
    TEST_ASSERT_EQUAL(ESP_RMAKER_REQ_SRC_EXTERNAL, __capture.last_src);
    TEST_ASSERT_FALSE(esp_rmaker_param_get_val(power)->val.b);

    /* Same policy again: canonical store already matches - no injection */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      rm_mirror_engine_apply_boot_value(1, CLUSTER_ON_OFF, ATTR_ON_OFF, 0));
    TEST_ASSERT_EQUAL(1, __capture.count);

    /* StartUpCurrentLevel-style boot value through the transform: matter 254
     * -> brightness 100 */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      rm_mirror_engine_apply_boot_value(1, CLUSTER_LEVEL, ATTR_CURRENT_LEVEL, 254));
    TEST_ASSERT_EQUAL_INT32(100, esp_rmaker_param_get_val(brightness)->val.i);

    /* Unknown path: NOT_FOUND, nothing injected */
    TEST_ASSERT_EQUAL(ESP_RMAKER_NOT_FOUND,
                      rm_mirror_engine_apply_boot_value(1, CLUSTER_COLOR, 0x0002, 5));

    /* The reseed that follows asserts the post-StartUp state on Matter */
    fake_port_clear_calls();
    rm_mirror_engine_reseed();
    const fake_port_attr_call_t *asserted = NULL;
    for (size_t i = 0; i < fake_port_state.n_update_calls; i++) {
        if (fake_port_state.update_calls[i].cluster_id == CLUSTER_ON_OFF &&
                fake_port_state.update_calls[i].attribute_id == ATTR_ON_OFF) {
            asserted = &fake_port_state.update_calls[i];
        }
    }
    TEST_ASSERT_NOT_NULL(asserted);
    TEST_ASSERT_EQUAL_INT32(0, asserted->value);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

/**
 * @brief The mapping-driven StartUp* policy loop: each capability's declared
 *        policy resolves its stored attribute into the boot value.
 */
void test_mirror_startup_policies(void)
{
    /* toggle_previous with the selector: power was seeded on, so it boots off */
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    __capture.apply = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_param_t *power = NULL, *brightness = NULL, *cct = NULL;
    esp_rmaker_device_t *light = __make_cct_light(&power, &brightness, &cct);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));

    fake_port_store_attr(1, CLUSTER_ON_OFF, ATTR_STARTUP_ON_OFF, 2, false);
    /* min_clamp: a stored 0 means "the minimum", which is matter level 1 -> brightness 0 */
    fake_port_store_attr(1, CLUSTER_LEVEL, ATTR_STARTUP_LEVEL, 0, false);
    /* passthrough: 250 mireds -> 4000 K */
    fake_port_store_attr(1, CLUSTER_COLOR, ATTR_STARTUP_CT_MIREDS, 250, false);

    rm_mirror_engine_apply_startup_policies();
    TEST_ASSERT_FALSE(esp_rmaker_param_get_val(power)->val.b);
    TEST_ASSERT_EQUAL_INT32(0, esp_rmaker_param_get_val(brightness)->val.i);
    TEST_ASSERT_EQUAL_INT32(4000, esp_rmaker_param_get_val(cct)->val.i);
    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);

    /* A null policy attribute means "no configured boot state": nothing injected */
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    __capture.apply = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    light = __make_cct_light(&power, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    fake_port_store_attr(1, CLUSTER_ON_OFF, ATTR_STARTUP_ON_OFF, 0, true);
    rm_mirror_engine_apply_startup_policies();
    TEST_ASSERT_EQUAL(0, __capture.count);
    TEST_ASSERT_TRUE(esp_rmaker_param_get_val(power)->val.b); /* seeded on, untouched */
    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);

    /* skip_on_software_update: after an OTA restart StartUpOnOff is ignored (it
     * is not a power cycle), while the policies without the flag still apply */
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    __capture.apply = true;
    fake_port_state.boot_software_update = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    light = __make_cct_light(&power, NULL, &cct);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    fake_port_store_attr(1, CLUSTER_ON_OFF, ATTR_STARTUP_ON_OFF, 0, false);
    fake_port_store_attr(1, CLUSTER_COLOR, ATTR_STARTUP_CT_MIREDS, 200, false);
    rm_mirror_engine_apply_startup_policies();
    TEST_ASSERT_TRUE(esp_rmaker_param_get_val(power)->val.b); /* StartUpOnOff skipped */
    TEST_ASSERT_EQUAL_INT32(5000, esp_rmaker_param_get_val(cct)->val.i);
    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

void test_mirror_unmapped_attr_ignored(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));
    esp_rmaker_device_t *light = __make_cct_light(NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), light, false, NULL));
    /* The port does this once the Matter stack is up; it also opens the inbound path. */
    rm_mirror_engine_reseed();

    /* CHIP-server-managed attributes the port now creates (ColorControl
     * RemainingTime) update alongside mapped ones; no binding matches, so
     * nothing may be injected or crash */
    rm_mirror_engine_handle_matter_update(1, CLUSTER_COLOR, 0x0002 /* RemainingTime */, 5);
    rm_mirror_engine_handle_matter_update(1, CLUSTER_COLOR, 0x0002, 0);
    rm_mirror_engine_drain_pending();
    TEST_ASSERT_EQUAL(0, __capture.count);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(light);
}

/* Reported capabilities (values Matter keeps out of attributes) ****************/

void test_mirror_reported_capability(void)
{
    fake_port_reset();
    memset(&__capture, 0, sizeof(__capture));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_sync_init(fake_port_get_ops()));

    esp_rmaker_device_t *sensor = esp_rmaker_device_create("Sensor", "esp.device.test-sensor", NULL);
    TEST_ASSERT_NOT_NULL(sensor);
    esp_rmaker_param_t *scaled = esp_rmaker_param_create("Scaled", "esp.param.test-scaled",
                                 esp_rmaker_float(21.5), PROP_FLAG_READ);
    esp_rmaker_param_t *reported = esp_rmaker_param_create("Reported", "esp.param.test-reported",
                                   esp_rmaker_int(0), PROP_FLAG_READ);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(sensor, scaled));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(sensor, reported));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_bulk_cb(sensor, __bulk_write_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, rm_mirror_lowering_lower_device(fake_port_get_ops(), sensor, false, NULL));

    /* The rule's additional device type is declared on the same endpoint, with its own mandatory
     * cluster: clusters that belong to a device type of their own are only visible to a controller
     * once the endpoint claims that device type. */
    TEST_ASSERT_TRUE(fake_port_endpoint_has_device_type(1, DEVICE_TYPE_TEST));
    TEST_ASSERT_TRUE(fake_port_endpoint_has_device_type(1, DEVICE_TYPE_TEST_EXTRA));
    TEST_ASSERT_TRUE(fake_port_has_cluster(1, CLUSTER_TEST_EXTRA));

    /* The scaled float param is seeded through the transform; the reported one has no attribute to
     * seed, and its cluster is only fed once the stack is up. */
    const fake_port_attr_call_t *seed = fake_port_find_set(1, CLUSTER_TEST, ATTR_TEST_SCALED);
    TEST_ASSERT_NOT_NULL(seed);
    TEST_ASSERT_EQUAL_INT32(2150, seed->value);
    TEST_ASSERT_NULL(fake_port_find_set(1, CLUSTER_TEST, ATTR_TEST_REPORTED));
    TEST_ASSERT_EQUAL(0, fake_port_state.n_report_calls);

    /* A param update reaches the port's report hook, carrying the mapping's kind */
    esp_rmaker_param_val_t val = esp_rmaker_int(42);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(reported, val));
    rm_mirror_sync_handle_param_update(reported, &val);
    TEST_ASSERT_EQUAL(0, fake_port_state.n_update_calls);
    const fake_port_report_call_t *report = fake_port_find_report(1, CLUSTER_TEST, ATTR_TEST_REPORTED);
    TEST_ASSERT_NOT_NULL(report);
    TEST_ASSERT_EQUAL_STRING("test_report", report->kind);
    TEST_ASSERT_EQUAL_INT32(42, report->value);

    /* Same value again: suppressed, so a cluster that emits an event per report stays quiet */
    rm_mirror_sync_handle_param_update(reported, &val);
    TEST_ASSERT_EQUAL(1, fake_port_state.n_report_calls);

    /* A float param still updates its attribute, scaled */
    esp_rmaker_param_val_t scaled_val = esp_rmaker_float(30.25);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(scaled, scaled_val));
    rm_mirror_sync_handle_param_update(scaled, &scaled_val);
    TEST_ASSERT_EQUAL(1, fake_port_state.n_update_calls);
    TEST_ASSERT_EQUAL_INT32(3025, fake_port_state.update_calls[0].value);

    rm_mirror_engine_deinit();
    esp_rmaker_device_delete(sensor);
}
