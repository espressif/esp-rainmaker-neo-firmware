/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_controller_handles.c
 * @brief Tests for the handle lifetime: survivors keep their handles, dropped objects go stale.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "controller_internal.h"

static const char *k_config =
    "{\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\",\"primary\":\"Power\","
    "\"params\":[{\"id\":\"Power\",\"type\":\"esp.param.power\",\"data_type\":\"bool\","
    "\"properties\":[\"read\",\"write\"]},"
    "{\"id\":\"Brightness\",\"type\":\"esp.param.brightness\",\"data_type\":\"int\","
    "\"properties\":[\"read\",\"write\"]}]}]}";

static const char *k_config_without_brightness =
    "{\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\","
    "\"params\":[{\"id\":\"Power\",\"type\":\"esp.param.power\",\"data_type\":\"bool\","
    "\"properties\":[\"read\",\"write\"]}]}]}";

static ctrl_node_t *fresh_node(const char *node_id, const char *subgroup)
{
    ctrl_node_t *node = controller_model_node_alloc(node_id);
    TEST_ASSERT_NOT_NULL(node);
    if (subgroup) {
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_node_add_subgroup(node, subgroup));
    }
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_node_set_group(node, "grp123"));
    return node;
}

static ctrl_node_t *link_nodes(ctrl_node_t *a, ctrl_node_t *b)
{
    a->next = b;
    return a;
}

static void sync_inventory(ctrl_node_t *nodes, ctrl_subgroup_t *subgroups)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_lock());
    controller_model_replace_inventory(nodes, subgroups);
    controller_model_unlock();
}

static void parse(esp_rmaker_controller_node_t node, const char *config)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          controller_parse_node_config(controller_node_from(node), config, strlen(config)));
}

static esp_rmaker_controller_param_t find(esp_rmaker_controller_node_t node, const char *param_type)
{
    return esp_rmaker_controller_node_find_param(node, "esp.device.lightbulb", param_type);
}

void test_controller_handle_resolves_only_its_own_kind_and_generation(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    int obj = 0;
    esp_rmaker_handle_t h = controller_handle_alloc(&obj, CTRL_KIND_PARAM);
    TEST_ASSERT_NOT_EQUAL(0, h);
    TEST_ASSERT_EQUAL_PTR(&obj, controller_handle_resolve(h, CTRL_KIND_PARAM));
    TEST_ASSERT_NULL(controller_handle_resolve(h, CTRL_KIND_NODE));
    TEST_ASSERT_NULL(controller_handle_resolve(0, CTRL_KIND_PARAM));

    void *out = &obj;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG, controller_handle_lookup(h, CTRL_KIND_DEVICE, &out));
    TEST_ASSERT_NULL(out);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG, controller_handle_lookup(0, CTRL_KIND_PARAM, &out));

    controller_handle_free(h);
    TEST_ASSERT_NULL(controller_handle_resolve(h, CTRL_KIND_PARAM));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, controller_handle_lookup(h, CTRL_KIND_PARAM, &out));
    /* An index past the table is stale, not a crash. */
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, controller_handle_lookup(0xFFFF, CTRL_KIND_PARAM, &out));
}

void test_controller_handle_of_a_reused_slot_does_not_match_the_old_handle(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    int a = 0;
    int b = 0;
    esp_rmaker_handle_t old = controller_handle_alloc(&a, CTRL_KIND_PARAM);
    controller_handle_free(old);
    /* Take slots until the old index comes back. */
    esp_rmaker_handle_t taken[256];
    size_t num = 0;
    esp_rmaker_handle_t reused = 0;
    while (num < sizeof(taken) / sizeof(taken[0])) {
        esp_rmaker_handle_t h = controller_handle_alloc(&b, CTRL_KIND_PARAM);
        taken[num++] = h;
        if ((h & 0xFFFF) == (old & 0xFFFF)) {
            reused = h;
            break;
        }
    }
    TEST_ASSERT_NOT_EQUAL(0, reused);
    TEST_ASSERT_NOT_EQUAL(old, reused);
    TEST_ASSERT_NULL(controller_handle_resolve(old, CTRL_KIND_PARAM));
    TEST_ASSERT_EQUAL_PTR(&b, controller_handle_resolve(reused, CTRL_KIND_PARAM));
    for (size_t i = 0; i < num; i++) {
        controller_handle_free(taken[i]);
    }
}

