/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_bridge_accessors.c
 * @brief Unit tests for the bridge module's new accessor surface
 *        (child <-> topic_ctx, local_id, version_progress) and the
 *        per-slot in-memory version-handshake state.
 *
 * Tests use ``bridge_internal_test_seed_child`` to construct real slot
 * entries without a live MQTT broker, then exercise the accessors that
 * cloud-manager dispatch + state pipeline depend on.
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "sdkconfig.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

#include "esp_rmaker_bridge.h"
#include "bridge/bridge_internal.h"
#include "network/mqtt_topics.h"

#include "osal_event_loop.h"

#include <stddef.h>
#include <string.h>

/* Fixture *******************************************************************/

static void __setup(void)
{
    osal_event_loop_create_default();
    /* Seed-child path init's the bridge subsystem itself; nothing else
     * to do here. */
    bridge_internal_deinit();
}

static void __teardown(void)
{
    bridge_internal_deinit();
    osal_event_loop_delete_default();
}

/* bridge_internal_child_local_id *******************************************/

void test_bridge_accessors_local_id(void)
{
    __setup();
    TEST_ASSERT_NULL(bridge_internal_child_local_id(NULL));

    esp_rmaker_bridge_child_handle_t c = bridge_internal_test_seed_child("A", "lid_a", "test--A");
    TEST_ASSERT_NOT_NULL(c);
    const char *lid = bridge_internal_child_local_id(c);
    TEST_ASSERT_NOT_NULL(lid);
    TEST_ASSERT_EQUAL_STRING("lid_a", lid);

    __teardown();
}

/* node-derived topic ctx **************************************************/

void test_bridge_accessors_topic_ctx(void)
{
    __setup();
    TEST_ASSERT_NULL(esp_rmaker_node_topic_ctx(bridge_internal_child_node(NULL)));

    esp_rmaker_bridge_child_handle_t c = bridge_internal_test_seed_child("A", "lid_a", "test--A");
    TEST_ASSERT_NOT_NULL(c);

    const esp_rmaker_topic_ctx_t *ctx = esp_rmaker_node_topic_ctx(bridge_internal_child_node(c));
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_TRUE(esp_rmaker_topic_ctx_is_valid(ctx));
    /* ops must be non-NULL and resolve to the slot's child priv. */
    TEST_ASSERT_NOT_NULL(ctx->ops);

    /* Topic builders driven by the ctx resolve to the child's thing name. */
    char buf[MQTT_TOPIC_BUFFER_SIZE];
    int len = esp_rmaker_mqtt_topic_to_cloud(ctx, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(buf, "test--A"));

    /* After the child is torn down the same ctx pointer becomes invalid. */
    bridge_internal_deinit();
    TEST_ASSERT_FALSE(esp_rmaker_topic_ctx_is_valid(ctx));

    __teardown();
}

/* bridge_internal_child_from_ctx - round trip identity ********************/

void test_bridge_accessors_child_from_ctx_roundtrip(void)
{
    __setup();

    /* NULL ctx -> NULL. */
    TEST_ASSERT_NULL(bridge_internal_child_from_ctx(NULL));

    /* Self ctx -> NULL (never a child). */
    TEST_ASSERT_NULL(bridge_internal_child_from_ctx(&esp_rmaker_topic_ctx_self));

    /* Arbitrary stack-allocated ctx not in the pool -> NULL. */
    esp_rmaker_topic_ctx_t stack_ctx = { 0 };
    TEST_ASSERT_NULL(bridge_internal_child_from_ctx(&stack_ctx));

    /* Seed two children; each ctx must round-trip to its own child. */
    esp_rmaker_bridge_child_handle_t a = bridge_internal_test_seed_child("A", "lid_a", "test--A");
    esp_rmaker_bridge_child_handle_t b = bridge_internal_test_seed_child("B", "lid_b", "test--B");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_EQUAL(a, b);

    const esp_rmaker_topic_ctx_t *ctx_a = esp_rmaker_node_topic_ctx(bridge_internal_child_node(a));
    const esp_rmaker_topic_ctx_t *ctx_b = esp_rmaker_node_topic_ctx(bridge_internal_child_node(b));
    TEST_ASSERT_NOT_NULL(ctx_a);
    TEST_ASSERT_NOT_NULL(ctx_b);
    TEST_ASSERT_NOT_EQUAL(ctx_a, ctx_b);

    TEST_ASSERT_EQUAL_PTR(a, bridge_internal_child_from_ctx(ctx_a));
    TEST_ASSERT_EQUAL_PTR(b, bridge_internal_child_from_ctx(ctx_b));

    __teardown();
}

