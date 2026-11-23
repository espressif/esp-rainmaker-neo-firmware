/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_controller_parse.c
 * @brief Tests for the response parsing: expired credentials and GroupInfo.
 *
 * Mistaking a denial for expired credentials re-fetches credentials on every refused call; missing
 * a real expiry leaves the controller locked out until the signer's own refresh comes round.
 */

#include <string.h>

#include "unity.h"

#include "controller_internal.h"

/* --- expired credentials --------------------------------------------------------------------- */

void test_controller_expired_credentials_detected_in_a_403_body(void)
{
    static const char body[] = "{\"message\":\"The security token included in the request is expired\"}";
    TEST_ASSERT_TRUE(controller_is_expired_credentials(body, strlen(body)));
}

void test_controller_expired_credentials_not_reported_for_a_denial(void)
{
    static const char body[] = "{\"Message\":\"User: arn:aws:sts::1:assumed-role/x is not authorized\"}";
    TEST_ASSERT_FALSE(controller_is_expired_credentials(body, strlen(body)));
}

void test_controller_expired_credentials_honours_body_len(void)
{
    static const char body[] = "{\"message\":\"is expired\"}";
    /* The body is not NUL-terminated; a match past body_len must not count. */
    TEST_ASSERT_FALSE(controller_is_expired_credentials(body, 12));
    TEST_ASSERT_FALSE(controller_is_expired_credentials(NULL, 0));
}

/* --- GroupInfo ------------------------------------------------------------------------------ */

#define SEEN_MAX 4

typedef struct {
    char group_name[32];
    char nodes[SEEN_MAX][RMAKER_THING_NAME_BUFFER_SIZE];
    int  num_nodes;
    char subgroups[SEEN_MAX][RMAKER_SUBGROUP_BUFFER_SIZE];
    char subgroup_names[SEEN_MAX][32];
    int  num_subgroups;
    char members[SEEN_MAX][RMAKER_SUBGROUP_BUFFER_SIZE + RMAKER_THING_NAME_BUFFER_SIZE];
    int  num_members;
    int  stop_after_nodes;   /* 0 never stops */
} group_ctx_t;

