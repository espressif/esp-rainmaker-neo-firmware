/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_automation.c
 * @brief Unit tests for automation trigger-details JSON parsing
 *
 * Includes automation.c directly to test static parsing helpers. Triggers are
 * per-node: each test operates on the self node's embedded ``automation`` list
 * under the per-node lock.
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osal_semaphore.h"
#include "sdkconfig.h"

#include "services/automation.h"
#include "esp_rmaker_flow.h"
#include "node_internal.h"

#include "esp_rmaker_val.h"

/* Include automation.c to access static parsing functions. */
#include "services/automation.c"

/* A node's embedded trigger substruct. */
#define NODE_AUTO(node) (&((_esp_rmaker_node_t *)(node))->automation)

/* A bare in-place self node for the parse-only tests: ``_esp_rmaker_node_init``
 * creates just the per-node lock (and self topic ops) without requiring the
 * full node bring-up (credentials / factory NVS). Tests that need real
 * devices/params use ``esp_rmaker_node_init`` directly instead. */
static _esp_rmaker_node_t s_test_node;

static esp_rmaker_node_t *automation_test_node_setup(void)
{
    memset(&s_test_node, 0, sizeof(s_test_node));
    _esp_rmaker_node_init(&s_test_node);
    TEST_ASSERT_NOT_NULL(s_test_node.lock);
    return (esp_rmaker_node_t *)&s_test_node;
}

static void automation_test_node_teardown(esp_rmaker_node_t *node)
{
    _esp_rmaker_node_t *n = (_esp_rmaker_node_t *)node;
    _esp_rmaker_node_reset(n);
    /* _esp_rmaker_node_reset preserves the lock; delete it to avoid a leak
     * across tests (the next setup memsets and recreates). */
    if (n->lock) {
        osal_semaphore_delete(n->lock);
        n->lock = NULL;
    }
}


typedef struct {
    const char *id;
    esp_rmaker_val_compare_t cop;
    esp_rmaker_val_type_t ty;
    int i;
    float f;
    bool b;
    const char *s;
} automation_parse_expected_row_t;

static void automation_test_assert_parsed_triggers(const esp_rmaker_node_t *node, const automation_parse_expected_row_t *exp, size_t n)
{
    const node_automation_trigger_state_t *a = NODE_AUTO(node);
    TEST_ASSERT_NOT_NULL(a->list);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)n, a->count);
    for (size_t i = 0; i < n; i++) {
        const esp_rmaker_automation_trigger_t *t = &a->list[i];
        TEST_ASSERT_EQUAL_STRING(exp[i].id, t->id);
        TEST_ASSERT_EQUAL(exp[i].cop, t->compare_op);
        TEST_ASSERT_EQUAL(exp[i].ty, t->expected_val.type);
        switch (exp[i].ty) {
        case RMAKER_VAL_TYPE_INTEGER:
            TEST_ASSERT_EQUAL_INT(exp[i].i, t->expected_val.val.i);
            break;
        case RMAKER_VAL_TYPE_FLOAT:
            TEST_ASSERT_FLOAT_WITHIN(1e-4f, exp[i].f, t->expected_val.val.f);
            break;
        case RMAKER_VAL_TYPE_BOOLEAN:
            TEST_ASSERT_EQUAL(exp[i].b, t->expected_val.val.b);
            break;
        case RMAKER_VAL_TYPE_STRING:
        case RMAKER_VAL_TYPE_OBJECT:
        case RMAKER_VAL_TYPE_ARRAY:
            TEST_ASSERT_NOT_NULL(t->expected_val.val.s);
            TEST_ASSERT_EQUAL_STRING(exp[i].s, t->expected_val.val.s);
            break;
        default:
            TEST_FAIL_MESSAGE("unexpected value type");
            break;
        }
    }
}

