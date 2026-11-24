/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_controller_changes.c
 * @brief Tests for the change lists: what each rebuild reports, in which order, and to whom.
 *
 * The controller task does not run here, so each test delivers the queued lists itself.
 */

#include <string.h>

#include "unity.h"

#include "controller_internal.h"

#define LISTS_MAX   8
#define ENTRIES_MAX 32

typedef struct {
    size_t num;
    esp_rmaker_controller_change_t items[ENTRIES_MAX];
} seen_list_t;

static seen_list_t s_lists[LISTS_MAX];
static size_t s_num_lists;

static void record(const esp_rmaker_controller_change_t *changes, size_t count, void *priv)
{
    (void)priv;
    TEST_ASSERT_TRUE(s_num_lists < LISTS_MAX);
    TEST_ASSERT_TRUE(count <= ENTRIES_MAX);
    s_lists[s_num_lists].num = count;
    memcpy(s_lists[s_num_lists].items, changes, count * sizeof(*changes));
    s_num_lists++;
}

static void deliver(void)
{
    s_num_lists = 0;
    controller_changes_drain();
}

static const char *k_config =
    "{\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\","
    "\"params\":[{\"id\":\"Power\",\"type\":\"esp.param.power\",\"data_type\":\"bool\","
    "\"properties\":[\"read\",\"write\"]},"
    "{\"id\":\"Brightness\",\"type\":\"esp.param.brightness\",\"data_type\":\"int\","
    "\"properties\":[\"read\",\"write\"],\"bounds\":{\"min\":0,\"max\":100}}]},"
    "{\"id\":\"Fan\",\"type\":\"esp.device.fan\",\"params\":[]}]}";

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

static ctrl_node_t *link_nodes(ctrl_node_t *a, ctrl_node_t *b)
{
    a->next = b;
    return a;
}

/* "nodeA" in subgroup "sgA" with k_config parsed into it, and a plain "nodeB". */
static esp_rmaker_controller_node_t setup_group(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    esp_rmaker_controller_unwatch_changes();
    sync_inventory(link_nodes(fresh_node("nodeA", "sgA"), fresh_node("nodeB", NULL)),
                   controller_model_subgroup_alloc("sgA", "Kitchen"));
    esp_rmaker_controller_node_t node = esp_rmaker_controller_node_find("nodeA");
    parse(node, k_config);
    return node;
}

/* Register, and discard the replay. */
static void watch(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_watch_changes(record, NULL));
    deliver();
}

static void teardown_group(void)
{
    esp_rmaker_controller_unwatch_changes();
    sync_inventory(NULL, NULL);
}

static void assert_entry(const esp_rmaker_controller_change_t *c, esp_rmaker_controller_change_type_t type,
                         esp_rmaker_controller_kind_t kind, esp_rmaker_handle_t handle, esp_rmaker_handle_t parent)
{
    TEST_ASSERT_EQUAL_INT(type, c->type);
    TEST_ASSERT_EQUAL_INT(kind, c->kind);
    TEST_ASSERT_EQUAL(handle, c->handle);
    TEST_ASSERT_EQUAL(parent, c->parent);
}

void test_controller_changes_register_replays_the_snapshot(void)
{
    esp_rmaker_controller_node_t a = setup_group();
    esp_rmaker_controller_node_t b = esp_rmaker_controller_node_find("nodeB");
    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_id(a, "Light");
    esp_rmaker_controller_device_t fan = esp_rmaker_controller_node_device_by_id(a, "Fan");

    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_watch_changes(record, NULL));
    deliver();
    TEST_ASSERT_EQUAL_size_t(1, s_num_lists);
    const esp_rmaker_controller_change_t *c = s_lists[0].items;
    TEST_ASSERT_EQUAL_size_t(7, s_lists[0].num);
    assert_entry(&c[0], ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_NODE, a, 0);
    assert_entry(&c[1], ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_DEVICE, light, a);
    assert_entry(&c[2], ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_PARAM,
                 esp_rmaker_controller_device_param_by_id(light, "Power"), light);
    assert_entry(&c[3], ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_PARAM,
                 esp_rmaker_controller_device_param_by_id(light, "Brightness"), light);
    assert_entry(&c[4], ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_DEVICE, fan, a);
    assert_entry(&c[5], ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_NODE, b, 0);
    assert_entry(&c[6], ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_SUBGROUP,
                 esp_rmaker_controller_subgroup_first(), 0);
    teardown_group();
}

