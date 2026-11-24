/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_controller_lifecycle.c
 * @brief Tests for the controller lifecycle through the public API, against a fake cloud.
 *
 * The real controller task runs. A REST hook and the MQTT vtable stand in for the cloud, and the
 * real subscription manager routes each inbound message to the controller.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "osal_mqtt_events.h"
#include "osal_mqtt_subscription_manager.h"

#include "esp_rmaker_aws_sign_port_osal.h"
#include "esp_rmaker_core.h"
#include "esp_rmaker_mqtt_impl.h"
#include "local_config.h"

#include "controller_hooks.h"
#include "controller_internal.h"

#define FAKE_DOC_MAX     768
#define FAKE_PAYLOAD_MAX 1024

static const char *k_switch_config =
    "{\"devices\":[{\"id\":\"Switch\",\"type\":\"esp.device.switch\","
    "\"params\":[{\"id\":\"Power\",\"type\":\"esp.param.power\",\"data_type\":\"bool\","
    "\"properties\":[\"read\",\"write\"]}]}]}";

static const char *k_light_config =
    "{\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\","
    "\"params\":[{\"id\":\"Power\",\"type\":\"esp.param.power\",\"data_type\":\"bool\","
    "\"properties\":[\"read\",\"write\"]},"
    "{\"id\":\"Brightness\",\"type\":\"esp.param.brightness\",\"data_type\":\"int\","
    "\"properties\":[\"read\",\"write\"],\"bounds\":{\"min\":0,\"max\":100}},"
    "{\"id\":\"Serial\",\"type\":\"esp.param.serial\",\"data_type\":\"string\","
    "\"properties\":[\"read\"]}]}]}";

typedef struct {
    const char *id;
    const char *config;             /**< NULL answers 404. */
    char shadow[FAKE_DOC_MAX];      /**< The document a shadow get returns. */
} fake_node_t;

typedef struct {
    const char *id;
    char body[FAKE_DOC_MAX];
} fake_group_t;

/* The cloud: two groups and three nodes, plus what the controller sent to it. */
static struct {
    fake_group_t groups[2];
    fake_node_t nodes[3];
    int group_gets;
    int config_gets;
    int publishes;
    int subscribes;
    int unsubscribes;
    char last_topic[CONTROLLER_TOPIC_MAX_LEN];
    char last_payload[FAKE_PAYLOAD_MAX];
} s_cloud;

/* What the controller reported to the application, read after a barrier. */
static struct {
    int param_calls;
    esp_rmaker_controller_param_t last_param;
    int node_calls;
    esp_rmaker_controller_node_t last_node;
    int changes[3][4];  /**< Entries by change type, then by kind. */
} s_seen;

static esp_rmaker_node_t *s_node;
static char *s_self;
static osal_mqtt_impl_t s_saved_impl;
static bool s_was_connected;

/* Fake cloud ******************************************************************/