void test_automation_parse_details_null_invalid_arg(void)
{
    esp_rmaker_node_t *node = automation_test_node_setup();
    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, __build_trigger_details_for_node_locked(node, NULL, 1, NULL, NULL));
    esp_rmaker_node_unlock(node);
    automation_test_node_teardown(node);
}

void test_automation_parse_details_empty_invalid_arg(void)
{
    esp_rmaker_node_t *node = automation_test_node_setup();
    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, __build_trigger_details_for_node_locked(node, "", 0, NULL, NULL));
    esp_rmaker_node_unlock(node);
    automation_test_node_teardown(node);
}

void test_automation_parse_details_invalid_json_fail(void)
{
    esp_rmaker_node_t *node = automation_test_node_setup();
    const char *json = "{";
    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, __build_trigger_details_for_node_locked(node, json, strlen(json), NULL, NULL));
    esp_rmaker_node_unlock(node);
    automation_test_node_teardown(node);
}

void test_automation_parse_details_whitespace_empty_array(void)
{
    esp_rmaker_node_t *node = automation_test_node_setup();
    const char *json = "  [   ]  ";
    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __build_trigger_details_for_node_locked(node, json, strlen(json), NULL, NULL));
    TEST_ASSERT_NULL(NODE_AUTO(node)->list);
    TEST_ASSERT_EQUAL_UINT8(0, NODE_AUTO(node)->count);
    esp_rmaker_node_unlock(node);
    automation_test_node_teardown(node);
}

void test_automation_parse_details_whitespace_invalid_triggers(void)
{
    /* Extra whitespace; triggers lack device/param so parsing fails - must not leak the allocated list */
    esp_rmaker_node_t *node = automation_test_node_setup();
    const char *json = "  [  {  \"id\"  :  \"a\"  }  ,  {  \"id\"  :  \"b\"  }  ]  ";
    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, __build_trigger_details_for_node_locked(node, json, strlen(json), NULL, NULL));
    TEST_ASSERT_NULL(NODE_AUTO(node)->list);
    TEST_ASSERT_EQUAL_UINT8(0, NODE_AUTO(node)->count);
    esp_rmaker_node_unlock(node);
    automation_test_node_teardown(node);
}

void test_automation_parse_details_all_invalid_frees_list(void)
{
    esp_rmaker_node_t *node = automation_test_node_setup();
    const char *json = "[{\"id\":\"x\"},{\"id\":\"y\"}]";
    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, __build_trigger_details_for_node_locked(node, json, strlen(json), NULL, NULL));
    TEST_ASSERT_NULL(NODE_AUTO(node)->list);
    TEST_ASSERT_EQUAL_UINT8(0, NODE_AUTO(node)->count);
    esp_rmaker_node_unlock(node);
    automation_test_node_teardown(node);
}

