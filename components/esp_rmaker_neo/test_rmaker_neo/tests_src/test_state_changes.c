/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_state_changes.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "network/state_changes.h"

#include "osal_event_loop.h"
#include "osal_task.h"

#include <stdatomic.h>

#include "esp_rmaker_flow.h"
#include "esp_rmaker_data_model.h"
#include "esp_rmaker_val.h"
#include "esp_rmaker_mqtt_impl.h"

void test_state_changes_lock_unlock(void)
{
    osal_event_loop_create_default();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_state_init());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_state_lock());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_state_unlock());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_state_deinit());
    osal_event_loop_delete_default();
}

/* --- Concurrency regression: mark_for_update vs report drain --------------- */

#define SC_STRESS_ITERS 4000

static esp_rmaker_param_t *s_sc_p1;
static esp_rmaker_param_t *s_sc_p2;
static atomic_bool s_sc_marker_done;

/* No-op publish so the scheduled report drain runs without a broker. The drain
 * still frees (release()s) the pending list entries, which is what races the
 * marker - exactly the production scenario. */
static osal_err_t __sc_noop_publish(osal_mqtt_event_loop_channel_t *channel, const char *topic,
                                    size_t topic_len, void *data, size_t data_len, osal_mqtt_QoS_t qos, bool retain)
{
    (void)channel; (void)topic; (void)topic_len; (void)data; (void)data_len; (void)qos; (void)retain;
    return OSAL_ERR_OK;
}

static void __sc_marker_task(void *arg)
{
    (void)arg;
    /* Two params back-to-back mimics the timezone service's fan-out (multiple
     * marks per event) that made the original use-after-free reproducible. */
    for (int i = 0; i < SC_STRESS_ITERS; i++) {
        esp_rmaker_param_update(s_sc_p1, esp_rmaker_int(i));
        esp_rmaker_param_update(s_sc_p2, esp_rmaker_int(i + 1));
    }
    atomic_store(&s_sc_marker_done, true);
    osal_task_delete(NULL);
}

/*
 * Regression for the use-after-free in esp_rmaker_state_mark_for_update: it used
 * to dereference update_id (get_value / to_node / check_and_fire) AFTER inserting
 * it into the pending report list, which transfers ownership. The report drain
 * frees list entries on another task, so the post-insert deref could touch freed
 * memory. This hammers a marker thread against a concurrent drain. Under ASan the
 * pre-fix code reports heap-use-after-free; the fixed code (deref before insert)
 * stays clean. Pass == completes without a crash / ASan abort.
 */
void test_state_changes_concurrent_mark_and_drain(void)
{
    osal_event_loop_create_default();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_state_init());
    esp_rmaker_mqtt_impl.publish = __sc_noop_publish;

    esp_rmaker_config_t cfg = { .enable_time_sync = false };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&cfg, "test", "type");
    TEST_ASSERT_NOT_NULL(node);
    esp_rmaker_device_t *dev = esp_rmaker_device_create("d1", "test_type", NULL);
    TEST_ASSERT_NOT_NULL(dev);
    s_sc_p1 = esp_rmaker_param_create("p1", "int_type", esp_rmaker_int(0), PROP_FLAG_READ);
    s_sc_p2 = esp_rmaker_param_create("p2", "int_type", esp_rmaker_int(0), PROP_FLAG_READ);
    TEST_ASSERT_NOT_NULL(s_sc_p1);
    TEST_ASSERT_NOT_NULL(s_sc_p2);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, s_sc_p1));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_device_add_param(dev, s_sc_p2));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_node_add_device(node, dev));

    atomic_store(&s_sc_marker_done, false);
    osal_task_handle_t marker = NULL;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK,
                      osal_task_create(__sc_marker_task, "sc_marker", 16384, NULL, 5, &marker));

    /* Drainer: repeatedly free the pending list (releasing update_ids) while the
     * marker keeps inserting - the exact window the old code mishandled. */
    while (!atomic_load(&s_sc_marker_done)) {
        esp_rmaker_state_drop_node(node);
    }
    esp_rmaker_state_drop_node(node);

    /* Let the marker task fully exit before teardown. */
    osal_task_delay(200);

    esp_rmaker_node_deinit(node);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_state_deinit());
    osal_event_loop_delete_default();
}