static void copy_n(char *dst, size_t len, const char *src, size_t src_len)
{
    size_t n = src_len < len - 1 ? src_len : len - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static fake_node_t *fake_node(const char *id, size_t id_len)
{
    for (size_t i = 0; i < sizeof(s_cloud.nodes) / sizeof(s_cloud.nodes[0]); i++) {
        const char *nid = s_cloud.nodes[i].id;
        if (strlen(nid) == id_len && strncmp(nid, id, id_len) == 0) {
            return &s_cloud.nodes[i];
        }
    }
    return NULL;
}

static esp_rmaker_error_t fake_rest_get(const char *path, esp_rmaker_aws_sign_response_t *resp)
{
    const char *body = NULL;
    const char *nodes = strstr(path, "/nodes/");
    if (nodes) {
        const char *id = nodes + strlen("/nodes/");
        const char *end = strstr(id, "/config");
        fake_node_t *node = end ? fake_node(id, (size_t)(end - id)) : NULL;
        body = node ? node->config : NULL;
        s_cloud.config_gets++;
    } else {
        for (size_t i = 0; i < sizeof(s_cloud.groups) / sizeof(s_cloud.groups[0]); i++) {
            char want[64];
            snprintf(want, sizeof(want), "/v1/groups/%s", s_cloud.groups[i].id);
            if (strcmp(path, want) == 0) {
                body = s_cloud.groups[i].body;
            }
        }
        s_cloud.group_gets++;
    }
    memset(resp, 0, sizeof(*resp));
    resp->status_code = body ? 200 : 404;
    if (body) {
        resp->body_len = strlen(body);
        resp->body = malloc(resp->body_len + 1);
        if (!resp->body) {
            return ESP_RMAKER_NO_MEM;
        }
        memcpy(resp->body, body, resp->body_len + 1);
    }
    return ESP_RMAKER_OK;
}

/* A shadow get is answered at once on get/accepted, so a refresh needs no broker. */
static osal_err_t fake_publish(osal_mqtt_event_loop_channel_t *channel, const char *topic, size_t topic_len,
                               void *data, size_t data_len, osal_mqtt_QoS_t qos, bool retain)
{
    (void)channel;
    (void)qos;
    (void)retain;
    s_cloud.publishes++;
    copy_n(s_cloud.last_topic, sizeof(s_cloud.last_topic), topic, topic_len);
    copy_n(s_cloud.last_payload, sizeof(s_cloud.last_payload), (const char *)data, data_len);

    if (topic_len < 4 || memcmp(topic + topic_len - 4, "/get", 4) != 0) {
        return OSAL_ERR_OK;
    }
    const char *thing = NULL;
    size_t thing_len = 0;
    if (!esp_rmaker_mqtt_topic_parse_thing_name(topic, topic_len, &thing, &thing_len)) {
        return OSAL_ERR_OK;
    }
    fake_node_t *node = fake_node(thing, thing_len);
    if (node && node->shadow[0]) {
        char accepted[CONTROLLER_TOPIC_MAX_LEN + sizeof("/accepted")];
        snprintf(accepted, sizeof(accepted), "%s/accepted", s_cloud.last_topic);
        osal_mqtt_subscription_handle_publish(accepted, strlen(accepted), node->shadow, strlen(node->shadow));
    }
    return OSAL_ERR_OK;
}

static osal_err_t fake_subscribe(osal_mqtt_event_loop_channel_t *channel, const char *topic, size_t topic_len,
                                 osal_mqtt_subscribe_cb_t cb, osal_mqtt_QoS_t qos, void *priv_data)
{
    s_cloud.subscribes++;
    return osal_mqtt_subscription_add(topic, (uint16_t)topic_len, channel, cb, qos, priv_data)
           ? OSAL_ERR_OK : OSAL_ERR_NO_MEM;
}

static osal_err_t fake_unsubscribe(osal_mqtt_event_loop_channel_t *channel, const char *topic, size_t topic_len,
                                   osal_mqtt_QoS_t qos)
{
    (void)channel;
    (void)qos;
    s_cloud.unsubscribes++;
    osal_mqtt_subscription_remove(topic, (uint16_t)topic_len);
    return OSAL_ERR_OK;
}

static void set_group(int i, const char *id, const char *name, const char *member, const char *subgroups)
{
    s_cloud.groups[i].id = id;
    snprintf(s_cloud.groups[i].body, sizeof(s_cloud.groups[i].body),
             "{\"group_id\":\"%s\",\"group_name\":\"%s\",\"node_ids\":[\"%s\",\"nodeA\",\"%s\"]%s}",
             id, name, s_self, member, subgroups);
}

static void set_node(int i, const char *id, const char *config, const char *shadow)
{
    s_cloud.nodes[i].id = id;
    s_cloud.nodes[i].config = config;
    snprintf(s_cloud.nodes[i].shadow, sizeof(s_cloud.nodes[i].shadow), "%s", shadow);
}

static void reset_counters(void)
{
    s_cloud.group_gets = 0;
    s_cloud.config_gets = 0;
    s_cloud.publishes = 0;
    s_cloud.subscribes = 0;
    s_cloud.unsubscribes = 0;
    memset(&s_seen, 0, sizeof(s_seen));
}

/* Recorders, on the controller task ******************************************/

static void on_param(esp_rmaker_controller_param_t param, void *priv)
{
    (void)priv;
    s_seen.param_calls++;
    s_seen.last_param = param;
}

static void on_node(esp_rmaker_controller_node_t node, void *priv)
{
    (void)priv;
    s_seen.node_calls++;
    s_seen.last_node = node;
}

static void on_changes(const esp_rmaker_controller_change_t *changes, size_t count, void *priv)
{
    (void)priv;
    for (size_t i = 0; i < count; i++) {
        s_seen.changes[changes[i].type][changes[i].kind]++;
    }
}

/* Helpers *********************************************************************/

static esp_rmaker_error_t noop_job(void *arg)
{
    (void)arg;
    return ESP_RMAKER_OK;
}

/* Wait for every job queued so far, and for any job those jobs queued. */
static void barrier(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_task_run_sync(noop_job, NULL));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_task_run_sync(noop_job, NULL));
}