static esp_rmaker_error_t on_group(void *user_ctx, const char *group_name)
{
    group_ctx_t *ctx = (group_ctx_t *)user_ctx;
    snprintf(ctx->group_name, sizeof(ctx->group_name), "%s", group_name);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t on_node(void *user_ctx, const char *node_id)
{
    group_ctx_t *ctx = (group_ctx_t *)user_ctx;
    if (ctx->num_nodes < SEEN_MAX) {
        snprintf(ctx->nodes[ctx->num_nodes], sizeof(ctx->nodes[0]), "%s", node_id);
    }
    ctx->num_nodes++;
    if (ctx->stop_after_nodes && ctx->num_nodes >= ctx->stop_after_nodes) {
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t on_subgroup(void *user_ctx, const char *subgroup_id, const char *subgroup_name)
{
    group_ctx_t *ctx = (group_ctx_t *)user_ctx;
    if (ctx->num_subgroups < SEEN_MAX) {
        snprintf(ctx->subgroups[ctx->num_subgroups], sizeof(ctx->subgroups[0]), "%s", subgroup_id);
        snprintf(ctx->subgroup_names[ctx->num_subgroups], sizeof(ctx->subgroup_names[0]), "%s", subgroup_name);
    }
    ctx->num_subgroups++;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t on_member(void *user_ctx, const char *subgroup_id, const char *node_id)
{
    group_ctx_t *ctx = (group_ctx_t *)user_ctx;
    if (ctx->num_members < SEEN_MAX) {
        snprintf(ctx->members[ctx->num_members], sizeof(ctx->members[0]), "%s:%s", subgroup_id, node_id);
    }
    ctx->num_members++;
    return ESP_RMAKER_OK;
}

static const controller_group_info_cbs_t k_cbs = {
    .group = on_group,
    .node = on_node,
    .subgroup = on_subgroup,
    .member = on_member,
};

void test_controller_group_info_reports_every_node(void)
{
    const char *body = "{\"group_id\":\"grp123\",\"node_ids\":[\"nodeA\",\"nodeB\",\"nodeC\"]}";
    group_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_group_info(body, strlen(body), &k_cbs, &ctx));
    TEST_ASSERT_EQUAL_INT(3, ctx.num_nodes);
    TEST_ASSERT_EQUAL_STRING("nodeA", ctx.nodes[0]);
    TEST_ASSERT_EQUAL_STRING("nodeC", ctx.nodes[2]);
    TEST_ASSERT_EQUAL_INT(0, ctx.num_subgroups);
}

void test_controller_group_info_reports_the_group_name(void)
{
    const char *body = "{\"group_id\":\"grp123\",\"group_name\":\"Home\",\"node_ids\":[\"nodeA\"]}";
    group_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_group_info(body, strlen(body), &k_cbs, &ctx));
    TEST_ASSERT_EQUAL_STRING("Home", ctx.group_name);
}

void test_controller_group_info_tolerates_a_missing_group_name(void)
{
    const char *body = "{\"group_id\":\"grp123\",\"node_ids\":[\"nodeA\"]}";
    group_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_group_info(body, strlen(body), &k_cbs, &ctx));
    TEST_ASSERT_EQUAL_STRING("", ctx.group_name);
    TEST_ASSERT_EQUAL_INT(1, ctx.num_nodes);
}

void test_controller_group_info_reports_subgroups_and_their_members(void)
{
    /* Every node's subgroup path is already on the wire, which is what lets the SDK derive every
     * shadow name itself. */
    const char *body =
        "{\"group_id\":\"grp123\",\"node_ids\":[\"nodeA\",\"nodeB\"],"
        "\"subgroups\":[{\"subgroup_id\":\"sgA\",\"subgroup_name\":\"Kitchen\",\"node_ids\":[\"nodeA\"]},"
        "{\"subgroup_id\":\"sgB\",\"subgroup_name\":\"Hall\",\"node_ids\":[\"nodeA\",\"nodeB\"]}]}";
    group_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_group_info(body, strlen(body), &k_cbs, &ctx));
    TEST_ASSERT_EQUAL_INT(2, ctx.num_nodes);
    TEST_ASSERT_EQUAL_INT(2, ctx.num_subgroups);
    TEST_ASSERT_EQUAL_STRING("sgA", ctx.subgroups[0]);
    TEST_ASSERT_EQUAL_STRING("Kitchen", ctx.subgroup_names[0]);
    TEST_ASSERT_EQUAL_STRING("sgB", ctx.subgroups[1]);
    TEST_ASSERT_EQUAL_INT(3, ctx.num_members);
    TEST_ASSERT_EQUAL_STRING("sgA:nodeA", ctx.members[0]);
    TEST_ASSERT_EQUAL_STRING("sgB:nodeA", ctx.members[1]);
    TEST_ASSERT_EQUAL_STRING("sgB:nodeB", ctx.members[2]);
}

void test_controller_group_info_tolerates_a_missing_subgroup_name(void)
{
    const char *body =
        "{\"node_ids\":[\"nodeA\"],\"subgroups\":[{\"subgroup_id\":\"sgA\",\"node_ids\":[\"nodeA\"]}]}";
    group_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_group_info(body, strlen(body), &k_cbs, &ctx));
    TEST_ASSERT_EQUAL_INT(1, ctx.num_subgroups);
    TEST_ASSERT_EQUAL_STRING("", ctx.subgroup_names[0]);
}

void test_controller_group_info_stops_when_a_callback_stops(void)
{
    const char *body = "{\"node_ids\":[\"nodeA\",\"nodeB\",\"nodeC\"]}";
    group_ctx_t ctx = { .stop_after_nodes = 2 };
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_FAIL, controller_parse_group_info(body, strlen(body), &k_cbs, &ctx));
    TEST_ASSERT_EQUAL_INT(2, ctx.num_nodes);
}

void test_controller_group_info_reports_an_empty_group(void)
{
    const char *body = "{\"node_ids\":[]}";
    group_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_OK, controller_parse_group_info(body, strlen(body), &k_cbs, &ctx));
    TEST_ASSERT_EQUAL_INT(0, ctx.num_nodes);
}

void test_controller_group_info_rejects_a_body_without_node_ids(void)
{
    const char *body = "{\"group_id\":\"grp123\"}";
    group_ctx_t ctx = {0};
    TEST_ASSERT_EQUAL_INT(ESP_RMAKER_NOT_FOUND,
                          controller_parse_group_info(body, strlen(body), &k_cbs, &ctx));
}
