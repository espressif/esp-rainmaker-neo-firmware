/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_controller_config.c
 * @brief Tests for parsing a node config into device and param handles.
 *
 * The re-fetch is the part worth pinning: a survivor keeps its handle, and a dropped object goes
 * stale.
 */

#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "controller_internal.h"

/* A light with a writable power param and a read-only brightness range, plus a timezone service. */
static const char *k_config =
    "{\"node_id\":\"node9\",\"data_model\":\"default\","
    "\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\",\"primary\":\"Power\","
    "\"params\":[{\"id\":\"Power\",\"type\":\"esp.param.power\",\"data_type\":\"bool\","
    "\"properties\":[\"read\",\"write\"],\"ui_type\":\"esp.ui.toggle\"},"
    "{\"id\":\"Brightness\",\"type\":\"esp.param.brightness\",\"data_type\":\"int\","
    "\"properties\":[\"read\"],\"bounds\":{\"min\":0,\"max\":100,\"step\":5}}]}],"
    "\"services\":[{\"id\":\"Time\",\"type\":\"esp.service.time\","
    "\"params\":[{\"id\":\"TZ\",\"type\":\"esp.param.tz\",\"data_type\":\"string\","
    "\"properties\":[\"read\",\"write\"]}]}]}";

static ctrl_node_t *s_node;
static char *s_str;

/* Hold the latest copy, so a test can compare it without a free of its own. */
static const char *hold(char *str)
{
    free(s_str);
    s_str = str;
    return s_str ? s_str : "";
}

static const char *device_id(esp_rmaker_controller_device_t dev)
{
    char *id = NULL;
    esp_rmaker_controller_device_get_id(dev, &id);
    return hold(id);
}

static const char *param_id(esp_rmaker_controller_param_t param)
{
    char *id = NULL;
    esp_rmaker_controller_param_get_id(param, &id);
    return hold(id);
}

static ctrl_node_t *make_node(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node9");
    TEST_ASSERT_NOT_NULL(node);
    return node;
}

static void drop_node(ctrl_node_t *node)
{
    controller_model_free_node_list(node);
}

void test_controller_config_parses_devices_and_params(void)
{
    s_node = make_node();
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          controller_parse_node_config(s_node, k_config, strlen(k_config)));
    TEST_ASSERT_TRUE(s_node->config_held);

    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(s_node);
    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_type(nh, "esp.device.lightbulb");
    TEST_ASSERT_NOT_EQUAL(0, light);
    TEST_ASSERT_EQUAL_STRING("Light", device_id(light));
    TEST_ASSERT_FALSE(esp_rmaker_controller_device_is_service(light));

    esp_rmaker_controller_param_t power = esp_rmaker_controller_device_param_by_type(light, "esp.param.power");
    TEST_ASSERT_NOT_EQUAL(0, power);
    TEST_ASSERT_EQUAL_STRING("Power", param_id(power));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_BOOLEAN, esp_rmaker_controller_param_get_data_type(power));
    TEST_ASSERT_EQUAL_INT(PROP_FLAG_READ | PROP_FLAG_WRITE, esp_rmaker_controller_param_get_props(power));
    esp_rmaker_param_bounds_t bounds;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_param_get_bounds(power, &bounds));
    drop_node(s_node);
}

void test_controller_config_parses_bounds_on_a_numeric_param(void)
{
    s_node = make_node();
    controller_parse_node_config(s_node, k_config, strlen(k_config));
    esp_rmaker_controller_param_t bright = esp_rmaker_controller_node_find_param(
            CONTROLLER_HANDLE(s_node), "esp.device.lightbulb", "esp.param.brightness");
    TEST_ASSERT_NOT_EQUAL(0, bright);
    esp_rmaker_param_bounds_t bounds;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_param_get_bounds(bright, &bounds));
    TEST_ASSERT_EQUAL_INT(0, bounds.min.val.i);
    TEST_ASSERT_EQUAL_INT(100, bounds.max.val.i);
    TEST_ASSERT_EQUAL_INT(5, bounds.step.val.i);
    /* Read-only, so a write must be refused client-side rather than silently ignored by the node. */
    TEST_ASSERT_EQUAL_INT(PROP_FLAG_READ, esp_rmaker_controller_param_get_props(bright));
    drop_node(s_node);
}