static esp_rmaker_controller_node_t node(const char *id)
{
    esp_rmaker_controller_node_t h = esp_rmaker_controller_node_find(id);
    TEST_ASSERT_NOT_EQUAL(0, h);
    return h;
}

static esp_rmaker_controller_param_t param(const char *node_id, const char *dev_id, const char *param_id)
{
    esp_rmaker_controller_device_t dev = esp_rmaker_controller_node_device_by_id(node(node_id), dev_id);
    TEST_ASSERT_NOT_EQUAL(0, dev);
    esp_rmaker_controller_param_t h = esp_rmaker_controller_device_param_by_id(dev, param_id);
    TEST_ASSERT_NOT_EQUAL(0, h);
    return h;
}

static int int_val(esp_rmaker_controller_param_t h)
{
    esp_rmaker_param_val_t val = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_param_get_val(h, &val));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_INTEGER, val.type);
    return val.val.i;
}

static bool bool_val(esp_rmaker_controller_param_t h)
{
    esp_rmaker_param_val_t val = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_param_get_val(h, &val));
    TEST_ASSERT_EQUAL_INT(RMAKER_VAL_TYPE_BOOLEAN, val.type);
    return val.val.b;
}

static void sync_all(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_group_sync());
    for (esp_rmaker_controller_node_t n = esp_rmaker_controller_node_first(); n;
            n = esp_rmaker_controller_node_next(n)) {
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_node_sync(n));
    }
}

/* The topic one node's shadow takes, with @p suffix in place of "get". */
static void shadow_topic(const char *node_id, const char *suffix, char *out, size_t len)
{
    esp_rmaker_topic_ctx_t ctx;
    controller_node_topic_ctx(controller_node_from(node(node_id)), &ctx);
    char get[CONTROLLER_TOPIC_MAX_LEN];
    TEST_ASSERT_TRUE(esp_rmaker_mqtt_topic_params_named_shadow_get(&ctx, get, sizeof(get)) > 3);
    snprintf(out, len, "%.*s%s", (int)(strlen(get) - 3), get, suffix);
}

/* Deliver @p doc on the update/accepted topic of @p node_id, then wait for the jobs it queues. */
static void push_update(const char *node_id, const char *doc)
{
    char topic[CONTROLLER_TOPIC_MAX_LEN];
    char payload[FAKE_DOC_MAX];
    shadow_topic(node_id, "update/accepted", topic, sizeof(topic));
    snprintf(payload, sizeof(payload), "%s", doc);
    TEST_ASSERT_TRUE(osal_mqtt_subscription_handle_publish(topic, strlen(topic), payload, strlen(payload)));
    barrier();
}

/* Setup and teardown **********************************************************/

