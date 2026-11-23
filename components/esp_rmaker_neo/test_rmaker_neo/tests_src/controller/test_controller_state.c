/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_controller_state.c
 * @brief Tests for applying a shadow reported state onto the cached param values.
 *
 * A node publishes only the params that changed, so a delta must merge rather than replace. A
 * parser that cleared what a message omits would flip every other param back to its default on
 * every push.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "controller_internal.h"

static const char *k_config =
    "{\"devices\":[{\"id\":\"Light\",\"type\":\"esp.device.lightbulb\","
    "\"params\":[{\"id\":\"Power\",\"type\":\"esp.param.power\",\"data_type\":\"bool\","
    "\"properties\":[\"read\",\"write\"]},"
    "{\"id\":\"Brightness\",\"type\":\"esp.param.brightness\",\"data_type\":\"int\","
    "\"properties\":[\"read\",\"write\"]},"
    "{\"id\":\"Name\",\"type\":\"esp.param.name\",\"data_type\":\"string\","
    "\"properties\":[\"read\",\"write\"]}]}]}";

static ctrl_node_t *make_node(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node9");
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_node_config(node, k_config, strlen(k_config)));
    return node;
}

static esp_rmaker_controller_param_t find(ctrl_node_t *node, const char *param_type)
{
    return esp_rmaker_controller_node_find_param(CONTROLLER_HANDLE(node),
            "esp.device.lightbulb", param_type);
}

static esp_rmaker_error_t apply(ctrl_node_t *node, const char *body, size_t *num, bool *moved)
{
    controller_state_delta_t delta = {0};
    esp_rmaker_error_t err = controller_parse_reported_state(node, body, strlen(body), &delta);
    if (num) {
        *num = delta.num_changed;
    }
    if (moved) {
        *moved = delta.ncfg_ver_moved;
    }
    return err;
}

static bool apply_online(ctrl_node_t *node, const char *body)
{
    controller_state_delta_t delta = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK,
                          controller_parse_reported_state(node, body, strlen(body), &delta));
    return delta.online_moved;
}

static void collect(void *ctx, ctrl_param_t *param)
{
    ctrl_param_t ***next = (ctrl_param_t ***)ctx;
    *(*next)++ = param;
}

void test_controller_state_applies_reported_params(void)
{
    ctrl_node_t *node = make_node();
    const char *body =
        "{\"state\":{\"reported\":{\"online\":true,\"params\":{\"Light\":"
        "{\"Power\":true,\"Brightness\":40,\"Name\":\"Lamp\"}}}},\"version\":7}";
    size_t num = 0;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, apply(node, body, &num, NULL));
    TEST_ASSERT_EQUAL_size_t(3, num);
    TEST_ASSERT_TRUE(node->online);

    esp_rmaker_param_val_t val = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_param_get_val(find(node, "esp.param.power"), &val));
    TEST_ASSERT_TRUE(val.val.b);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_param_get_val(find(node, "esp.param.brightness"), &val));
    TEST_ASSERT_EQUAL_INT(40, val.val.i);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_param_get_val(find(node, "esp.param.name"), &val));
    TEST_ASSERT_EQUAL_STRING("Lamp", val.val.s);
    esp_rmaker_val_free(&val);
    controller_model_free_node_list(node);
}

void test_controller_state_reports_not_found_before_any_value(void)
{
    ctrl_node_t *node = make_node();
    esp_rmaker_param_val_t val = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND,
                          esp_rmaker_controller_param_get_val(find(node, "esp.param.power"), &val));
    controller_model_free_node_list(node);
}

void test_controller_state_delta_keeps_params_it_does_not_mention(void)
{
    ctrl_node_t *node = make_node();
    const char *full =
        "{\"state\":{\"reported\":{\"params\":{\"Light\":{\"Power\":true,\"Brightness\":40}}}}}";
    apply(node, full, NULL, NULL);

    /* update/accepted echoes only the changed param. */
    const char *delta = "{\"state\":{\"reported\":{\"params\":{\"Light\":{\"Brightness\":80}}}}}";
    size_t num = 0;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, apply(node, delta, &num, NULL));
    TEST_ASSERT_EQUAL_size_t(1, num);

    esp_rmaker_param_val_t val = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_param_get_val(find(node, "esp.param.brightness"), &val));
    TEST_ASSERT_EQUAL_INT(80, val.val.i);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, esp_rmaker_controller_param_get_val(find(node, "esp.param.power"), &val));
    TEST_ASSERT_TRUE(val.val.b);
    controller_model_free_node_list(node);
}