void test_controller_config_parses_int64_bounds(void)
{
    static const char *config =
        "{\"node_id\":\"node9\",\"devices\":[{\"id\":\"Meter\",\"type\":\"esp.device.meter\","
        "\"params\":[{\"id\":\"Energy\",\"type\":\"esp.param.energy\",\"data_type\":\"int64\","
        "\"properties\":[\"read\"],\"bounds\":{\"min\":0,\"max\":8589934592,\"step\":1}}]}]}";
    s_node = make_node();
    controller_parse_node_config(s_node, config, strlen(config));
    esp_rmaker_controller_param_t energy = esp_rmaker_controller_node_find_param(
            CONTROLLER_HANDLE(s_node), "esp.device.meter", "esp.param.energy");
    TEST_ASSERT_NOT_EQUAL(0, energy);
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_INT64, esp_rmaker_controller_param_get_data_type(energy));
    esp_rmaker_param_bounds_t bounds;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_param_get_bounds(energy, &bounds));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_INT64, bounds.max.type);
    TEST_ASSERT_TRUE(bounds.max.val.i64 == 8589934592LL);
    drop_node(s_node);
}

void test_controller_config_parses_services_into_the_same_list(void)
{
    /* Devices and services share one list, matching esp_rmaker_node_get_first_device() on the node
     * side, so a service is addressable exactly like a device. */
    s_node = make_node();
    controller_parse_node_config(s_node, k_config, strlen(k_config));
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(s_node);

    int count = 0;
    for (esp_rmaker_controller_device_t d = esp_rmaker_controller_node_device_first(nh); d;
            d = esp_rmaker_controller_device_next(d)) {
        count++;
    }
    TEST_ASSERT_EQUAL_INT(2, count);

    esp_rmaker_controller_device_t svc = esp_rmaker_controller_node_service_by_type(nh, "esp.service.time");
    TEST_ASSERT_NOT_EQUAL(0, svc);
    TEST_ASSERT_TRUE(esp_rmaker_controller_device_is_service(svc));
    TEST_ASSERT_NOT_EQUAL(0, esp_rmaker_controller_device_param_by_type(svc, "esp.param.tz"));
    drop_node(s_node);
}

void test_controller_config_accepts_name_as_a_legacy_alias_for_id(void)
{
    /* Config payloads key by "id"; an older producer emitting "name" still parses. */
    const char *body =
        "{\"devices\":[{\"name\":\"Light\",\"type\":\"esp.device.lightbulb\","
        "\"params\":[{\"name\":\"Power\",\"type\":\"esp.param.power\",\"data_type\":\"bool\","
        "\"properties\":[\"read\",\"write\"]}]}]}";
    s_node = make_node();
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_node_config(s_node, body, strlen(body)));
    TEST_ASSERT_NOT_EQUAL(0, esp_rmaker_controller_node_device_by_id(CONTROLLER_HANDLE(s_node), "Light"));
    drop_node(s_node);
}

void test_controller_config_parses_the_ui_type_and_the_primary_param(void)
{
    /* A hub picks its widget from ui_type and its card state from the primary param, so a parser
     * that drops either field forces every consumer to guess. */
    s_node = make_node();
    controller_parse_node_config(s_node, k_config, strlen(k_config));
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(s_node);

    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_type(nh, "esp.device.lightbulb");
    esp_rmaker_controller_param_t power = esp_rmaker_controller_device_param_by_type(light, "esp.param.power");
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_TOGGLE, esp_rmaker_controller_param_get_ui_type(power));
    TEST_ASSERT_EQUAL(power, esp_rmaker_controller_device_get_primary_param(light));

    /* A config that names no ui_type reports none rather than a guess. */
    esp_rmaker_controller_param_t bright = esp_rmaker_controller_device_param_by_type(light, "esp.param.brightness");
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_NONE, esp_rmaker_controller_param_get_ui_type(bright));

    /* A device that names no primary reports none. */
    esp_rmaker_controller_device_t svc = esp_rmaker_controller_node_service_by_type(nh, "esp.service.time");
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_device_get_primary_param(svc));
    drop_node(s_node);
}