/* No asserts here: a failed setup is checked inside the test's TEST_PROTECT. */
static bool lifecycle_setup(void)
{
    esp_rmaker_config_t config = { .enable_time_sync = false };
    s_node = esp_rmaker_node_init(&config, "test_node", "test_type");
    s_self = esp_rmaker_get_node_id();
    if (!s_node || !s_self) {
        return false;
    }
    memset(&s_cloud, 0, sizeof(s_cloud));
    memset(&s_seen, 0, sizeof(s_seen));
    set_group(0, "grp1", "Home", "nodeB",
              ",\"subgroups\":[{\"subgroup_id\":\"sgA\",\"subgroup_name\":\"Kitchen\",\"node_ids\":[\"nodeB\"]}]");
    set_group(1, "grp2", "Office", "nodeC", "");
    set_node(0, "nodeA", k_light_config,
             "{\"state\":{\"reported\":{\"online\":true,\"ncfg_ver\":\"a1\","
             "\"params\":{\"Light\":{\"Power\":true,\"Brightness\":42,\"Serial\":\"X1\"}}}}}");
    set_node(1, "nodeB", k_switch_config,
             "{\"state\":{\"reported\":{\"online\":false,\"ncfg_ver\":\"b1\","
             "\"params\":{\"Switch\":{\"Power\":false}}}}}");
    set_node(2, "nodeC", k_light_config,
             "{\"state\":{\"reported\":{\"online\":true,\"ncfg_ver\":\"c1\","
             "\"params\":{\"Light\":{\"Power\":false,\"Brightness\":7,\"Serial\":\"X3\"}}}}}");

    osal_mqtt_subscription_init();
    s_saved_impl = esp_rmaker_mqtt_impl;
    esp_rmaker_mqtt_impl.publish = fake_publish;
    esp_rmaker_mqtt_impl.subscribe = fake_subscribe;
    esp_rmaker_mqtt_impl.unsubscribe = fake_unsubscribe;
    s_was_connected = osal_mqtt_event_get_bits(OSAL_MQTT_CLIENT_CONNECTED_BIT) != 0;
    osal_mqtt_event_set_bits(OSAL_MQTT_CLIENT_CONNECTED_BIT);
    /* The response free goes through the port, so a fake body needs one installed. */
    esp_rmaker_aws_sign_port_install(esp_rmaker_aws_sign_port_osal_get());
    controller_test_set_rest_get(fake_rest_get);
    esp_rmaker_local_config_set_group_info_str("grp1");
    return esp_rmaker_controller_enable() == ESP_RMAKER_OK;
}

/* No asserts here: see the Unity cleanup rule in test_migration.c. */
static void lifecycle_teardown(void)
{
    esp_rmaker_controller_unwatch_changes();
    esp_rmaker_controller_unwatch_params();
    esp_rmaker_controller_unwatch_nodes();
    esp_rmaker_controller_set_active(true);
    esp_rmaker_controller_disable();
    controller_test_set_rest_get(NULL);
    if (!s_was_connected) {
        osal_mqtt_event_clear_bits(OSAL_MQTT_CLIENT_CONNECTED_BIT);
    }
    esp_rmaker_mqtt_impl = s_saved_impl;
    osal_mqtt_subscription_deinit();
    if (s_node) {
        esp_rmaker_local_config_set_group_info_str("");
        esp_rmaker_node_deinit(s_node);
        s_node = NULL;
    }
    free(s_self);
    s_self = NULL;
}

#define LIFECYCLE_TEST(...)                            \
    do {                                               \
        bool ready = lifecycle_setup();                \
        if (TEST_PROTECT()) {                          \
            TEST_ASSERT_TRUE_MESSAGE(ready, "setup");  \
            __VA_ARGS__                                \
        }                                              \
        lifecycle_teardown();                          \
    } while (0)

/* Tests ***********************************************************************/