void test_controller_state_reports_no_change_when_the_value_is_the_same(void)
{
    ctrl_node_t *node = make_node();
    const char *body = "{\"state\":{\"reported\":{\"params\":{\"Light\":{\"Brightness\":40}}}}}";
    size_t num = 0;
    apply(node, body, &num, NULL);
    TEST_ASSERT_EQUAL_size_t(1, num);
    apply(node, body, &num, NULL);
    TEST_ASSERT_EQUAL_size_t(0, num);
    controller_model_free_node_list(node);
}

void test_controller_state_ignores_the_desired_plane(void)
{
    /* The same topic carries desired-state writes, which are a request rather than the node's
     * state. */
    ctrl_node_t *node = make_node();
    const char *body =
        "{\"state\":{\"desired\":{\"params\":{\"Light\":{\"Power\":true}}},"
        "\"reported\":{\"params\":{\"Light\":{\"Brightness\":10}}}}}";
    size_t num = 0;
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, apply(node, body, &num, NULL));
    TEST_ASSERT_EQUAL_size_t(1, num);

    esp_rmaker_param_val_t val = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND,
                          esp_rmaker_controller_param_get_val(find(node, "esp.param.power"), &val));
    controller_model_free_node_list(node);
}

void test_controller_state_rejects_a_body_without_reported(void)
{
    ctrl_node_t *node = make_node();
    const char *body = "{\"state\":{\"desired\":{}}}";
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND, apply(node, body, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_INVALID_ARG, apply(node, "not json", NULL, NULL));
    controller_model_free_node_list(node);
}

void test_controller_state_ncfg_ver_change_marks_the_config_stale(void)
{
    ctrl_node_t *node = make_node();
    /* The first token seen is a baseline, not a change. */
    const char *first = "{\"state\":{\"reported\":{\"ncfg_ver\":\"aaaa\"}}}";
    bool moved = true;
    apply(node, first, NULL, &moved);
    TEST_ASSERT_FALSE(moved);
    TEST_ASSERT_EQUAL_STRING("aaaa", node->ncfg_ver);

    apply(node, first, NULL, &moved);
    TEST_ASSERT_FALSE(moved);

    const char *second = "{\"state\":{\"reported\":{\"ncfg_ver\":\"bbbb\"}}}";
    apply(node, second, NULL, &moved);
    TEST_ASSERT_TRUE(moved);
    TEST_ASSERT_EQUAL_STRING("bbbb", node->ncfg_ver);
    controller_model_free_node_list(node);
}

void test_controller_state_config_is_current_until_ncfg_ver_moves(void)
{
    /* A shadow get reports the token too, so this is what lets a re-sync skip the config fetch. */
    ctrl_node_t *node = make_node();
    esp_rmaker_controller_node_t nh = CONTROLLER_HANDLE(node);
    TEST_ASSERT_TRUE(esp_rmaker_controller_node_config_is_current(nh));

    apply(node, "{\"state\":{\"reported\":{\"ncfg_ver\":\"aaaa\"}}}", NULL, NULL);
    TEST_ASSERT_TRUE(esp_rmaker_controller_node_config_is_current(nh));

    apply(node, "{\"state\":{\"reported\":{\"ncfg_ver\":\"bbbb\"}}}", NULL, NULL);
    TEST_ASSERT_FALSE(esp_rmaker_controller_node_config_is_current(nh));

    /* The re-fetch clears it. */
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_node_config(node, k_config, strlen(k_config)));
    TEST_ASSERT_TRUE(esp_rmaker_controller_node_config_is_current(nh));

    controller_model_release_config(node);
    TEST_ASSERT_FALSE(esp_rmaker_controller_node_config_is_current(nh));
    controller_model_free_node_list(node);
}

