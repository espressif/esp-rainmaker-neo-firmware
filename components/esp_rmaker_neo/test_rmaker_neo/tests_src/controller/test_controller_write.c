/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_controller_write.c
 * @brief Tests for the three write payload shapes.
 *
 * The SDK owns the keying difference an application would otherwise have to know: a node write is
 * keyed by device and param NAME, a group write by TYPE and wrapped in a "params" envelope. Getting
 * either wrong produces a publish the receiving node parses and then ignores.
 */

#include <string.h>

#include "unity.h"

#include "controller_internal.h"

static char s_payload[256];

static const char *k_config =
    "{\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\","
    "\"params\":[{\"id\":\"Power\",\"type\":\"esp.param.power\",\"data_type\":\"bool\","
    "\"properties\":[\"read\",\"write\"]},"
    "{\"id\":\"Brightness\",\"type\":\"esp.param.brightness\",\"data_type\":\"int\","
    "\"properties\":[\"read\"]}]},"
    "{\"id\":\"Fan\",\"type\":\"esp.device.fan\","
    "\"params\":[{\"id\":\"Speed\",\"type\":\"esp.param.speed\",\"data_type\":\"int\","
    "\"properties\":[\"read\",\"write\"]}]}]}";

static ctrl_node_t *make_node(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node9");
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_node_config(node, k_config, strlen(k_config)));
    return node;
}

static ctrl_param_t *param_of(ctrl_node_t *node, const char *device_type, const char *param_type)
{
    return controller_param_from(esp_rmaker_controller_node_find_param(CONTROLLER_HANDLE(node),
                                 device_type, param_type));
}

void test_controller_write_node_payload_is_keyed_by_id(void)
{
    ctrl_node_t *node = make_node();
    esp_rmaker_param_val_t val = esp_rmaker_bool(true);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          controller_render_node_write(param_of(node, "esp.device.lightbulb", "esp.param.power"),
                                  &val, s_payload, sizeof(s_payload)));
    TEST_ASSERT_EQUAL_STRING("{\"Light\":{\"Power\":true}}", s_payload);
    controller_model_free_node_list(node);
}

void test_controller_write_refuses_a_read_only_param(void)
{
    /* The node does not enforce this, so refusing here turns a silently-ignored publish into an
     * immediate error. */
    ctrl_node_t *node = make_node();
    esp_rmaker_param_val_t val = esp_rmaker_int(50);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          controller_render_node_write(param_of(node, "esp.device.lightbulb", "esp.param.brightness"),
                                  &val, s_payload, sizeof(s_payload)));
    controller_model_free_node_list(node);
}

void test_controller_write_batch_groups_params_by_device(void)
{
    ctrl_node_t *node = make_node();
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_node_write_begin(nh));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          esp_rmaker_controller_node_write_add(nh,
                                  esp_rmaker_controller_node_find_param(nh, "esp.device.lightbulb", "esp.param.power"),
                                  esp_rmaker_bool(false)));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          esp_rmaker_controller_node_write_add(nh,
                                  esp_rmaker_controller_node_find_param(nh, "esp.device.fan", "esp.param.speed"),
                                  esp_rmaker_int(3)));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          controller_render_node_batch(node, s_payload, sizeof(s_payload)));
    TEST_ASSERT_EQUAL_STRING("{\"Light\":{\"Power\":false},\"Fan\":{\"Speed\":3}}", s_payload);
    esp_rmaker_controller_node_write_abort(nh);
    controller_model_free_node_list(node);
}

void test_controller_write_node_batch_refuses_to_truncate(void)
{
    char small[24];
    ctrl_node_t *node = make_node();
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_node_write_begin(nh));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          esp_rmaker_controller_node_write_add(nh,
                                  esp_rmaker_controller_node_find_param(nh, "esp.device.lightbulb", "esp.param.power"),
                                  esp_rmaker_bool(false)));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          esp_rmaker_controller_node_write_add(nh,
                                  esp_rmaker_controller_node_find_param(nh, "esp.device.fan", "esp.param.speed"),
                                  esp_rmaker_int(3)));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NO_MEM, controller_render_node_batch(node, small, sizeof(small)));
    esp_rmaker_controller_node_write_abort(nh);
    controller_model_free_node_list(node);
}

void test_controller_write_batch_rejects_a_read_only_param(void)
{
    ctrl_node_t *node = make_node();
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_node_write_begin(nh));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          esp_rmaker_controller_node_write_add(nh,
                                  esp_rmaker_controller_node_find_param(nh, "esp.device.lightbulb", "esp.param.brightness"),
                                  esp_rmaker_int(50)));
    esp_rmaker_controller_node_write_abort(nh);
    controller_model_free_node_list(node);
}