void test_controller_lifecycle_sync_builds_the_group_with_values(void)
{
    LIFECYCLE_TEST({
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_group_sync());
        TEST_ASSERT_EQUAL_INT(1, s_cloud.group_gets);
        /* Two shadow names, each observed on get/accepted and update/accepted. */
        TEST_ASSERT_EQUAL_INT(4, s_cloud.subscribes);
        TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_find(s_self));

        char *name = NULL;
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_get_group_name(&name));
        TEST_ASSERT_EQUAL_STRING("Home", name);
        free(name);
        TEST_ASSERT_TRUE(esp_rmaker_controller_node_in_subgroup(node("nodeB"), "sgA"));
        TEST_ASSERT_FALSE(esp_rmaker_controller_node_in_subgroup(node("nodeA"), "sgA"));
        /* Membership only: no data model is read yet. */
        TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_device_first(node("nodeA")));

        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_node_sync(node("nodeA")));
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_node_sync(node("nodeB")));
        TEST_ASSERT_EQUAL_INT(2, s_cloud.config_gets);
        TEST_ASSERT_TRUE(bool_val(param("nodeA", "Light", "Power")));
        TEST_ASSERT_EQUAL_INT(42, int_val(param("nodeA", "Light", "Brightness")));
        TEST_ASSERT_FALSE(bool_val(param("nodeB", "Switch", "Power")));
        TEST_ASSERT_TRUE(esp_rmaker_controller_node_is_online(node("nodeA")));
        TEST_ASSERT_FALSE(esp_rmaker_controller_node_is_online(node("nodeB")));
        TEST_ASSERT_TRUE(esp_rmaker_controller_node_config_is_current(node("nodeA")));
    });
}

void test_controller_lifecycle_resync_of_an_unchanged_group_fetches_no_config(void)
{
    LIFECYCLE_TEST({
        sync_all();
        esp_rmaker_controller_node_t a = node("nodeA");
        esp_rmaker_controller_param_t brightness = param("nodeA", "Light", "Brightness");
        reset_counters();

        sync_all();
        TEST_ASSERT_EQUAL_INT(0, s_cloud.config_gets);
        TEST_ASSERT_EQUAL_INT(0, s_cloud.subscribes);
        TEST_ASSERT_EQUAL_INT(2, s_cloud.publishes);
        TEST_ASSERT_EQUAL(a, node("nodeA"));
        TEST_ASSERT_EQUAL(brightness, param("nodeA", "Light", "Brightness"));
    });
}

void test_controller_lifecycle_delta_updates_one_value_and_notifies_it(void)
{
    LIFECYCLE_TEST({
        sync_all();
        esp_rmaker_controller_watch_params(on_param, NULL);
        push_update("nodeA", "{\"state\":{\"reported\":{\"params\":{\"Light\":{\"Brightness\":77}}}}}");

        TEST_ASSERT_EQUAL_INT(1, s_seen.param_calls);
        TEST_ASSERT_EQUAL(param("nodeA", "Light", "Brightness"), s_seen.last_param);
        TEST_ASSERT_EQUAL_INT(77, int_val(param("nodeA", "Light", "Brightness")));
        TEST_ASSERT_TRUE(bool_val(param("nodeA", "Light", "Power")));
    });
}

void test_controller_lifecycle_online_change_notifies_the_node(void)
{
    LIFECYCLE_TEST({
        sync_all();
        esp_rmaker_controller_watch_nodes(on_node, NULL);
        push_update("nodeB", "{\"state\":{\"reported\":{\"online\":true}}}");

        TEST_ASSERT_EQUAL_INT(1, s_seen.node_calls);
        TEST_ASSERT_EQUAL(node("nodeB"), s_seen.last_node);
        TEST_ASSERT_TRUE(esp_rmaker_controller_node_is_online(node("nodeB")));
    });
}