void test_automation_parse_details_valid_format(void)
{
    static const automation_parse_expected_row_t k_exp[] = {
        /* integer: all six operators */
        { .id = "ti_eq", .cop = RMAKER_VAL_COMPARE_EQ, .ty = RMAKER_VAL_TYPE_INTEGER, .i = 10 },
        { .id = "ti_ne", .cop = RMAKER_VAL_COMPARE_NEQ, .ty = RMAKER_VAL_TYPE_INTEGER, .i = 11 },
        { .id = "ti_gt", .cop = RMAKER_VAL_COMPARE_GT, .ty = RMAKER_VAL_TYPE_INTEGER, .i = 5 },
        { .id = "ti_lt", .cop = RMAKER_VAL_COMPARE_LT, .ty = RMAKER_VAL_TYPE_INTEGER, .i = 100 },
        { .id = "ti_ge", .cop = RMAKER_VAL_COMPARE_GTE, .ty = RMAKER_VAL_TYPE_INTEGER, .i = 7 },
        { .id = "ti_le", .cop = RMAKER_VAL_COMPARE_LTE, .ty = RMAKER_VAL_TYPE_INTEGER, .i = 99 },
        /* float: all six operators */
        { .id = "tf_eq", .cop = RMAKER_VAL_COMPARE_EQ, .ty = RMAKER_VAL_TYPE_FLOAT, .f = 1.5f },
        { .id = "tf_ne", .cop = RMAKER_VAL_COMPARE_NEQ, .ty = RMAKER_VAL_TYPE_FLOAT, .f = 2.5f },
        { .id = "tf_gt", .cop = RMAKER_VAL_COMPARE_GT, .ty = RMAKER_VAL_TYPE_FLOAT, .f = 0.5f },
        { .id = "tf_lt", .cop = RMAKER_VAL_COMPARE_LT, .ty = RMAKER_VAL_TYPE_FLOAT, .f = 9.875f },
        { .id = "tf_ge", .cop = RMAKER_VAL_COMPARE_GTE, .ty = RMAKER_VAL_TYPE_FLOAT, .f = 1.25f },
        { .id = "tf_le", .cop = RMAKER_VAL_COMPARE_LTE, .ty = RMAKER_VAL_TYPE_FLOAT, .f = 8.25f },
        /* boolean: eq / ne only */
        { .id = "tb_eq", .cop = RMAKER_VAL_COMPARE_EQ, .ty = RMAKER_VAL_TYPE_BOOLEAN, .b = true },
        { .id = "tb_ne", .cop = RMAKER_VAL_COMPARE_NEQ, .ty = RMAKER_VAL_TYPE_BOOLEAN, .b = false },
        /* string: eq / ne only */
        { .id = "ts_eq", .cop = RMAKER_VAL_COMPARE_EQ, .ty = RMAKER_VAL_TYPE_STRING, .s = "alpha" },
        { .id = "ts_ne", .cop = RMAKER_VAL_COMPARE_NEQ, .ty = RMAKER_VAL_TYPE_STRING, .s = "beta" },
        /* object: eq / ne only (JSON object values) */
        { .id = "to_eq", .cop = RMAKER_VAL_COMPARE_EQ, .ty = RMAKER_VAL_TYPE_OBJECT, .s = "{\"k\":1}" },
        { .id = "to_ne", .cop = RMAKER_VAL_COMPARE_NEQ, .ty = RMAKER_VAL_TYPE_OBJECT, .s = "{\"k\":2}" },
        /* array: eq / ne only */
        { .id = "ta_eq", .cop = RMAKER_VAL_COMPARE_EQ, .ty = RMAKER_VAL_TYPE_ARRAY, .s = "[1]" },
        { .id = "ta_ne", .cop = RMAKER_VAL_COMPARE_NEQ, .ty = RMAKER_VAL_TYPE_ARRAY, .s = "[1,2]" },
    };

    esp_rmaker_config_t cfg = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&cfg, "auto_parse_valid", "type");
    TEST_ASSERT_NOT_NULL(node);

    esp_rmaker_device_t *dev = esp_rmaker_device_create("dev_a", "dtype", NULL);
    TEST_ASSERT_NOT_NULL(dev);

    esp_rmaker_param_t *p_i = esp_rmaker_param_create("param_i", "itype", esp_rmaker_int(0), PROP_FLAG_READ);
    esp_rmaker_param_t *p_f = esp_rmaker_param_create("param_f", "ftype", esp_rmaker_float(0.f), PROP_FLAG_READ);
    esp_rmaker_param_t *p_b = esp_rmaker_param_create("param_b", "btype", esp_rmaker_bool(false), PROP_FLAG_READ);
    esp_rmaker_param_t *p_s = esp_rmaker_param_create("param_s", "stype", esp_rmaker_str(""), PROP_FLAG_READ);
    esp_rmaker_param_t *p_o = esp_rmaker_param_create("param_o", "otype", esp_rmaker_obj("{}"), PROP_FLAG_READ);
    esp_rmaker_param_t *p_a = esp_rmaker_param_create("param_arr", "atype", esp_rmaker_array("[]"), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(p_i);
    TEST_ASSERT_NOT_NULL(p_f);
    TEST_ASSERT_NOT_NULL(p_b);
    TEST_ASSERT_NOT_NULL(p_s);
    TEST_ASSERT_NOT_NULL(p_o);
    TEST_ASSERT_NOT_NULL(p_a);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, p_i));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, p_f));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, p_b));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, p_s));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, p_o));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, p_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, dev));

    const char *json =
        "["
        "{\"id\":\"ti_eq\",\"path\":\"dev_a.param_i\",\"operator\":\"eq\",\"value\":10},"
        "{\"id\":\"ti_ne\",\"path\":\"dev_a.param_i\",\"operator\":\"ne\",\"value\":11},"
        "{\"id\":\"ti_gt\",\"path\":\"dev_a.param_i\",\"operator\":\"gt\",\"value\":5},"
        "{\"id\":\"ti_lt\",\"path\":\"dev_a.param_i\",\"operator\":\"lt\",\"value\":100},"
        "{\"id\":\"ti_ge\",\"path\":\"dev_a.param_i\",\"operator\":\"ge\",\"value\":7},"
        "{\"id\":\"ti_le\",\"path\":\"dev_a.param_i\",\"operator\":\"le\",\"value\":99},"
        "{\"id\":\"tf_eq\",\"path\":\"dev_a.param_f\",\"operator\":\"eq\",\"value\":1.5},"
        "{\"id\":\"tf_ne\",\"path\":\"dev_a.param_f\",\"operator\":\"ne\",\"value\":2.5},"
        "{\"id\":\"tf_gt\",\"path\":\"dev_a.param_f\",\"operator\":\"gt\",\"value\":0.5},"
        "{\"id\":\"tf_lt\",\"path\":\"dev_a.param_f\",\"operator\":\"lt\",\"value\":9.875},"
        "{\"id\":\"tf_ge\",\"path\":\"dev_a.param_f\",\"operator\":\"ge\",\"value\":1.25},"
        "{\"id\":\"tf_le\",\"path\":\"dev_a.param_f\",\"operator\":\"le\",\"value\":8.25},"
        "{\"id\":\"tb_eq\",\"path\":\"dev_a.param_b\",\"operator\":\"eq\",\"value\":true},"
        "{\"id\":\"tb_ne\",\"path\":\"dev_a.param_b\",\"operator\":\"ne\",\"value\":false},"
        "{\"id\":\"ts_eq\",\"path\":\"dev_a.param_s\",\"operator\":\"eq\",\"value\":\"alpha\"},"
        "{\"id\":\"ts_ne\",\"path\":\"dev_a.param_s\",\"operator\":\"ne\",\"value\":\"beta\"},"
        "{\"id\":\"to_eq\",\"path\":\"dev_a.param_o\",\"operator\":\"eq\",\"value\":{\"k\":1}},"
        "{\"id\":\"to_ne\",\"path\":\"dev_a.param_o\",\"operator\":\"ne\",\"value\":{\"k\":2}},"
        "{\"id\":\"ta_eq\",\"path\":\"dev_a.param_arr\",\"operator\":\"eq\",\"value\":[1]},"
        "{\"id\":\"ta_ne\",\"path\":\"dev_a.param_arr\",\"operator\":\"ne\",\"value\":[1,2]}"
        "]";

    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __build_trigger_details_for_node_locked(node, json, strlen(json), NULL, NULL));
    automation_test_assert_parsed_triggers(node, k_exp, sizeof(k_exp) / sizeof(k_exp[0]));
    esp_rmaker_node_unlock(node);

    esp_rmaker_node_clear_stored_values(node);
    esp_rmaker_node_deinit(node);
}