void test_controller_handle_node_survives_a_resync_that_keeps_it(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    sync_inventory(link_nodes(fresh_node("nodeA", NULL), fresh_node("nodeB", NULL)), NULL);
    esp_rmaker_controller_node_t a = esp_rmaker_controller_node_find("nodeA");
    TEST_ASSERT_NOT_EQUAL(0, a);
    parse(a, k_config);
    esp_rmaker_controller_param_t power = find(a, "esp.param.power");

    /* nodeA moved under a subgroup, so its shadow name changes. */
    sync_inventory(fresh_node("nodeA", "sgA"), NULL);
    TEST_ASSERT_EQUAL(a, esp_rmaker_controller_node_find("nodeA"));
    TEST_ASSERT_EQUAL_STRING("params-grp123-sgA", controller_node_from(a)->shadow);
    TEST_ASSERT_TRUE(esp_rmaker_controller_node_in_subgroup(a, "sgA"));
    TEST_ASSERT_EQUAL(power, find(a, "esp.param.power"));
    sync_inventory(NULL, NULL);
}

void test_controller_handle_node_goes_stale_when_a_resync_drops_it(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    sync_inventory(link_nodes(fresh_node("nodeA", NULL), fresh_node("nodeB", NULL)), NULL);
    esp_rmaker_controller_node_t a = esp_rmaker_controller_node_find("nodeA");
    parse(a, k_config);
    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_id(a, "Light");
    esp_rmaker_controller_param_t power = find(a, "esp.param.power");

    sync_inventory(fresh_node("nodeB", NULL), NULL);
    char *str = NULL;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_node_get_id(a, &str));
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_next(a));
    TEST_ASSERT_FALSE(esp_rmaker_controller_node_is_online(a));
    /* The devices and params of a dropped node go with it. */
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_device_next(light));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_device_get_id(light, &str));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_INVALID, esp_rmaker_controller_param_get_data_type(power));
    sync_inventory(NULL, NULL);
}

void test_controller_handle_subgroup_survives_a_resync_and_takes_its_new_name(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    sync_inventory(NULL, controller_model_subgroup_alloc("sgA", "Kitchen"));
    esp_rmaker_controller_subgroup_t sg = esp_rmaker_controller_subgroup_first();
    TEST_ASSERT_NOT_EQUAL(0, sg);

    sync_inventory(NULL, controller_model_subgroup_alloc("sgA", "Cooking"));
    TEST_ASSERT_EQUAL(sg, esp_rmaker_controller_subgroup_first());
    char *name = NULL;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_subgroup_get_name(sg, &name));
    TEST_ASSERT_EQUAL_STRING("Cooking", name);
    free(name);

    sync_inventory(NULL, controller_model_subgroup_alloc("sgB", "Hall"));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_subgroup_get_name(sg, &name));
    TEST_ASSERT_NULL(name);
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_subgroup_next(sg));
    sync_inventory(NULL, NULL);
}

void test_controller_handle_dropped_param_is_stale_and_a_restore_gets_a_new_one(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node9");
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    parse(nh, k_config);
    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_id(nh, "Light");
    esp_rmaker_controller_param_t power = find(nh, "esp.param.power");
    esp_rmaker_controller_param_t bright = find(nh, "esp.param.brightness");

    parse(nh, k_config_without_brightness);
    TEST_ASSERT_EQUAL(light, esp_rmaker_controller_node_device_by_id(nh, "Light"));
    TEST_ASSERT_EQUAL(power, find(nh, "esp.param.power"));
    char *str = NULL;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_param_get_id(bright, &str));
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_param_next(bright));

    parse(nh, k_config);
    esp_rmaker_controller_param_t restored = find(nh, "esp.param.brightness");
    TEST_ASSERT_NOT_EQUAL(0, restored);
    TEST_ASSERT_NOT_EQUAL(bright, restored);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_param_get_id(bright, &str));
    controller_model_free_node_list(node);
}

