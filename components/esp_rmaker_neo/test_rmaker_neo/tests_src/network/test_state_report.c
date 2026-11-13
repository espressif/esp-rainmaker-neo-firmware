/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_state_report.c
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include "osal_event_loop.h"
#include "osal_scheduler.h"
#include "esp_rmaker_flow.h"
#include "esp_rmaker_runtime_gate.h"
#include "esp_rmaker_data_model.h"
#include "esp_rmaker_val.h"

#include "network/network_state_report.c"

static bool __state_report_timer_armed(void)
{
    osal_scheduler_task_handle_t handle = __state_retry_contexts.state_report.handle;
    return handle != NULL && osal_scheduler_is_active(handle);
}

void test_state_report_success_rearms_late_update(void)
{
    osal_event_loop_create_default();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_state_init());

    esp_rmaker_config_t cfg = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&cfg, "test", "type");
    TEST_ASSERT_NOT_NULL(node);
    esp_rmaker_device_t *dev = esp_rmaker_device_create("d1", "test_type", NULL);
    TEST_ASSERT_NOT_NULL(dev);
    esp_rmaker_param_t *param = esp_rmaker_param_create("p1", "int_type", esp_rmaker_int(0), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(param);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, param));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, dev));
    esp_rmaker_runtime_gate_set_active(true);

    /* An update after its node was drained arms the timer before the run ends. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_param_update(param, esp_rmaker_int(1)));
    TEST_ASSERT_TRUE(__state_report_timer_armed());

    __state_report_succeeded();
    bool armed_with_update = __state_report_timer_armed();

    esp_rmaker_node_lock(node);
    __reset_node_update_list_locked(node);
    esp_rmaker_node_unlock(node);
    __state_report_succeeded();
    bool armed_without_update = __state_report_timer_armed();

    esp_rmaker_runtime_gate_set_active(false);
    esp_rmaker_node_deinit(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_state_deinit());
    osal_event_loop_delete_default();

    TEST_ASSERT_TRUE(armed_with_update);
    TEST_ASSERT_FALSE(armed_without_update);
}