void test_automation_parse_details_exceeds_maximum(void)
{
    esp_rmaker_node_t *node = automation_test_node_setup();

    /* MAX+1 well-formed triggers. Content must be valid now that every
     * trigger is structurally parsed before the count limit is enforced;
     * the limit still rejects the over-large batch with INVALID_ARG. */
    const int n = RMAKER_TRIGGER_MAX_COUNT + 1;
    const char *one = "{\"id\":\"i\",\"path\":\"p\",\"operator\":\"eq\",\"value\":1}";
    size_t one_len = strlen(one);
    size_t buf_len = 2 + (size_t)n * one_len + (size_t)(n - 1);
    char *json = (char *)malloc(buf_len + 1);
    TEST_ASSERT_NOT_NULL(json);
    char *p = json;
    *p++ = '[';
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            *p++ = ',';
        }
        memcpy(p, one, one_len);
        p += one_len;
    }
    *p++ = ']';
    *p = '\0';

    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, __build_trigger_details_for_node_locked(node, json, strlen(json), NULL, NULL));
    TEST_ASSERT_NULL(NODE_AUTO(node)->list);
    TEST_ASSERT_EQUAL_UINT8(0, NODE_AUTO(node)->count);
    esp_rmaker_node_unlock(node);

    free(json);
    automation_test_node_teardown(node);
}

