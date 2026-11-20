/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_utf8.c
 * @brief Tests for the UTF-8 checks in the data model and node setters.
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_rmaker_flow.h"
#include "esp_rmaker_data_model_introspect.h"
#include "node_internal.h"
#include "data_model_internal.h"
#include "osal_storage.h"
#include "constants/nvs.h"

#define BAD     "Kitchen \xC3"
#define GOOD    "\xC3\xA9t\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x92\xA1"

static esp_rmaker_node_t *__setup(void)
{
    esp_rmaker_config_t config = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&config, "utf8_node", "utf8_type");
    TEST_ASSERT_NOT_NULL(node);
    return node;
}

static void __teardown(esp_rmaker_node_t *node)
{
    esp_rmaker_node_clear_stored_values(node);
    esp_rmaker_node_deinit(node);
}

static void __nvs_set_raw(const char *ns, const char *key, const char *data, size_t len)
{
    osal_storage_handle_t handle;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_open(RMAKER_NVS_PART_NAME, ns, OSAL_STORAGE_OPEN_READWRITE, &handle));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_erase_all(handle));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_storage_set(handle, key, data, len, OSAL_STORAGE_TYPE_BINARY));
    osal_storage_commit(handle);
    osal_storage_close(handle);
}

static int __observer_calls;
static void __observer(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                       const esp_rmaker_param_val_t *val, void *priv)
{
    __observer_calls++;
}

static int __bulk_calls;
static char __bulk_last[32];
static esp_rmaker_error_t __bulk_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
                                    uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    __bulk_calls++;
    if (count && write_req[0].val.type == RMAKER_VAL_TYPE_STRING && write_req[0].val.val.s) {
        strncpy(__bulk_last, write_req[0].val.val.s, sizeof(__bulk_last) - 1);
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __update_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                                      const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    return esp_rmaker_param_update(param, val);
}

void test_utf8_check_cases(void)
{
    const struct {
        const char *s;
        bool valid;
    } cases[] = {
        { "", true },
        { "plain ascii", true },
        { "\xC3\xA9", true },                 /* U+00E9, 2 bytes */
        { "\xE2\x82\xAC", true },             /* U+20AC, 3 bytes */
        { "\xF0\x9F\x92\xA1", true },         /* U+1F4A1, 4 bytes */
        { "\xF4\x8F\xBF\xBF", true },         /* U+10FFFF, the last code point */
        { "\xC3", false },                    /* truncated at the end */
        { "\xC3 tail", false },               /* truncated in the middle */
        { "\x80", false },                    /* lone continuation byte */
        { "\xC0\x80", false },                /* overlong NUL */
        { "\xE0\x80\x80", false },            /* overlong 3-byte form */
        { "\xF0\x80\x80\x80", false },        /* overlong 4-byte form */
        { "\xED\xA0\x80", false },            /* UTF-16 surrogate U+D800 */
        { "\xF4\x90\x80\x80", false },        /* above U+10FFFF */
        { "\xFF", false },
        { "ok \xFE ok", false },
    };
    TEST_ASSERT_TRUE(esp_rmaker_str_is_utf8(NULL));
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TEST_ASSERT_EQUAL_MESSAGE(cases[i].valid, esp_rmaker_str_is_utf8(cases[i].s), cases[i].s);
    }
}

void test_utf8_param_create(void)
{
    TEST_ASSERT_NULL(esp_rmaker_param_create("p", "t", esp_rmaker_str(BAD), PROP_FLAG_READ));

    esp_rmaker_param_t *param = esp_rmaker_param_create("p", "t", esp_rmaker_str(GOOD), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(param);
    TEST_ASSERT_EQUAL_STRING(GOOD, esp_rmaker_param_get_val(param)->val.s);
    esp_rmaker_param_delete(param);

    param = esp_rmaker_param_create("p", "t", esp_rmaker_str(NULL), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(param);
    esp_rmaker_param_delete(param);

    /* Object and array values are raw JSON, which json_generator does not check. */
    param = esp_rmaker_param_create("p", "t", esp_rmaker_obj("{\"k\":\"" BAD "\"}"), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(param);
    esp_rmaker_param_delete(param);
    param = esp_rmaker_param_create("p", "t", esp_rmaker_array("[\"" BAD "\"]"), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(param);
    esp_rmaker_param_delete(param);
}

void test_utf8_param_update(void)
{
    esp_rmaker_node_t *node = __setup();
    esp_rmaker_device_t *device = esp_rmaker_device_create("d_upd", "t", NULL);
    esp_rmaker_param_t *name = esp_rmaker_param_create("nm", "t", esp_rmaker_str("old"),
                               PROP_FLAG_READ | PROP_FLAG_PERSIST);
    esp_rmaker_param_t *num = esp_rmaker_param_create("n", "t", esp_rmaker_int(1), PROP_FLAG_READ);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(device, name));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(device, num));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, device));

    __observer_calls = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update_observer_register(__observer, NULL));

    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update(name, esp_rmaker_str(BAD)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update_and_report(name, esp_rmaker_str(BAD)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update_and_notify(name, esp_rmaker_str(BAD)));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_update(num, esp_rmaker_str(BAD)));
    TEST_ASSERT_EQUAL_STRING("old", esp_rmaker_param_get_val(name)->val.s);
    TEST_ASSERT_EQUAL(1, esp_rmaker_param_get_val(num)->val.i);
    TEST_ASSERT_EQUAL(0, __observer_calls);

    esp_rmaker_param_val_t stored = { .type = RMAKER_VAL_TYPE_STRING };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_get_stored_value((_esp_rmaker_param_t *)name, &stored));
    TEST_ASSERT_EQUAL_STRING("old", stored.val.s);
    free(stored.val.s);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(name, esp_rmaker_str(GOOD)));
    TEST_ASSERT_EQUAL_STRING(GOOD, esp_rmaker_param_get_val(name)->val.s);
    TEST_ASSERT_EQUAL(1, __observer_calls);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update_observer_unregister(__observer));
    __teardown(node);
}