void test_controller_handle_param_is_stale_after_release_config(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node9");
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    parse(nh, k_config);
    esp_rmaker_controller_param_t power = find(nh, "esp.param.power");

    esp_rmaker_controller_node_release_config(nh);
    esp_rmaker_param_val_t val;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_param_get_val(power, &val));
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_param_get_device(power));
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_device_first(nh));
    controller_model_free_node_list(node);
}

void test_controller_handle_of_another_kind_is_an_invalid_arg(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node9");
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    parse(nh, k_config);
    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_id(nh, "Light");

    char *str = NULL;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG, esp_rmaker_controller_node_get_id(light, &str));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG, esp_rmaker_controller_node_get_id(0, &str));
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_device_first(light));
    controller_model_free_node_list(node);
}

void test_controller_handle_getter_returns_a_copy_the_caller_frees(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node9");
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    parse(nh, k_config);
    esp_rmaker_controller_param_t bright = find(nh, "esp.param.brightness");

    char *id = NULL;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_param_get_id(bright, &id));
    TEST_ASSERT_EQUAL_STRING("Brightness", id);
    /* The copy outlives the param. */
    esp_rmaker_controller_node_release_config(nh);
    TEST_ASSERT_EQUAL_STRING("Brightness", id);
    free(id);

    /* A failed call leaves NULL, also over a value the caller passed in. */
    char sentinel = 'x';
    id = &sentinel;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_param_get_id(bright, &id));
    TEST_ASSERT_NULL(id);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG, esp_rmaker_controller_param_get_id(bright, NULL));
    controller_model_free_node_list(node);
}

void test_controller_handle_device_param_by_id(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node9");
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    parse(nh, k_config);
    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_id(nh, "Light");

    TEST_ASSERT_EQUAL(find(nh, "esp.param.power"), esp_rmaker_controller_device_param_by_id(light, "Power"));
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_device_param_by_id(light, "Hue"));
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_device_param_by_id(nh, "Power"));
    controller_model_free_node_list(node);
}

static int s_notified;

static void count_param(esp_rmaker_controller_param_t param, void *priv)
{
    (void)param;
    (void)priv;
    s_notified++;
}

void test_controller_handle_notify_skips_a_released_param(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node9");
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    parse(nh, k_config);
    esp_rmaker_controller_param_t queued[] = { find(nh, "esp.param.power"), find(nh, "esp.param.brightness") };

    s_notified = 0;
    esp_rmaker_controller_watch_params(count_param, NULL);
    controller_watch_deliver_params(queued, 2);
    TEST_ASSERT_EQUAL_INT(2, s_notified);

    /* The notification was queued before the release, and runs after it. */
    esp_rmaker_controller_node_release_config(nh);
    s_notified = 0;
    controller_watch_deliver_params(queued, 2);
    TEST_ASSERT_EQUAL_INT(0, s_notified);
    esp_rmaker_controller_unwatch_params();
    controller_model_free_node_list(node);
}

void test_controller_handle_batch_with_a_dropped_param_renders_nothing(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node9");
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    parse(nh, k_config);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_node_write_begin(nh));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_node_write_add(nh, find(nh, "esp.param.brightness"),
                          esp_rmaker_int(40)));

    parse(nh, k_config_without_brightness);
    char payload[128] = "";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, controller_render_node_batch(node, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_STRING("", payload);
    esp_rmaker_controller_node_write_abort(nh);
    controller_model_free_node_list(node);
}