void test_automation_parse_details_boolean_non_eq_ne_operators_rejected(void)
{
    static const char *const forbidden_ops[] = { "gt", "lt", "ge", "le" };

    esp_rmaker_config_t cfg = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&cfg, "auto_bool_ops", "type");
    TEST_ASSERT_NOT_NULL(node);

    esp_rmaker_device_t *dev = esp_rmaker_device_create("dev_a", "dtype", NULL);
    TEST_ASSERT_NOT_NULL(dev);
    esp_rmaker_param_t *param = esp_rmaker_param_create("param_b", "btype", esp_rmaker_bool(false), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(param);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, dev));

    char json[192];
    for (size_t i = 0; i < sizeof(forbidden_ops) / sizeof(forbidden_ops[0]); i++) {
        int n = snprintf(json, sizeof(json),
                         "[{\"id\":\"tb\",\"path\":\"dev_a.param_b\",\"operator\":\"%s\",\"value\":true}]",
                         forbidden_ops[i]);
        TEST_ASSERT_GREATER_THAN(0, n);
        TEST_ASSERT_LESS_THAN((int)sizeof(json), n);

        esp_rmaker_node_lock(node);
        /* Parse failure on any element fails the whole update (rolls back). */
        TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, __build_trigger_details_for_node_locked(node, json, (size_t)n, NULL, NULL));
        TEST_ASSERT_NULL(NODE_AUTO(node)->list);
        TEST_ASSERT_EQUAL_UINT8(0, NODE_AUTO(node)->count);
        esp_rmaker_node_unlock(node);
    }

    esp_rmaker_node_clear_stored_values(node);
    esp_rmaker_node_deinit(node);
}

void test_automation_parse_details_partial_failure_rolls_back(void)
{
    esp_rmaker_config_t cfg = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&cfg, "auto_parse_node", "type");
    TEST_ASSERT_NOT_NULL(node);

    esp_rmaker_device_t *dev = esp_rmaker_device_create("dev_a", "dtype", NULL);
    TEST_ASSERT_NOT_NULL(dev);
    esp_rmaker_param_t *param = esp_rmaker_param_create("param_i", "itype", esp_rmaker_int(0), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(param);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, dev));

    const char *json = "["
                       "  {  \"id\"  :  \"t1\"  ,  \"device\"  :  \"dev_a\"  ,  \"param\"  :  \"param_i\"  ,  \"operator\"  :  \"eq\"  ,  \"value\"  :  1  }  ,  "
                       "  {  \"id\"  :  \"t2\"  }  "
                       "]";
    esp_rmaker_node_lock(node);
    /* Any parse failure rolls back the whole list; disabled-only entries are not parse failures. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, __build_trigger_details_for_node_locked(node, json, strlen(json), NULL, NULL));
    TEST_ASSERT_NULL(NODE_AUTO(node)->list);
    TEST_ASSERT_EQUAL_UINT8(0, NODE_AUTO(node)->count);
    esp_rmaker_node_unlock(node);

    esp_rmaker_node_clear_stored_values(node);
    esp_rmaker_node_deinit(node);
}

void test_automation_parse_details_enabled_false_single_no_triggers(void)
{
    /* Disabled triggers parse OK but add nothing; all-disabled is success with an empty list. */
    esp_rmaker_node_t *node = automation_test_node_setup();
    const char *json = "[{\"id\":\"off\",\"enabled\":false}]";
    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __build_trigger_details_for_node_locked(node, json, strlen(json), NULL, NULL));
    TEST_ASSERT_NULL(NODE_AUTO(node)->list);
    TEST_ASSERT_EQUAL_UINT8(0, NODE_AUTO(node)->count);
    esp_rmaker_node_unlock(node);
    automation_test_node_teardown(node);
}