/* bridge_internal_child_from_ctx after slot invalidation *******************/

void test_bridge_accessors_child_from_ctx_after_invalidate(void)
{
    __setup();
    esp_rmaker_bridge_child_handle_t c = bridge_internal_test_seed_child("A", "lid_a", "test--A");
    TEST_ASSERT_NOT_NULL(c);
    const esp_rmaker_topic_ctx_t *ctx = esp_rmaker_node_topic_ctx(bridge_internal_child_node(c));
    TEST_ASSERT_NOT_NULL(ctx);

    /* Tear down: same ctx pointer must now resolve to NULL - slot's
     * in_use flag has been cleared but the ctx address itself remains
     * valid storage (per the topic_ctx lifetime contract). */
    bridge_internal_deinit();
    TEST_ASSERT_NULL(bridge_internal_child_from_ctx(ctx));

    __teardown();
}

/* bridge_internal_child_version_progress ***********************************/

void test_bridge_accessors_version_progress(void)
{
    __setup();
    TEST_ASSERT_NULL(bridge_internal_child_version_progress(NULL, BRIDGE_VERSION_KIND_SCHED));
    TEST_ASSERT_NULL(bridge_internal_child_version_progress(NULL, BRIDGE_VERSION_KIND_TRIGGER));

    esp_rmaker_bridge_child_handle_t c = bridge_internal_test_seed_child("A", "lid_a", "test--A");
    TEST_ASSERT_NOT_NULL(c);

    esp_rmaker_bridge_version_progress_t *sched =
        bridge_internal_child_version_progress(c, BRIDGE_VERSION_KIND_SCHED);
    esp_rmaker_bridge_version_progress_t *trig =
        bridge_internal_child_version_progress(c, BRIDGE_VERSION_KIND_TRIGGER);
    TEST_ASSERT_NOT_NULL(sched);
    TEST_ASSERT_NOT_NULL(trig);
    /* Two distinct slots per child. */
    TEST_ASSERT_NOT_EQUAL(sched, trig);

    /* Freshly-seeded slot: unset sentinels - pending_version is -1, flags clear. */
    TEST_ASSERT_FALSE(sched->is_new_version);
    TEST_ASSERT_FALSE(sched->is_new_details);
    TEST_ASSERT_EQUAL_INT(-1, sched->pending_version);
    TEST_ASSERT_FALSE(trig->is_new_version);
    TEST_ASSERT_FALSE(trig->is_new_details);
    TEST_ASSERT_EQUAL_INT(-1, trig->pending_version);

    /* Mutations through the returned pointer are observed on re-lookup. */
    sched->is_new_version = true;
    sched->pending_version = 42;
    esp_rmaker_bridge_version_progress_t *sched_again =
        bridge_internal_child_version_progress(c, BRIDGE_VERSION_KIND_SCHED);
    TEST_ASSERT_EQUAL_PTR(sched, sched_again);
    TEST_ASSERT_TRUE(sched_again->is_new_version);
    TEST_ASSERT_EQUAL_INT(42, sched_again->pending_version);
    /* Trigger slot untouched. */
    TEST_ASSERT_FALSE(trig->is_new_version);
    TEST_ASSERT_EQUAL_INT(-1, trig->pending_version);

    __teardown();
}

/* find_by_thing_name / find_by_local_id round-trip *************************/

void test_bridge_accessors_find_by_thing_name_and_local_id(void)
{
    __setup();
    esp_rmaker_bridge_child_handle_t seeded =
        bridge_internal_test_seed_child("A", "lid_a", "test--A");
    TEST_ASSERT_NOT_NULL(seeded);

    /* Positive lookups round-trip to the same handle. */
    TEST_ASSERT_EQUAL_PTR(seeded, bridge_internal_find_by_thing_name("test--A"));
    TEST_ASSERT_EQUAL_PTR(seeded, bridge_internal_find_by_local_id("lid_a"));

    /* Negative lookups. */
    TEST_ASSERT_NULL(bridge_internal_find_by_thing_name("nope"));
    TEST_ASSERT_NULL(bridge_internal_find_by_local_id("nope"));
    TEST_ASSERT_NULL(bridge_internal_find_by_thing_name(NULL));
    TEST_ASSERT_NULL(bridge_internal_find_by_local_id(NULL));

    /* After tear-down both lookups miss. */
    bridge_internal_deinit();
    TEST_ASSERT_NULL(bridge_internal_find_by_thing_name("test--A"));
    TEST_ASSERT_NULL(bridge_internal_find_by_local_id("lid_a"));

    __teardown();
}