void test_controller_write_batch_needs_a_begin(void)
{
    ctrl_node_t *node = make_node();
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_STATE,
                          esp_rmaker_controller_node_write_add(nh,
                                  esp_rmaker_controller_node_find_param(nh, "esp.device.fan", "esp.param.speed"),
                                  esp_rmaker_int(1)));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_node_write_begin(nh));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_STATE, esp_rmaker_controller_node_write_begin(nh));
    esp_rmaker_controller_node_write_abort(nh);
    controller_model_free_node_list(node);
}

void test_controller_write_group_payload_is_keyed_by_type(void)
{
    esp_rmaker_param_val_t val = esp_rmaker_bool(true);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          controller_render_group_write("esp.device.lightbulb", "esp.param.power", &val,
                                  s_payload, sizeof(s_payload)));
    TEST_ASSERT_EQUAL_STRING("{\"esp.device.lightbulb\":{\"params\":{\"esp.param.power\":true}}}", s_payload);
}

void test_controller_write_group_payload_reaches_a_service_type(void)
{
    /* The receiving node filters on is_service in neither mode, so a service is addressable at the
     * group level exactly like a device. */
    esp_rmaker_param_val_t val = esp_rmaker_str("Asia/Shanghai");
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          controller_render_group_write("esp.service.time", "esp.param.tz", &val,
                                  s_payload, sizeof(s_payload)));
    TEST_ASSERT_EQUAL_STRING("{\"esp.service.time\":{\"params\":{\"esp.param.tz\":\"Asia/Shanghai\"}}}",
                             s_payload);
}

void test_controller_write_group_payload_rejects_an_empty_type(void)
{
    esp_rmaker_param_val_t val = esp_rmaker_bool(true);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          controller_render_group_write("", "esp.param.power", &val, s_payload, sizeof(s_payload)));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          controller_render_group_write("esp.device.fan", "", &val, s_payload, sizeof(s_payload)));
}

void test_controller_write_group_batch_holds_every_device_type(void)
{
    /* The point of the batch: one publish drives every type of the selection. A receiving node
     * applies the types it owns a device of and ignores the rest. */
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_group_write_begin(NULL));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          esp_rmaker_controller_group_write_add("esp.device.lightbulb", "esp.param.power",
                                  esp_rmaker_bool(false)));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          esp_rmaker_controller_group_write_add("esp.device.fan", "esp.param.power",
                                  esp_rmaker_bool(false)));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          controller_render_group_batch(controller_group_batch_peek(), s_payload, sizeof(s_payload)));
    TEST_ASSERT_EQUAL_STRING("{\"esp.device.lightbulb\":{\"params\":{\"esp.param.power\":false}},"
                             "\"esp.device.fan\":{\"params\":{\"esp.param.power\":false}}}", s_payload);
    esp_rmaker_controller_group_write_abort();
}

void test_controller_write_group_batch_shares_one_envelope_per_type(void)
{
    /* Two params of one type belong in that type's single "params" object, not in two. */
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_group_write_begin(NULL));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          esp_rmaker_controller_group_write_add("esp.device.lightbulb", "esp.param.power",
                                  esp_rmaker_bool(true)));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          esp_rmaker_controller_group_write_add("esp.device.lightbulb", "esp.param.brightness",
                                  esp_rmaker_int(40)));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          controller_render_group_batch(controller_group_batch_peek(), s_payload, sizeof(s_payload)));
    TEST_ASSERT_EQUAL_STRING("{\"esp.device.lightbulb\":{\"params\":"
                             "{\"esp.param.power\":true,\"esp.param.brightness\":40}}}", s_payload);
    esp_rmaker_controller_group_write_abort();
}

void test_controller_write_group_batch_needs_a_begin(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_STATE,
                          esp_rmaker_controller_group_write_add("esp.device.fan", "esp.param.power",
                                  esp_rmaker_bool(true)));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_group_write_begin(NULL));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_STATE, esp_rmaker_controller_group_write_begin(NULL));
    esp_rmaker_controller_group_write_abort();
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_STATE, esp_rmaker_controller_group_write_commit());
}

void test_controller_write_group_batch_refuses_to_truncate(void)
{
    /* A batch too large for one payload is refused whole, rather than published half-formed. */
    char small[64];
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_group_write_begin(NULL));
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                              esp_rmaker_controller_group_write_add("esp.device.lightbulb", "esp.param.power",
                                      esp_rmaker_bool(true)));
    }
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NO_MEM,
                          controller_render_group_batch(controller_group_batch_peek(), small, sizeof(small)));
    esp_rmaker_controller_group_write_abort();
}

void test_controller_write_group_payload_refuses_invalid_utf8(void)
{
    esp_rmaker_param_val_t val = esp_rmaker_str("\xff\xfe");
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                          controller_render_group_write("esp.service.time", "esp.param.tz", &val,
                                  s_payload, sizeof(s_payload)));
}

void test_controller_write_group_payload_refuses_to_truncate(void)
{
    char small[16];
    esp_rmaker_param_val_t val = esp_rmaker_bool(true);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NO_MEM,
                          controller_render_group_write("esp.device.lightbulb", "esp.param.power", &val,
                                  small, sizeof(small)));
}