void test_automation_parse_details_enabled_omitted_adds_trigger(void)
{
    static const automation_parse_expected_row_t k_exp[] = {
        { .id = "no_enabled_key", .cop = RMAKER_VAL_COMPARE_EQ, .ty = RMAKER_VAL_TYPE_INTEGER, .i = 42 },
    };

    esp_rmaker_config_t cfg = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&cfg, "auto_en_omit", "type");
    TEST_ASSERT_NOT_NULL(node);

    esp_rmaker_device_t *dev = esp_rmaker_device_create("dev_a", "dtype", NULL);
    TEST_ASSERT_NOT_NULL(dev);
    esp_rmaker_param_t *param = esp_rmaker_param_create("param_i", "itype", esp_rmaker_int(0), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(param);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, dev));

    const char *json = "[{\"id\":\"no_enabled_key\",\"path\":\"dev_a.param_i\",\"operator\":\"eq\",\"value\":42}]";

    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __build_trigger_details_for_node_locked(node, json, strlen(json), NULL, NULL));
    automation_test_assert_parsed_triggers(node, k_exp, sizeof(k_exp) / sizeof(k_exp[0]));
    esp_rmaker_node_unlock(node);

    esp_rmaker_node_clear_stored_values(node);
    esp_rmaker_node_deinit(node);
}

void test_automation_parse_details_enabled_explicit_true_and_false_skips(void)
{
    static const automation_parse_expected_row_t k_exp[] = {
        { .id = "on_a", .cop = RMAKER_VAL_COMPARE_EQ, .ty = RMAKER_VAL_TYPE_INTEGER, .i = 1 },
        { .id = "on_b", .cop = RMAKER_VAL_COMPARE_EQ, .ty = RMAKER_VAL_TYPE_INTEGER, .i = 2 },
    };

    esp_rmaker_config_t cfg = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&cfg, "auto_en_mix", "type");
    TEST_ASSERT_NOT_NULL(node);

    esp_rmaker_device_t *dev = esp_rmaker_device_create("dev_a", "dtype", NULL);
    TEST_ASSERT_NOT_NULL(dev);
    esp_rmaker_param_t *param = esp_rmaker_param_create("param_i", "itype", esp_rmaker_int(0), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(param);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, dev));

    const char *json = "["
                       "{\"id\":\"on_a\",\"enabled\":true,\"path\":\"dev_a.param_i\",\"operator\":\"eq\",\"value\":1},"
                       "{\"id\":\"skip\",\"enabled\":false},"
                       "{\"id\":\"on_b\",\"path\":\"dev_a.param_i\",\"operator\":\"eq\",\"value\":2}"
                       "]";

    esp_rmaker_node_lock(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, __build_trigger_details_for_node_locked(node, json, strlen(json), NULL, NULL));
    automation_test_assert_parsed_triggers(node, k_exp, sizeof(k_exp) / sizeof(k_exp[0]));
    esp_rmaker_node_unlock(node);

    esp_rmaker_node_clear_stored_values(node);
    esp_rmaker_node_deinit(node);
}