void test_controller_changes_register_before_a_sync_replays_nothing(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    sync_inventory(NULL, NULL);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_watch_changes(record, NULL));
    deliver();
    TEST_ASSERT_EQUAL_size_t(0, s_num_lists);

    /* The first sync then reports each object as ADDED. */
    sync_inventory(fresh_node("nodeA", NULL), NULL);
    deliver();
    TEST_ASSERT_EQUAL_size_t(1, s_num_lists);
    TEST_ASSERT_EQUAL_size_t(1, s_lists[0].num);
    assert_entry(&s_lists[0].items[0], ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_NODE,
                 esp_rmaker_controller_node_find("nodeA"), 0);
    teardown_group();
}

void test_controller_changes_resync_reports_a_dropped_and_an_added_node(void)
{
    esp_rmaker_controller_node_t a = setup_group();
    esp_rmaker_controller_node_t b = esp_rmaker_controller_node_find("nodeB");
    watch();

    /* nodeA leaves, nodeB stays as it was, nodeC joins, and the subgroup keeps its name. */
    sync_inventory(link_nodes(fresh_node("nodeB", NULL), fresh_node("nodeC", NULL)),
                   controller_model_subgroup_alloc("sgA", "Kitchen"));
    deliver();
    TEST_ASSERT_EQUAL_size_t(1, s_num_lists);
    TEST_ASSERT_EQUAL_size_t(2, s_lists[0].num);
    /* REMOVED first, and no entries for the devices and params of nodeA. */
    assert_entry(&s_lists[0].items[0], ESP_RMAKER_CONTROLLER_REMOVED, ESP_RMAKER_CONTROLLER_KIND_NODE, a, 0);
    assert_entry(&s_lists[0].items[1], ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_NODE,
                 esp_rmaker_controller_node_find("nodeC"), 0);
    TEST_ASSERT_EQUAL(b, esp_rmaker_controller_node_find("nodeB"));
    teardown_group();
}

void test_controller_changes_resync_reports_a_node_whose_subgroups_changed(void)
{
    esp_rmaker_controller_node_t a = setup_group();
    watch();

    sync_inventory(link_nodes(fresh_node("nodeA", NULL), fresh_node("nodeB", NULL)),
                   controller_model_subgroup_alloc("sgA", "Cooking"));
    deliver();
    TEST_ASSERT_EQUAL_size_t(1, s_num_lists);
    TEST_ASSERT_EQUAL_size_t(2, s_lists[0].num);
    assert_entry(&s_lists[0].items[0], ESP_RMAKER_CONTROLLER_UPDATED, ESP_RMAKER_CONTROLLER_KIND_NODE, a, 0);
    assert_entry(&s_lists[0].items[1], ESP_RMAKER_CONTROLLER_UPDATED, ESP_RMAKER_CONTROLLER_KIND_SUBGROUP,
                 esp_rmaker_controller_subgroup_first(), 0);
    teardown_group();
}

void test_controller_changes_refetch_reports_a_dropped_and_an_added_param(void)
{
    esp_rmaker_controller_node_t a = setup_group();
    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_id(a, "Light");
    esp_rmaker_controller_param_t bright = esp_rmaker_controller_device_param_by_id(light, "Brightness");
    watch();

    const char *changed =
        "{\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\","
        "\"params\":[{\"id\":\"Power\",\"type\":\"esp.param.power\",\"data_type\":\"bool\","
        "\"properties\":[\"read\",\"write\"]},"
        "{\"id\":\"Hue\",\"type\":\"esp.param.hue\",\"data_type\":\"int\"}]},"
        "{\"id\":\"Fan\",\"type\":\"esp.device.fan\",\"params\":[]}]}";
    parse(a, changed);
    deliver();
    TEST_ASSERT_EQUAL_size_t(1, s_num_lists);
    TEST_ASSERT_EQUAL_size_t(2, s_lists[0].num);
    assert_entry(&s_lists[0].items[0], ESP_RMAKER_CONTROLLER_REMOVED, ESP_RMAKER_CONTROLLER_KIND_PARAM, bright, light);
    assert_entry(&s_lists[0].items[1], ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_PARAM,
                 esp_rmaker_controller_device_param_by_id(light, "Hue"), light);
    teardown_group();
}

void test_controller_changes_refetch_reports_changed_bounds(void)
{
    esp_rmaker_controller_node_t a = setup_group();
    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_id(a, "Light");
    watch();

    const char *changed =
        "{\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\","
        "\"params\":[{\"id\":\"Power\",\"type\":\"esp.param.power\",\"data_type\":\"bool\","
        "\"properties\":[\"read\",\"write\"]},"
        "{\"id\":\"Brightness\",\"type\":\"esp.param.brightness\",\"data_type\":\"int\","
        "\"properties\":[\"read\",\"write\"],\"bounds\":{\"min\":0,\"max\":255}}]},"
        "{\"id\":\"Fan\",\"type\":\"esp.device.fan\",\"params\":[]}]}";
    parse(a, changed);
    deliver();
    TEST_ASSERT_EQUAL_size_t(1, s_num_lists);
    TEST_ASSERT_EQUAL_size_t(1, s_lists[0].num);
    assert_entry(&s_lists[0].items[0], ESP_RMAKER_CONTROLLER_UPDATED, ESP_RMAKER_CONTROLLER_KIND_PARAM,
                 esp_rmaker_controller_device_param_by_id(light, "Brightness"), light);
    teardown_group();
}