void test_controller_state_ncfg_ver_accepts_an_integer_token(void)
{
    /* The docs disagree on the type, so it is compared as an opaque token and never parsed. */
    ctrl_node_t *node = make_node();
    bool moved = true;
    apply(node, "{\"state\":{\"reported\":{\"ncfg_ver\":1700000000}}}", NULL, &moved);
    TEST_ASSERT_FALSE(moved);
    TEST_ASSERT_EQUAL_STRING("1700000000", node->ncfg_ver);
    apply(node, "{\"state\":{\"reported\":{\"ncfg_ver\":1700000001}}}", NULL, &moved);
    TEST_ASSERT_TRUE(moved);
    controller_model_free_node_list(node);
}

void test_controller_state_tracks_the_online_flag(void)
{
    ctrl_node_t *node = make_node();
    apply(node, "{\"state\":{\"reported\":{\"online\":true}}}", NULL, NULL);
    TEST_ASSERT_TRUE(esp_rmaker_controller_node_is_online(CONTROLLER_HANDLE(node)));
    apply(node, "{\"state\":{\"reported\":{\"online\":false}}}", NULL, NULL);
    TEST_ASSERT_FALSE(esp_rmaker_controller_node_is_online(CONTROLLER_HANDLE(node)));
    controller_model_free_node_list(node);
}

void test_controller_state_reports_an_online_transition(void)
{
    /* ``online`` is a shadow field rather than a param, so this out-param is a hub's only
     * notification that a node went away. */
    ctrl_node_t *node = make_node();
    const char *up = "{\"state\":{\"reported\":{\"online\":true}}}";
    TEST_ASSERT_TRUE(apply_online(node, up));

    /* The same flag repeated is not a transition. */
    TEST_ASSERT_FALSE(apply_online(node, up));

    const char *down = "{\"state\":{\"reported\":{\"online\":false}}}";
    TEST_ASSERT_TRUE(apply_online(node, down));

    /* A payload that does not mention the flag is not a transition either. */
    const char *silent = "{\"state\":{\"reported\":{\"params\":{\"Light\":{\"Brightness\":5}}}}}";
    TEST_ASSERT_FALSE(apply_online(node, silent));
    controller_model_free_node_list(node);
}

/** Append to @p buf, asserting the whole of @p fmt fit. */
static void append(char *buf, size_t cap, size_t *len, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *len, cap - *len, fmt, ap);
    va_end(ap);
    TEST_ASSERT_TRUE(n > 0 && *len + (size_t)n < cap);
    *len += (size_t)n;
}

void test_controller_state_reports_every_changed_param(void)
{
    /* A node carries as many params as it likes, so a delta that touches all of them must report
     * all of them. */
    enum { num_params = 60 };
    char cfg[8192];
    char body[1024];
    size_t cfg_len = 0;
    size_t body_len = 0;
    append(cfg, sizeof(cfg), &cfg_len, "{\"devices\":[{\"id\":\"D\",\"type\":\"esp.device.other\",\"params\":[");
    append(body, sizeof(body), &body_len, "{\"state\":{\"reported\":{\"params\":{\"D\":{");
    for (int i = 0; i < num_params; i++) {
        append(cfg, sizeof(cfg), &cfg_len,
               "%s{\"id\":\"p%d\",\"data_type\":\"int\",\"properties\":[\"read\"]}", i ? "," : "", i);
        append(body, sizeof(body), &body_len, "%s\"p%d\":%d", i ? "," : "", i, i + 1);
    }
    append(cfg, sizeof(cfg), &cfg_len, "]}]}");
    append(body, sizeof(body), &body_len, "}}}}}");

    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_model_init());
    ctrl_node_t *node = controller_model_node_alloc("node60");
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_node_config(node, cfg, cfg_len));

    ctrl_param_t *changed[num_params];
    ctrl_param_t **next = changed;
    controller_state_delta_t delta = { .on_param = collect, .ctx = &next };
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_reported_state(node, body, body_len, &delta));
    TEST_ASSERT_EQUAL_size_t(num_params, delta.num_changed);
    TEST_ASSERT_EQUAL_PTR(changed + num_params, next);
    for (int i = 0; i < num_params; i++) {
        TEST_ASSERT_NOT_NULL(changed[i]);
    }
    controller_model_free_node_list(node);
}