void test_controller_config_primary_param_reports_none_once_it_is_dropped(void)
{
    s_node = make_node();
    controller_parse_node_config(s_node, k_config, strlen(k_config));
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(s_node);

    /* The node kept the primary id but dropped the param it names. */
    const char *without_power =
        "{\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\",\"primary\":\"Power\","
        "\"params\":[{\"id\":\"Brightness\",\"type\":\"esp.param.brightness\",\"data_type\":\"int\","
        "\"properties\":[\"read\"]}]}]}";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          controller_parse_node_config(s_node, without_power, strlen(without_power)));
    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_type(nh, "esp.device.lightbulb");
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_device_get_primary_param(light));
    drop_node(s_node);
}

void test_controller_config_reports_the_parents_of_a_param(void)
{
    /* A change callback hands out a bare param handle, so without these a consumer rescans the
     * whole group to name its owner. */
    s_node = make_node();
    controller_parse_node_config(s_node, k_config, strlen(k_config));
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(s_node);
    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_type(nh, "esp.device.lightbulb");
    esp_rmaker_controller_param_t power = esp_rmaker_controller_device_param_by_type(light, "esp.param.power");

    TEST_ASSERT_EQUAL(light, esp_rmaker_controller_param_get_device(power));
    TEST_ASSERT_EQUAL(nh, esp_rmaker_controller_device_get_node(light));
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_param_get_device(0));
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_device_get_node(0));
    drop_node(s_node);
}

void test_controller_config_maps_every_ui_type(void)
{
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_TOGGLE, controller_parse_ui_type(ESP_RMAKER_UI_TOGGLE));
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_SLIDER, controller_parse_ui_type(ESP_RMAKER_UI_SLIDER));
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_DROPDOWN, controller_parse_ui_type(ESP_RMAKER_UI_DROPDOWN));
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_TEXT, controller_parse_ui_type(ESP_RMAKER_UI_TEXT));
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_HUE_SLIDER, controller_parse_ui_type(ESP_RMAKER_UI_HUE_SLIDER));
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_HUE_CIRCLE, controller_parse_ui_type(ESP_RMAKER_UI_HUE_CIRCLE));
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_PUSHBUTTON, controller_parse_ui_type(ESP_RMAKER_UI_PUSHBUTTON));
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_TRIGGER, controller_parse_ui_type(ESP_RMAKER_UI_TRIGGER));
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_HIDDEN, controller_parse_ui_type(ESP_RMAKER_UI_HIDDEN));
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_QR_SCAN, controller_parse_ui_type(ESP_RMAKER_UI_QR_SCAN));
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_UNKNOWN, controller_parse_ui_type("esp.ui.hologram"));
    TEST_ASSERT_EQUAL_INT(RMAKER_UI_TYPE_NONE, controller_parse_ui_type(NULL));
}

void test_controller_config_maps_every_data_type(void)
{
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_BOOLEAN, controller_parse_data_type("bool"));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_INTEGER, controller_parse_data_type("int"));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_INT64, controller_parse_data_type("int64"));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_FLOAT, controller_parse_data_type("float"));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_STRING, controller_parse_data_type("string"));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_OBJECT, controller_parse_data_type("object"));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_ARRAY, controller_parse_data_type("array"));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_INVALID, controller_parse_data_type("cluster"));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_INVALID, controller_parse_data_type(NULL));
}