void test_utf8_param_valid_values(void)
{
    esp_rmaker_param_t *str = esp_rmaker_param_create("s", "t", esp_rmaker_str("a"), PROP_FLAG_READ);
    esp_rmaker_param_t *num = esp_rmaker_param_create("n", "t", esp_rmaker_int(0), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_NOT_NULL(num);

    esp_rmaker_param_value_label_t good[] = {
        { esp_rmaker_str("a"), "A" },
        { esp_rmaker_str(GOOD), GOOD },
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_add_valid_values(str, good, 2));

    esp_rmaker_param_value_label_t bad_value[] = {
        { esp_rmaker_str("a"), "A" },
        { esp_rmaker_str(BAD), "B" },
    };
    esp_rmaker_param_value_label_t bad_label[] = {
        { esp_rmaker_str("a"), "A" },
        { esp_rmaker_str("b"), BAD },
    };
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_add_valid_values(str, bad_value, 2));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_add_valid_values(str, bad_label, 2));

    /* A rejected call keeps the earlier list. */
    _esp_rmaker_param_t *_str = (_esp_rmaker_param_t *)str;
    TEST_ASSERT_EQUAL(2, _str->n_valid_values);
    TEST_ASSERT_EQUAL_STRING(GOOD, _str->valid_values[1].label);

    esp_rmaker_param_value_label_t num_bad_label[] = { { esp_rmaker_int(0), BAD } };
    esp_rmaker_param_value_label_t num_good_label[] = { { esp_rmaker_int(0), GOOD } };
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_param_add_valid_values(num, num_bad_label, 1));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_add_valid_values(num, num_good_label, 1));

    esp_rmaker_param_delete(str);
    esp_rmaker_param_delete(num);
}

void test_utf8_device_attribute(void)
{
    esp_rmaker_device_t *device = esp_rmaker_device_create("d_attr", "t", NULL);
    TEST_ASSERT_NOT_NULL(device);

    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_add_attribute(device, BAD, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_device_add_attribute(device, "k", BAD));
    TEST_ASSERT_NULL(((_esp_rmaker_device_t *)device)->attributes);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_attribute(device, GOOD, GOOD));
    TEST_ASSERT_EQUAL_STRING(GOOD, ((_esp_rmaker_device_t *)device)->attributes->value);
    esp_rmaker_device_delete(device);
}

void test_utf8_node_attribute_and_tag(void)
{
    esp_rmaker_node_t *node = __setup();

    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_attribute(node, BAD, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_attribute(node, "k", BAD));
    TEST_ASSERT_NULL(esp_rmaker_node_get_first_attribute(node));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_attribute(node, "k", GOOD));
    TEST_ASSERT_EQUAL_STRING(GOOD, esp_rmaker_node_get_first_attribute(node)->value);

    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_tag(node, BAD, "v"));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_add_tag(node, "t", BAD));
    TEST_ASSERT_NULL(esp_rmaker_node_get_tag_by_name(node, "t"));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_tag(node, "t", GOOD));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_update_tag(node, "t", BAD));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_update_tag(node, BAD, "v"));
    TEST_ASSERT_EQUAL_STRING(GOOD, esp_rmaker_node_get_tag_by_name(node, "t")->value);

    __teardown(node);
}