void test_controller_changes_refetch_with_no_change_reports_nothing(void)
{
    esp_rmaker_controller_node_t a = setup_group();
    watch();
    parse(a, k_config);
    deliver();
    TEST_ASSERT_EQUAL_size_t(0, s_num_lists);
    teardown_group();
}

void test_controller_changes_release_config_reports_each_device(void)
{
    esp_rmaker_controller_node_t a = setup_group();
    esp_rmaker_controller_device_t light = esp_rmaker_controller_node_device_by_id(a, "Light");
    esp_rmaker_controller_device_t fan = esp_rmaker_controller_node_device_by_id(a, "Fan");
    watch();

    esp_rmaker_controller_node_release_config(a);
    deliver();
    TEST_ASSERT_EQUAL_size_t(1, s_num_lists);
    TEST_ASSERT_EQUAL_size_t(2, s_lists[0].num);
    assert_entry(&s_lists[0].items[0], ESP_RMAKER_CONTROLLER_REMOVED, ESP_RMAKER_CONTROLLER_KIND_DEVICE, light, a);
    assert_entry(&s_lists[0].items[1], ESP_RMAKER_CONTROLLER_REMOVED, ESP_RMAKER_CONTROLLER_KIND_DEVICE, fan, a);
    teardown_group();
}

void test_controller_changes_deinit_reports_each_node_and_subgroup(void)
{
    esp_rmaker_controller_node_t a = setup_group();
    esp_rmaker_controller_node_t b = esp_rmaker_controller_node_find("nodeB");
    esp_rmaker_controller_subgroup_t sg = esp_rmaker_controller_subgroup_first();
    watch();

    /* disable() runs this after the controller task parks, and it delivers on the calling task. */
    s_num_lists = 0;
    controller_model_deinit();
    TEST_ASSERT_EQUAL_size_t(1, s_num_lists);
    TEST_ASSERT_EQUAL_size_t(3, s_lists[0].num);
    assert_entry(&s_lists[0].items[0], ESP_RMAKER_CONTROLLER_REMOVED, ESP_RMAKER_CONTROLLER_KIND_NODE, a, 0);
    assert_entry(&s_lists[0].items[1], ESP_RMAKER_CONTROLLER_REMOVED, ESP_RMAKER_CONTROLLER_KIND_NODE, b, 0);
    assert_entry(&s_lists[0].items[2], ESP_RMAKER_CONTROLLER_REMOVED, ESP_RMAKER_CONTROLLER_KIND_SUBGROUP, sg, 0);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    teardown_group();
}

void test_controller_changes_lists_arrive_in_order_and_old_ones_are_dropped(void)
{
    esp_rmaker_controller_node_t a = setup_group();
    watch();

    /* Two rebuilds, delivered in the order they ran. */
    esp_rmaker_controller_node_release_config(a);
    sync_inventory(fresh_node("nodeB", NULL), NULL);
    deliver();
    TEST_ASSERT_EQUAL_size_t(2, s_num_lists);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_CONTROLLER_KIND_DEVICE, s_lists[0].items[0].kind);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_CONTROLLER_KIND_NODE, s_lists[1].items[0].kind);

    /* A list collected before a new registration is not delivered; the replay covers it. */
    sync_inventory(link_nodes(fresh_node("nodeB", NULL), fresh_node("nodeC", NULL)), NULL);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_watch_changes(record, NULL));
    deliver();
    TEST_ASSERT_EQUAL_size_t(1, s_num_lists);
    TEST_ASSERT_EQUAL_size_t(2, s_lists[0].num);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_CONTROLLER_ADDED, s_lists[0].items[0].type);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_CONTROLLER_ADDED, s_lists[0].items[1].type);
    teardown_group();
}

void test_controller_changes_none_are_collected_without_a_callback(void)
{
    esp_rmaker_controller_node_t a = setup_group();
    ctrl_changes_t changes;
    TEST_ASSERT_NULL(controller_changes_begin(&changes));

    esp_rmaker_controller_node_release_config(a);
    sync_inventory(fresh_node("nodeB", NULL), NULL);
    deliver();
    TEST_ASSERT_EQUAL_size_t(0, s_num_lists);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG, esp_rmaker_controller_watch_changes(NULL, NULL));
    teardown_group();
}