void test_controller_config_reads_the_data_model(void)
{
    char model[CONTROLLER_DATA_MODEL_MAX_LEN];
    const char *matter = "{\"data_model\":\"matter\",\"endpoints\":[]}";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          controller_parse_data_model(matter, strlen(matter), model, sizeof(model)));
    TEST_ASSERT_EQUAL_STRING("matter", model);

    /* A body predating the field is the default model, not an unknown one. */
    const char *legacy = "{\"devices\":[]}";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          controller_parse_data_model(legacy, strlen(legacy), model, sizeof(model)));
    TEST_ASSERT_EQUAL_STRING(CONTROLLER_DATA_MODEL_DEFAULT, model);

    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          controller_parse_data_model("not json", 8, model, sizeof(model)));
}

void test_controller_config_update_keeps_surviving_handles_valid(void)
{
    s_node = make_node();
    controller_parse_node_config(s_node, k_config, strlen(k_config));
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(s_node);
    esp_rmaker_controller_param_t power = esp_rmaker_controller_node_find_param(
            nh, "esp.device.lightbulb", "esp.param.power");
    esp_rmaker_controller_param_t bright = esp_rmaker_controller_node_find_param(
            nh, "esp.device.lightbulb", "esp.param.brightness");
    TEST_ASSERT_NOT_EQUAL(0, power);
    TEST_ASSERT_NOT_EQUAL(0, bright);

    /* The node rebooted having dropped Brightness and gained Hue. */
    const char *changed =
        "{\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\","
        "\"params\":[{\"id\":\"Power\",\"type\":\"esp.param.power\",\"data_type\":\"bool\","
        "\"properties\":[\"read\",\"write\"]},"
        "{\"id\":\"Hue\",\"type\":\"esp.param.hue\",\"data_type\":\"int\","
        "\"properties\":[\"read\",\"write\"]}]}]}";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_node_config(s_node, changed, strlen(changed)));

    /* The survivor's handle still resolves to the same param. */
    TEST_ASSERT_EQUAL(power, esp_rmaker_controller_node_find_param(nh, "esp.device.lightbulb", "esp.param.power"));
    TEST_ASSERT_EQUAL_STRING("Power", param_id(power));

    /* The dropped one is stale and is gone from iteration. */
    esp_rmaker_param_val_t val;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_param_get_val(bright, &val));
    char *id = NULL;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_param_get_id(bright, &id));
    TEST_ASSERT_NULL(id);
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_find_param(nh, "esp.device.lightbulb", "esp.param.brightness"));
    TEST_ASSERT_NOT_EQUAL(0, esp_rmaker_controller_node_find_param(nh, "esp.device.lightbulb", "esp.param.hue"));

    int live = 0;
    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_type(nh, "esp.device.lightbulb");
    for (esp_rmaker_controller_param_t p = esp_rmaker_controller_device_param_first(light); p;
            p = esp_rmaker_controller_param_next(p)) {
        live++;
    }
    TEST_ASSERT_EQUAL_INT(2, live);
    drop_node(s_node);
}

void test_controller_config_update_makes_a_dropped_device_stale(void)
{
    s_node = make_node();
    controller_parse_node_config(s_node, k_config, strlen(k_config));
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(s_node);
    esp_rmaker_controller_device_t svc = esp_rmaker_controller_node_service_by_type(nh, "esp.service.time");
    TEST_ASSERT_NOT_EQUAL(0, svc);

    const char *without_service =
        "{\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\",\"params\":[]}]}";
    controller_parse_node_config(s_node, without_service, strlen(without_service));

    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_service_by_type(nh, "esp.service.time"));
    TEST_ASSERT_FALSE(esp_rmaker_controller_device_is_service(svc));
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_device_param_first(svc));
    int count = 0;
    for (esp_rmaker_controller_device_t d = esp_rmaker_controller_node_device_first(nh); d;
            d = esp_rmaker_controller_device_next(d)) {
        count++;
    }
    TEST_ASSERT_EQUAL_INT(1, count);
    drop_node(s_node);
}