void test_controller_handle_parser_skips_fields_that_do_not_fit(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node9");
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    /* A 64-char type does not fit CONTROLLER_CFG_TYPE_MAX_LEN with its NUL. */
    const char *body =
        "{\"devices\":["
        "{\"id\":\"Sixteen_chars_id\",\"type\":\"esp.device.fan\",\"params\":[]},"
        "{\"id\":\"Sixteen_chars_id\",\"name\":\"Short\",\"type\":\"esp.device.fan\",\"params\":[]},"
        "{\"name\":\"Legacy\",\"type\":\"esp.device.fan\",\"params\":["
        "{\"id\":\"Long\",\"type\":\"esp.param.012345678901234567890123456789012345678901234567890123\","
        "\"data_type\":\"int\"},"
        "{\"id\":\"Ok\",\"type\":\"esp.param.ok\",\"data_type\":\"int\"}]}]}";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_node_config(node, body, strlen(body)));

    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_device_by_id(nh, "Sixteen_chars_id"));
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_device_by_id(nh, "Short"));
    esp_rmaker_controller_device_t legacy = esp_rmaker_controller_node_device_by_id(nh, "Legacy");
    TEST_ASSERT_NOT_EQUAL(0, legacy);
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_device_param_by_id(legacy, "Long"));
    TEST_ASSERT_NOT_EQUAL(0, esp_rmaker_controller_device_param_by_id(legacy, "Ok"));
    controller_model_free_node_list(node);
}

void test_controller_handle_parser_skips_a_second_object_with_the_same_id(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node9");
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    const char *body =
        "{\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\",\"params\":["
        "{\"id\":\"Power\",\"type\":\"esp.param.power\"},{\"id\":\"Power\",\"type\":\"esp.param.other\"}]},"
        "{\"id\":\"Light\",\"type\":\"esp.device.fan\",\"params\":[]}]}";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_node_config(node, body, strlen(body)));

    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_id(nh, "Light");
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_device_next(light));
    TEST_ASSERT_NOT_EQUAL(0, esp_rmaker_controller_node_device_by_type(nh, "esp.device.lightbulb"));
    esp_rmaker_controller_param_t power = esp_rmaker_controller_device_param_by_id(light, "Power");
    TEST_ASSERT_NOT_EQUAL(0, esp_rmaker_controller_device_param_by_type(light, "esp.param.power"));
    TEST_ASSERT_EQUAL(0, esp_rmaker_controller_param_next(power));
    controller_model_free_node_list(node);
}

typedef struct {
    int subgroups;
    int members;
    char group_name[CONTROLLER_GROUP_NAME_MAX_LEN];
} group_seen_t;

static esp_rmaker_error_t seen_group(void *ctx, const char *group_name)
{
    snprintf(((group_seen_t *)ctx)->group_name, CONTROLLER_GROUP_NAME_MAX_LEN, "%s", group_name);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t seen_subgroup(void *ctx, const char *subgroup_id, const char *subgroup_name)
{
    (void)subgroup_id;
    (void)subgroup_name;
    ((group_seen_t *)ctx)->subgroups++;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t seen_member(void *ctx, const char *subgroup_id, const char *node_id)
{
    (void)subgroup_id;
    (void)node_id;
    ((group_seen_t *)ctx)->members++;
    return ESP_RMAKER_OK;
}

void test_controller_handle_parser_skips_a_name_that_does_not_fit(void)
{
    char name64[65];
    memset(name64, 'n', 64);
    name64[64] = '\0';
    char body[384];
    snprintf(body, sizeof(body),
             "{\"group_name\":\"%s\",\"node_ids\":[\"nodeA\"],\"subgroups\":["
             "{\"subgroup_id\":\"sgA\",\"subgroup_name\":\"%s\",\"node_ids\":[\"nodeA\"]},"
             "{\"subgroup_id\":\"sgB\",\"subgroup_name\":\"Hall\"}]}", name64, name64);
    const controller_group_info_cbs_t cbs = {
        .group = seen_group,
        .subgroup = seen_subgroup,
        .member = seen_member,
    };
    group_seen_t seen = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_group_info(body, strlen(body), &cbs, &seen));
    TEST_ASSERT_EQUAL_STRING("", seen.group_name);
    TEST_ASSERT_EQUAL_INT(1, seen.subgroups);
    /* The members of the skipped subgroup still count, because the shadow name follows them. */
    TEST_ASSERT_EQUAL_INT(1, seen.members);
}