void test_utf8_node_info(void)
{
    TEST_ASSERT_NULL(esp_rmaker_node_create(BAD, "t"));
    TEST_ASSERT_NULL(esp_rmaker_node_create("n", BAD));

    /* A rejected create leaves no self node, so a later init works. */
    esp_rmaker_node_t *node = __setup();

    const esp_rmaker_node_info_t good = {
        .name = "n", .type = "t", .fw_version = "1.0", .model = "m",
    };
    esp_rmaker_node_info_t info;
    for (int field = 0; field < 4; field++) {
        info = good;
        char **slot[] = { &info.name, &info.type, &info.fw_version, &info.model };
        *slot[field] = BAD;
        TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_node_fill_with_info(node, &info));
    }
    /* The check runs before the "already filled" check, so valid info reaches it. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_STATE, esp_rmaker_node_fill_with_info(node, &good));
    TEST_ASSERT_EQUAL_STRING("utf8_node", esp_rmaker_node_get_info(node)->name);

    __teardown(node);
}

void test_utf8_restore_invalid_stored_value(void)
{
    esp_rmaker_node_t *node = __setup();
    const char *dev_id = "d_rst_bad";
    __nvs_set_raw(dev_id, "nm", "bad\xFF", 4);

    esp_rmaker_device_t *device = esp_rmaker_device_create(dev_id, "t", NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_bulk_cb(device, __bulk_cb, NULL));
    esp_rmaker_param_t *param = esp_rmaker_param_create("nm", "t", esp_rmaker_str("def"),
                                PROP_FLAG_READ | PROP_FLAG_PERSIST);
    __bulk_calls = 0;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(device, param));

    TEST_ASSERT_EQUAL_STRING("def", esp_rmaker_param_get_val(param)->val.s);
    TEST_ASSERT_EQUAL(0, __bulk_calls);

    /* The default replaces the invalid stored value. */
    esp_rmaker_param_val_t stored = { .type = RMAKER_VAL_TYPE_STRING };
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_get_stored_value((_esp_rmaker_param_t *)param, &stored));
    TEST_ASSERT_EQUAL_STRING("def", stored.val.s);
    free(stored.val.s);

    esp_rmaker_device_clear_stored_values(device);
    esp_rmaker_device_delete(device);
    __teardown(node);
}

void test_utf8_restore_valid_stored_value(void)
{
    esp_rmaker_node_t *node = __setup();
    const char *dev_id = "d_rst_ok";
    __nvs_set_raw(dev_id, "nm", GOOD, strlen(GOOD));

    esp_rmaker_device_t *device = esp_rmaker_device_create(dev_id, "t", NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_bulk_cb(device, __bulk_cb, NULL));
    esp_rmaker_param_t *param = esp_rmaker_param_create("nm", "t", esp_rmaker_str("def"),
                                PROP_FLAG_READ | PROP_FLAG_PERSIST);
    __bulk_calls = 0;
    memset(__bulk_last, 0, sizeof(__bulk_last));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(device, param));

    TEST_ASSERT_EQUAL_STRING(GOOD, esp_rmaker_param_get_val(param)->val.s);
    TEST_ASSERT_EQUAL(1, __bulk_calls);
    TEST_ASSERT_EQUAL_STRING(GOOD, __bulk_last);

    esp_rmaker_device_clear_stored_values(device);
    esp_rmaker_device_delete(device);
    __teardown(node);
}

void test_utf8_cloud_write_keeps_reports_working(void)
{
    esp_rmaker_node_t *node = __setup();
    esp_rmaker_device_t *device = esp_rmaker_device_create("d_cloud", "t", NULL);
    esp_rmaker_param_t *param = esp_rmaker_param_create("nm", "t", esp_rmaker_str("old"), PROP_FLAG_READ | PROP_FLAG_WRITE);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(device, param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_cb(device, __update_cb, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, device));

    /* A raw invalid byte, then a lone escaped surrogate. The return code depends on the parser. */
    const char *payloads[] = {
        "{\"d_cloud\":{\"nm\":\"bad\xFF\"}}",
        "{\"d_cloud\":{\"nm\":\"\\ud800\"}}",
    };
    for (size_t i = 0; i < sizeof(payloads) / sizeof(payloads[0]); i++) {
        data_model_state_handle_update_payload_json(esp_rmaker_get_node(), payloads[i], strlen(payloads[i]),
                ESP_RMAKER_REQ_SRC_CLOUD);
        TEST_ASSERT_EQUAL_STRING("old", esp_rmaker_param_get_val(param)->val.s);
    }

    char *params = data_model_node_get_node_params();
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_NOT_NULL(strstr(params, "\"old\""));
    free(params);
    char *config = esp_rmaker_get_node_config();
    TEST_ASSERT_NOT_NULL(config);
    free(config);

    __teardown(node);
}