void test_controller_lifecycle_config_change_refetches_and_seeds_the_new_params(void)
{
    LIFECYCLE_TEST({
        sync_all();
        esp_rmaker_controller_param_t old_power = param("nodeB", "Switch", "Power");
        esp_rmaker_controller_watch_changes(on_changes, NULL);
        barrier();
        reset_counters();

        /* The node turns from a switch into a light. */
        set_node(1, "nodeB", k_light_config,
                 "{\"state\":{\"reported\":{\"online\":true,\"ncfg_ver\":\"b2\","
                 "\"params\":{\"Light\":{\"Power\":true,\"Brightness\":55,\"Serial\":\"X2\"}}}}}");
        push_update("nodeB", "{\"state\":{\"reported\":{\"ncfg_ver\":\"b2\"}}}");

        TEST_ASSERT_EQUAL_INT(1, s_cloud.config_gets);
        TEST_ASSERT_EQUAL_INT(55, int_val(param("nodeB", "Light", "Brightness")));
        TEST_ASSERT_TRUE(bool_val(param("nodeB", "Light", "Power")));
        TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_device_by_id(node("nodeB"), "Switch"));
        esp_rmaker_param_val_t val = {0};
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_param_get_val(old_power, &val));
        TEST_ASSERT_EQUAL_INT(1, s_seen.changes[ESP_RMAKER_CONTROLLER_REMOVED][ESP_RMAKER_CONTROLLER_KIND_DEVICE]);
        TEST_ASSERT_EQUAL_INT(1, s_seen.changes[ESP_RMAKER_CONTROLLER_ADDED][ESP_RMAKER_CONTROLLER_KIND_DEVICE]);
        TEST_ASSERT_EQUAL_INT(3, s_seen.changes[ESP_RMAKER_CONTROLLER_ADDED][ESP_RMAKER_CONTROLLER_KIND_PARAM]);
        TEST_ASSERT_TRUE(esp_rmaker_controller_node_config_is_current(node("nodeB")));
    });
}

void test_controller_lifecycle_failed_refetch_keeps_the_config_stale(void)
{
    LIFECYCLE_TEST({
        sync_all();
        s_cloud.nodes[1].config = NULL;
        push_update("nodeB", "{\"state\":{\"reported\":{\"ncfg_ver\":\"b2\"}}}");
        TEST_ASSERT_FALSE(esp_rmaker_controller_node_config_is_current(node("nodeB")));

        /* The next sync retries the fetch, and seeds the values with it. */
        set_node(1, "nodeB", k_light_config,
                 "{\"state\":{\"reported\":{\"online\":true,\"ncfg_ver\":\"b2\","
                 "\"params\":{\"Light\":{\"Power\":true,\"Brightness\":55,\"Serial\":\"X2\"}}}}}");
        reset_counters();
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_node_sync(node("nodeB")));
        TEST_ASSERT_EQUAL_INT(1, s_cloud.config_gets);
        TEST_ASSERT_EQUAL_INT(55, int_val(param("nodeB", "Light", "Brightness")));
        TEST_ASSERT_TRUE(esp_rmaker_controller_node_config_is_current(node("nodeB")));
    });
}

void test_controller_lifecycle_write_publishes_by_id_and_refuses_what_it_must(void)
{
    LIFECYCLE_TEST({
        sync_all();
        reset_counters();
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                              esp_rmaker_controller_param_write(param("nodeA", "Light", "Power"), esp_rmaker_bool(false)));
        TEST_ASSERT_EQUAL_INT(1, s_cloud.publishes);
        char topic[CONTROLLER_TOPIC_MAX_LEN];
        esp_rmaker_topic_ctx_t ctx;
        controller_node_topic_ctx(controller_node_from(node("nodeA")), &ctx);
        TEST_ASSERT_TRUE(esp_rmaker_mqtt_topic_params_to_node_for_node(&ctx, topic, sizeof(topic)) > 0);
        TEST_ASSERT_EQUAL_STRING(topic, s_cloud.last_topic);
        TEST_ASSERT_EQUAL_STRING("{\"Light\":{\"Power\":false}}", s_cloud.last_payload);

        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG,
                              esp_rmaker_controller_param_write(param("nodeA", "Light", "Serial"), esp_rmaker_str("X9")));
        esp_rmaker_controller_set_active(false);
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_STATE,
                              esp_rmaker_controller_param_write(param("nodeA", "Light", "Power"), esp_rmaker_bool(true)));
        TEST_ASSERT_EQUAL_INT(1, s_cloud.publishes);
    });
}