/* Slot pool exhaustion behaviour *******************************************/

void test_bridge_accessors_slot_pool_exhaustion(void)
{
    __setup();

    /* Fill the pool. Use a different local_id per slot. */
    char suffix[8];
    char lid[16];
    char tn[24];
    size_t seeded_count = 0;
    for (size_t i = 0; i < (size_t)CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        snprintf(suffix, sizeof(suffix), "S%zu", i);
        snprintf(lid, sizeof(lid), "lid_%zu", i);
        snprintf(tn, sizeof(tn), "test--%zu", i);
        if (bridge_internal_test_seed_child(suffix, lid, tn)) {
            seeded_count++;
        }
    }
    TEST_ASSERT_EQUAL_size_t((size_t)CONFIG_RMNG_BRIDGE_MAX_CHILDREN, seeded_count);

    /* One-past-the-end fails (pool exhausted). */
    TEST_ASSERT_NULL(bridge_internal_test_seed_child("X", "lid_overflow", "test--X"));

    /* Duplicate local_id rejected even with room. */
    bridge_internal_deinit();
    TEST_ASSERT_NOT_NULL(bridge_internal_test_seed_child("A", "shared", "test--A"));
    TEST_ASSERT_NULL(bridge_internal_test_seed_child("B", "shared", "test--B"));

    __teardown();
}

/* Seed-child validates suffix + non-NULL args *******************************/

void test_bridge_accessors_seed_child_arg_validation(void)
{
    __setup();
    /* NULL args. */
    TEST_ASSERT_NULL(bridge_internal_test_seed_child(NULL, "lid", "tn"));
    TEST_ASSERT_NULL(bridge_internal_test_seed_child("A", NULL, "tn"));
    TEST_ASSERT_NULL(bridge_internal_test_seed_child("A", "lid", NULL));

    /* Suffix rejected by validator. */
    TEST_ASSERT_NULL(bridge_internal_test_seed_child("has-hyphen", "lid", "tn"));
    TEST_ASSERT_NULL(bridge_internal_test_seed_child("", "lid", "tn"));

    __teardown();
}

/* Topic parser: extended edge cases ****************************************/

void test_bridge_parse_child_from_from_cloud_topic_edge_cases(void)
{
    char child[64];

    /* Empty child segment ("//"). */
    const char *t1 = "rainmaker/bridges/brdg/children//from_cloud";
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_from_from_cloud_topic(t1, strlen(t1), child, sizeof(child)));

    /* Trailing slash on the child segment - parsed as empty. */
    const char *t2 = "rainmaker/bridges/brdg/children/A/"; /* incomplete; still parses A */
    int n = bridge_internal_parse_child_from_from_cloud_topic(t2, strlen(t2), child, sizeof(child));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("A", child);

    /* Exact-size buffer: child len = 7 (brdg--A) + NUL = 8. */
    const char *t3 = "rainmaker/bridges/brdg/children/brdg--A/from_cloud";
    char exact_too_small[7]; /* not enough for "brdg--A" + NUL */
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_from_from_cloud_topic(t3, strlen(t3), exact_too_small, sizeof(exact_too_small)));
    char exact_ok[8];
    n = bridge_internal_parse_child_from_from_cloud_topic(t3, strlen(t3), exact_ok, sizeof(exact_ok));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("brdg--A", exact_ok);

    /* topic_len shorter than the actual string truncates parsing. */
    const char *t4 = "rainmaker/bridges/brdg/children/brdg--A/from_cloud";
    /* Pass length up to the start of '/from_cloud' - child should still parse. */
    n = bridge_internal_parse_child_from_from_cloud_topic(t4, strlen("rainmaker/bridges/brdg/children/brdg--A"),
            child, sizeof(child));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("brdg--A", child);

    /* Truncated mid-child (no terminating slash) - parses what's there. */
    n = bridge_internal_parse_child_from_from_cloud_topic(t4, strlen("rainmaker/bridges/brdg/children/brdg-"),
            child, sizeof(child));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("brdg-", child);
}

