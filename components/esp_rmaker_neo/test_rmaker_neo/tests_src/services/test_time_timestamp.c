/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_time_timestamp.c
 * @brief Tests for the Timestamp param of the Time service.
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#if CONFIG_ESP_RMAKER_NEO_TIME_CURRENT_TIME_PARAM

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_rmaker_data_model.h"
#include "esp_rmaker_flow.h"
#include "node_internal.h"
#include "esp_rmaker_standard_types.h"
#include "data_model_internal.h"
#include "services/standard_creation.h"
#include "osal_time.h"
#include "osal_timesync.h"
#include "osal_timesync_test_hooks.h"

#if !CONFIG_OSAL_TIMESYNC_TEST_HOOKS
#error "test_time_timestamp.c needs CONFIG_OSAL_TIMESYNC_TEST_HOOKS"
#endif

static esp_rmaker_node_t *__setup(esp_rmaker_param_t **timestamp)
{
    esp_rmaker_config_t cfg = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&cfg, "ts_node", "type");
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_timezone_service_add_to_node("Etc/UTC", "UTC0", NULL));
    esp_rmaker_device_t *service = esp_rmaker_node_get_device_by_id(node, "Time");
    TEST_ASSERT_NOT_NULL(service);
    *timestamp = esp_rmaker_device_get_param_by_type(service, ESP_RMAKER_PARAM_TIMESTAMP);
    TEST_ASSERT_NOT_NULL(*timestamp);
    return node;
}

static void __teardown(esp_rmaker_node_t *node)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_timezone_service_remove_from_node());
    esp_rmaker_node_clear_stored_values(node);
    esp_rmaker_node_deinit(node);
}

static void __write(esp_rmaker_node_t *node, const char *value)
{
    char payload[96];
    snprintf(payload, sizeof(payload), "{\"Time\":{\"Timestamp\":%s}}", value);
    data_model_state_handle_update_payload_json(node, payload, strlen(payload), ESP_RMAKER_REQ_SRC_CLOUD);
}

void test_time_timestamp_param_rejects_invalid(void)
{
    esp_rmaker_param_t *timestamp = NULL;
    esp_rmaker_node_t *node = __setup(&timestamp);
    esp_rmaker_param_val_t *val = esp_rmaker_param_get_val(timestamp);
    TEST_ASSERT_EQUAL(RMAKER_VAL_TYPE_INT64, val->type);
    TEST_ASSERT_TRUE(val->val.i64 == 0);

    char *config_str = esp_rmaker_get_node_config();
    TEST_ASSERT_NOT_NULL(config_str);
    TEST_ASSERT_NOT_NULL(strstr(config_str, "\"type\":\"esp.param.timestamp\",\"data_type\":\"int64\""));
    free(config_str);

    /* A time sync that got a time ignores the writes, so stop it for this test */
    const bool timesync_was_on = osal_timesync_is_initialized();
    if (timesync_was_on) {
        osal_timesync_deinit();
    }

    /* Each one fails before the clock changes; the last three are out of range */
    const char *rejected[] = { "0", "-5", "1000", "-9223372036854775807", "4294967296", "9223372036854775807" };
    const char *accepted = NULL;
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]) && !accepted; i++) {
        __write(node, rejected[i]);
        if (val->val.i64 != 0) {
            accepted = rejected[i];
        }
    }

    __teardown(node);
    const int restore_err = timesync_was_on ? osal_timesync_init(NULL) : 0;
    TEST_ASSERT_NULL_MESSAGE(accepted, accepted);
    TEST_ASSERT_EQUAL(0, restore_err);
}

void test_time_timestamp_param_ignored_when_synced(void)
{
    if (!osal_timesync_is_synced()) {
        TEST_IGNORE_MESSAGE("Needs a valid clock");
    }
    esp_rmaker_param_t *timestamp = NULL;
    esp_rmaker_node_t *node = __setup(&timestamp);
    const bool was_synced = osal_timesync_sntp_has_synced();
    osal_timesync_test_set_sntp_synced(true);

    /* A write one day back must not step the clock that SNTP owns */
    const int64_t before = osal_get_time(NULL);
    char value[24];
    snprintf(value, sizeof(value), "%" PRId64, before - 86400);
    __write(node, value);

    const int64_t reported = esp_rmaker_param_get_val(timestamp)->val.i64;
    const int64_t after = osal_get_time(NULL);

    osal_timesync_test_set_sntp_synced(was_synced);
    __teardown(node);
    TEST_ASSERT_TRUE(reported >= before && reported <= before + 5);
    TEST_ASSERT_TRUE(after >= before);
}

void test_time_timestamp_param_applies_before_sntp_sync(void)
{
    if (!osal_timesync_is_synced()) {
        TEST_IGNORE_MESSAGE("Needs a valid clock");
    }
    esp_rmaker_param_t *timestamp = NULL;
    esp_rmaker_node_t *node = __setup(&timestamp);
    const bool timesync_was_on = osal_timesync_is_initialized();
    const int init_err = timesync_was_on ? 0 : osal_timesync_init(NULL);
    const bool was_synced = osal_timesync_sntp_has_synced();
    osal_timesync_test_set_sntp_synced(false);

    /* An ignored write reports the clock; a write that reaches the range check fails and reports nothing */
    __write(node, "1000");
    const int64_t reported = esp_rmaker_param_get_val(timestamp)->val.i64;

    osal_timesync_test_set_sntp_synced(was_synced);
    if (!timesync_was_on) {
        osal_timesync_deinit();
    }
    __teardown(node);
    TEST_ASSERT_EQUAL(0, init_err);
    TEST_ASSERT_TRUE(reported == 0);
}

#endif /* CONFIG_ESP_RMAKER_NEO_TIME_CURRENT_TIME_PARAM */