void test_controller_lifecycle_group_write_publishes_by_type(void)
{
    LIFECYCLE_TEST({
        sync_all();
        char topic[CONTROLLER_TOPIC_MAX_LEN];
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_group_write(NULL, "esp.device.lightbulb",
                              "esp.param.power", esp_rmaker_bool(true)));
        TEST_ASSERT_TRUE(esp_rmaker_mqtt_topic_group_control_broadcast(topic, sizeof(topic), "grp1") > 0);
        TEST_ASSERT_EQUAL_STRING(topic, s_cloud.last_topic);
        TEST_ASSERT_EQUAL_STRING("{\"esp.device.lightbulb\":{\"params\":{\"esp.param.power\":true}}}",
                                 s_cloud.last_payload);

        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_group_write("sgA", "esp.device.switch",
                              "esp.param.power", esp_rmaker_bool(false)));
        TEST_ASSERT_TRUE(esp_rmaker_mqtt_topic_group_control_subgroup(topic, sizeof(topic), "grp1", "sgA") > 0);
        TEST_ASSERT_EQUAL_STRING(topic, s_cloud.last_topic);
    });
}

void test_controller_lifecycle_migration_rebuilds_the_group_and_its_filters(void)
{
    LIFECYCLE_TEST({
        sync_all();
        esp_rmaker_controller_node_t a = node("nodeA");
        esp_rmaker_controller_node_t b = node("nodeB");
        esp_rmaker_controller_watch_changes(on_changes, NULL);
        barrier();
        reset_counters();

        esp_rmaker_local_config_set_group_info_str("grp2");
        controller_group_notify_group_info(true);
        barrier();

        TEST_ASSERT_EQUAL_INT(1, s_cloud.group_gets);
        TEST_ASSERT_EQUAL_INT(4, s_cloud.unsubscribes);
        TEST_ASSERT_EQUAL_INT(2, s_cloud.subscribes);
        char *name = NULL;
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_get_group_name(&name));
        TEST_ASSERT_EQUAL_STRING("Office", name);
        free(name);

        TEST_ASSERT_EQUAL(a, node("nodeA"));
        TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_find("nodeB"));
        char *id = NULL;
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_node_get_id(b, &id));
        TEST_ASSERT_EQUAL_INT(1, s_seen.changes[ESP_RMAKER_CONTROLLER_REMOVED][ESP_RMAKER_CONTROLLER_KIND_NODE]);
        TEST_ASSERT_EQUAL_INT(1, s_seen.changes[ESP_RMAKER_CONTROLLER_ADDED][ESP_RMAKER_CONTROLLER_KIND_NODE]);
        TEST_ASSERT_EQUAL_INT(1, s_seen.changes[ESP_RMAKER_CONTROLLER_REMOVED][ESP_RMAKER_CONTROLLER_KIND_SUBGROUP]);

        /* The new node has no data model until the application syncs it. */
        TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_device_first(node("nodeC")));
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_node_sync(node("nodeC")));
        TEST_ASSERT_EQUAL_INT(7, int_val(param("nodeC", "Light", "Brightness")));
    });
}

void test_controller_lifecycle_leaving_the_group_drops_the_inventory(void)
{
    LIFECYCLE_TEST({
        sync_all();
        reset_counters();
        esp_rmaker_local_config_set_group_info_str("");
        controller_group_notify_group_info(true);
        barrier();

        TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_first());
        TEST_ASSERT_EQUAL_INT(4, s_cloud.unsubscribes);
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_group_sync());
    });
}

void test_controller_lifecycle_mqtt_down_fails_cleanly(void)
{
    LIFECYCLE_TEST({
        sync_all();
        reset_counters();
        osal_mqtt_event_clear_bits(OSAL_MQTT_CLIENT_CONNECTED_BIT);
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_STATE, esp_rmaker_controller_node_sync(node("nodeA")));
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_STATE,
                              esp_rmaker_controller_param_write(param("nodeA", "Light", "Power"), esp_rmaker_bool(false)));
        TEST_ASSERT_EQUAL_INT(0, s_cloud.publishes);
        osal_mqtt_event_set_bits(OSAL_MQTT_CLIENT_CONNECTED_BIT);
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_node_sync(node("nodeA")));
    });
}