void test_bridge_parse_child_and_shadow_from_params_topic_edge_cases(void)
{
    char child[64];
    char shadow[64];

    /* Multiple subgroups in the shadow name. */
    const char *t1 = "rainmaker/bridges/brdg/children/brdg--A/user/params-grp-sg1-sg2-sg3/params";
    int n = bridge_internal_parse_child_and_shadow_from_params_topic(t1, strlen(t1),
            child, sizeof(child), shadow, sizeof(shadow));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING("brdg--A", child);
    TEST_ASSERT_EQUAL_STRING("params-grp-sg1-sg2-sg3", shadow);

    /* Wrong tail (/notify instead of /params) rejected. */
    const char *t2 = "rainmaker/bridges/brdg/children/brdg--A/user/params-grp/notify";
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_and_shadow_from_params_topic(t2, strlen(t2),
                      child, sizeof(child), shadow, sizeof(shadow)));

    /* Truncated tail (/param missing trailing 's') rejected. */
    const char *t3 = "rainmaker/bridges/brdg/children/brdg--A/user/params-grp/param";
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_and_shadow_from_params_topic(t3, strlen(t3),
                      child, sizeof(child), shadow, sizeof(shadow)));

    /* Empty shadow segment. */
    const char *t4 = "rainmaker/bridges/brdg/children/brdg--A/user//params";
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_and_shadow_from_params_topic(t4, strlen(t4),
                      child, sizeof(child), shadow, sizeof(shadow)));

    /* Buffer too small for the shadow. */
    const char *t5 = "rainmaker/bridges/brdg/children/brdg--A/user/params-LongGroupName-LongSubGroup/params";
    char small_shadow[8];
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_and_shadow_from_params_topic(t5, strlen(t5),
                      child, sizeof(child), small_shadow, sizeof(small_shadow)));

    /* Null pointer guards. */
    const char *t6 = "rainmaker/bridges/brdg/children/brdg--A/user/params-grp/params";
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_and_shadow_from_params_topic(NULL, 0,
                      child, sizeof(child), shadow, sizeof(shadow)));
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_and_shadow_from_params_topic(t6, strlen(t6),
                      NULL, sizeof(child), shadow, sizeof(shadow)));
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_and_shadow_from_params_topic(t6, strlen(t6),
                      child, sizeof(child), NULL, sizeof(shadow)));
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_and_shadow_from_params_topic(t6, strlen(t6),
                      child, 0, shadow, sizeof(shadow)));
    TEST_ASSERT_EQUAL(-1, bridge_internal_parse_child_and_shadow_from_params_topic(t6, strlen(t6),
                      child, sizeof(child), shadow, 0));
}

/* Suffix validator: extended *********************************************/

void test_bridge_suffix_validator_extended(void)
{
    /* Every disallowed punctuation char rejected. */
    const char *bad[] = {
        "a.b", "a-b", "a+b", "a/b", "a\\b", "a:b", "a;b", "a,b",
        "a@b", "a#b", "a$b", "a%b", "a^b", "a&b", "a*b", "a(b",
        "a)b", "a=b", "a[b", "a]b", "a{b", "a}b", "a|b", "a\"b",
        "a'b", "a`b", "a~b", "a!b", "a?b", "a<b", "a>b", "a b",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(bridge_internal_valid_suffix(bad[i]), bad[i]);
    }

    /* Only digits / only underscore accepted. */
    TEST_ASSERT_TRUE(bridge_internal_valid_suffix("0000"));
    TEST_ASSERT_TRUE(bridge_internal_valid_suffix("____"));
    TEST_ASSERT_TRUE(bridge_internal_valid_suffix("_"));

    /* Boundary lengths. */
    TEST_ASSERT_TRUE(bridge_internal_valid_suffix("01234567890123456789012345678901")); /* 32 */
    TEST_ASSERT_FALSE(bridge_internal_valid_suffix("012345678901234567890123456789012")); /* 33 */
}

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