void test_controller_lifecycle_missing_config_reports_not_found(void)
{
    LIFECYCLE_TEST({
        s_cloud.nodes[0].config = NULL;
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_group_sync());
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_node_sync(node("nodeA")));
        TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_device_first(node("nodeA")));
    });
}

static esp_rmaker_error_t copy_body(void *user_ctx, const char *data, size_t len)
{
    copy_n((char *)user_ctx, FAKE_DOC_MAX, data, len);
    return ESP_RMAKER_OK;
}

void test_controller_lifecycle_api_get_returns_the_body(void)
{
    LIFECYCLE_TEST({
        char body[FAKE_DOC_MAX] = {0};
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_api_get("/v1/groups/grp1", copy_body, body));
        TEST_ASSERT_EQUAL_STRING(s_cloud.groups[0].body, body);
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, esp_rmaker_controller_api_get("/v1/groups/nope", NULL, NULL));
    });
}

void test_controller_lifecycle_disable_drops_the_shadow_filters(void)
{
    LIFECYCLE_TEST({
        sync_all();
        char topic[CONTROLLER_TOPIC_MAX_LEN];
        shadow_topic("nodeA", "update/accepted", topic, sizeof(topic));
        reset_counters();

        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_disable());
        TEST_ASSERT_EQUAL_INT(4, s_cloud.unsubscribes);
        const char *doc = "{\"state\":{\"reported\":{\"online\":false}}}";
        TEST_ASSERT_FALSE(osal_mqtt_subscription_handle_publish(topic, strlen(topic), (void *)doc, strlen(doc)));
    });
}

static esp_rmaker_error_t s_disable_result;

static void disable_from_callback(esp_rmaker_controller_node_t node, void *priv)
{
    (void)node;
    (void)priv;
    s_disable_result = esp_rmaker_controller_disable();
}

void test_controller_lifecycle_disable_from_a_callback_is_refused(void)
{
    LIFECYCLE_TEST({
        sync_all();
        s_disable_result = ESP_RMAKER_OK;
        esp_rmaker_controller_watch_nodes(disable_from_callback, NULL);
        push_update("nodeB", "{\"state\":{\"reported\":{\"online\":true}}}");

        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_STATE, s_disable_result);
        TEST_ASSERT_NOT_EQUAL(0, esp_rmaker_controller_node_find("nodeA"));
    });
}

void test_controller_lifecycle_disable_drops_an_open_group_batch(void)
{
    LIFECYCLE_TEST({
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_group_write_begin(NULL));
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                              esp_rmaker_controller_group_write_add("esp.device.lightbulb", "esp.param.power",
                                      esp_rmaker_bool(true)));
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_disable());
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_INITIALIZED, esp_rmaker_controller_group_write_begin(NULL));

        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_enable());
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_group_write_begin(NULL));
        esp_rmaker_controller_group_write_abort();
    });
}

void test_controller_lifecycle_blocking_call_after_disable_is_not_initialized(void)
{
    LIFECYCLE_TEST({
        sync_all();
        esp_rmaker_controller_node_t a = node("nodeA");
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_disable());
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_INITIALIZED, esp_rmaker_controller_group_sync());
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_INITIALIZED, esp_rmaker_controller_node_refresh_params(a));
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_enable());
    });
}

void test_controller_lifecycle_disable_makes_every_handle_stale(void)
{
    LIFECYCLE_TEST({
        sync_all();
        esp_rmaker_controller_node_t a = node("nodeA");
        esp_rmaker_controller_param_t brightness = param("nodeA", "Light", "Brightness");
        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_disable());
        TEST_ASSERT_EQUAL(0, esp_rmaker_controller_node_first());

        TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_enable());
        sync_all();
        esp_rmaker_param_val_t val = {0};
        char *id = NULL;
        TEST_ASSERT_NOT_EQUAL(ESP_RMAKER_OK, esp_rmaker_controller_node_get_id(a, &id));
        TEST_ASSERT_NOT_EQUAL(ESP_RMAKER_OK, esp_rmaker_controller_param_get_val(brightness, &val));
        TEST_ASSERT_EQUAL_INT(42, int_val(param("nodeA", "Light", "Brightness")));
    });
}
