/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_rmaker_neo_common_prototypes.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_rmaker_work_queue.h"
#include "osal_event_group.h"
#include "osal_task.h"
#include "osal_log.h"

static const char *TAG = "test_work_queue";

#include "sdkconfig.h"
#define TEST_WORK_QUEUE_TASKS_TO_ADD CONFIG_ESP_RMAKER_NEO_WORK_QUEUE_TASK_QUEUE_SIZE /* Follow maximum queue size */
#define TEST_WORK_QUEUE_FINISHED_FLAG 0x01
#define TEST_WORK_QUEUE_HANG_TASK_RUNNING_FLAG 0x02

static int execution_counter = 0;
static int execution_order[TEST_WORK_QUEUE_TASKS_TO_ADD] = {-1};
static osal_event_group_handle_t execution_event_group;

static bool continue_hanging = false;

static void __hang_task(void *p)
{
    osal_event_group_set_bits(execution_event_group, TEST_WORK_QUEUE_HANG_TASK_RUNNING_FLAG);
    while (continue_hanging) {
        osal_task_delay(10);
    }

    OSAL_LOGI(TAG, "Hang task finished");
}
static void __dummy(void *p)
{
    (void)p;
}

static void __init_event_group(void)
{
    execution_event_group = osal_event_group_create();
    TEST_ASSERT_NOT_NULL_MESSAGE(execution_event_group, "Failed to create event group");
}

static void __delete_event_group(void)
{
    osal_event_group_delete(execution_event_group);
}

static void test_work_queue_task(void *priv_data)
{
    int task_id = (int)(uintptr_t)priv_data;
    execution_order[execution_counter++] = task_id;
    if (execution_counter == TEST_WORK_QUEUE_TASKS_TO_ADD) {
        osal_event_group_set_bits(execution_event_group, TEST_WORK_QUEUE_FINISHED_FLAG);
    }
}

void test_work_queue_basic(void)
{
    esp_rmaker_error_t err;
    char *err_msg = NULL;

    __init_event_group();

    err = esp_rmaker_work_queue_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_RMAKER_OK, err, "Failed to initialize work queue");

    /* Start the work queue */
    err = esp_rmaker_work_queue_start();
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to start work queue";
        goto test_work_queue_basic_end;
    }

    /* Add tasks to the work queue */
    for (int i = 0; i < TEST_WORK_QUEUE_TASKS_TO_ADD; i++) {
        err = esp_rmaker_work_queue_add_task(test_work_queue_task, (void *)(uintptr_t)i);
        if (err != ESP_RMAKER_OK) {
            err_msg = "Failed to add task to work queue";
            goto test_work_queue_basic_end;
        }
    }

    /* Wait for all tasks to finish */
    osal_event_group_bits_t waited_bits = osal_event_group_wait_bits(execution_event_group, TEST_WORK_QUEUE_FINISHED_FLAG, true, true, osal_ticks_from_ms(1000));
    if (waited_bits != TEST_WORK_QUEUE_FINISHED_FLAG) {
        err_msg = "Failed to wait for tasks to finish";
        goto test_work_queue_basic_end;
    }

    /* Check the execution order */
    for (int i = 0; i < TEST_WORK_QUEUE_TASKS_TO_ADD; i++) {
        if (execution_order[i] == -1) {
            err_msg = "Did not execute all tasks";
            goto test_work_queue_basic_end;
        }
        if (execution_order[i] != i) {
            err_msg = "Task execution order is incorrect";
            goto test_work_queue_basic_end;
        }
    }

    err = esp_rmaker_work_queue_stop();
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to stop work queue";
    }

test_work_queue_basic_end:
    __delete_event_group();
    err = esp_rmaker_work_queue_deinit();
    if (err != ESP_RMAKER_OK) {
        err_msg = "Failed to de-init work queue";
    } else {
        /* Should not be able to add tasks after de-init */
        err = esp_rmaker_work_queue_add_task(test_work_queue_task, (void *)(uintptr_t)0);
        if (err == ESP_RMAKER_OK) {
            err_msg = "Should not be able to add tasks after de-init";
        }
    }

    if (err_msg) {
        TEST_FAIL_MESSAGE(err_msg);
    } else {
        TEST_PASS();
    }
}

void test_work_queue_error_paths(void)
{
    __init_event_group();

    /* Add task before init should fail */
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_work_queue_add_task(__dummy, NULL));

    /* Start before init should fail */
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_work_queue_start());

    /* Init twice should be OK (idempotent) */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_init());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_init());

    /* Stop before start should be OK (noop) */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_stop());

    /* Should fail if adding more tasks than the queue size */
    continue_hanging = true;
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_add_task(__hang_task, NULL));
    for (int i = 1; i < TEST_WORK_QUEUE_TASKS_TO_ADD; i++) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_add_task(__dummy, NULL));
    }
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_work_queue_add_task(__dummy, NULL));

    /* Start then start again should be OK (idempotent) */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_start());
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_start());

    /* Wait for the hang task to be running */
    TEST_ASSERT_EQUAL(TEST_WORK_QUEUE_HANG_TASK_RUNNING_FLAG, osal_event_group_wait_bits(execution_event_group, TEST_WORK_QUEUE_HANG_TASK_RUNNING_FLAG, true, true, osal_ticks_from_ms(1000)));

    /* Should fail if adding a task while hanging */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_add_task(__dummy, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_work_queue_add_task(__dummy, NULL));
    continue_hanging = false;

    /* Deinit while running will request stop and wait then succeed */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_deinit());

    /* Deinit again should be OK (noop) */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_deinit());

    /* Add task after deinit should fail */
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_work_queue_add_task(__dummy, NULL));

    __delete_event_group();
}

/* Instances *******************************************************************/

/* The singleton cases above must keep passing unchanged: that is the proof the singleton API is a
 * faithful wrapper over one static instance. These cover what only instances can do. */

#define TEST_WQ_INSTANCE_QUEUE_SIZE 4
#define TEST_WQ_A_DONE_FLAG 0x04
#define TEST_WQ_B_DONE_FLAG 0x08

static int wq_a_runs;
static int wq_b_runs;
static bool wq_current_inside;
static esp_rmaker_work_queue_handle_t wq_a;
static esp_rmaker_work_queue_handle_t wq_b;

static esp_rmaker_work_queue_handle_t __make_queue(const char *name)
{
    const esp_rmaker_work_queue_config_t cfg = {
        .name = name,
        .queue_size = TEST_WQ_INSTANCE_QUEUE_SIZE,
        .task_stack = 8192,
        .task_priority = 1,
    };
    esp_rmaker_work_queue_handle_t wq = esp_rmaker_work_queue_create_inst(&cfg);
    TEST_ASSERT_NOT_NULL_MESSAGE(wq, "Failed to create the work queue instance");
    return wq;
}

static void __count_a(void *p)
{
    (void)p;
    wq_a_runs++;
    osal_event_group_set_bits(execution_event_group, TEST_WQ_A_DONE_FLAG);
}

static void __count_b(void *p)
{
    (void)p;
    wq_b_runs++;
    osal_event_group_set_bits(execution_event_group, TEST_WQ_B_DONE_FLAG);
}

static void __probe_current_task(void *p)
{
    /* True only on this instance's own worker, which is what lets a work function run a nested job
     * inline instead of deadlocking on the task that would have to run it. */
    wq_current_inside = esp_rmaker_work_queue_is_current_task_inst(wq_a)
                        && !esp_rmaker_work_queue_is_current_task_inst(wq_b);
    osal_event_group_set_bits(execution_event_group, TEST_WQ_A_DONE_FLAG);
}

void test_work_queue_instances_run_independently(void)
{
    __init_event_group();
    wq_a = __make_queue("test_wq_a");
    wq_b = __make_queue("test_wq_b");
    wq_a_runs = 0;
    wq_b_runs = 0;

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_init_inst(wq_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_init_inst(wq_b));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_start_inst(wq_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_start_inst(wq_b));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_add_task_inst(wq_a, __count_a, NULL, 0));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_add_task_inst(wq_b, __count_b, NULL, 0));
    TEST_ASSERT_EQUAL(TEST_WQ_A_DONE_FLAG | TEST_WQ_B_DONE_FLAG,
                      osal_event_group_wait_bits(execution_event_group,
                              TEST_WQ_A_DONE_FLAG | TEST_WQ_B_DONE_FLAG,
                              true, true, osal_ticks_from_ms(2000)));
    TEST_ASSERT_EQUAL_INT(1, wq_a_runs);
    TEST_ASSERT_EQUAL_INT(1, wq_b_runs);

    /* Parking one must leave the other draining. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_deinit_inst(wq_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_work_queue_add_task_inst(wq_a, __count_a, NULL, 0));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_add_task_inst(wq_b, __count_b, NULL, 0));
    TEST_ASSERT_EQUAL(TEST_WQ_B_DONE_FLAG,
                      osal_event_group_wait_bits(execution_event_group, TEST_WQ_B_DONE_FLAG, true, true,
                              osal_ticks_from_ms(2000)));
    TEST_ASSERT_EQUAL_INT(1, wq_a_runs);
    TEST_ASSERT_EQUAL_INT(2, wq_b_runs);

    /* An instance is reopened by the next init, exactly as the singleton is. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_init_inst(wq_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_start_inst(wq_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_add_task_inst(wq_a, __count_a, NULL, 0));
    TEST_ASSERT_EQUAL(TEST_WQ_A_DONE_FLAG,
                      osal_event_group_wait_bits(execution_event_group, TEST_WQ_A_DONE_FLAG, true, true,
                              osal_ticks_from_ms(2000)));
    TEST_ASSERT_EQUAL_INT(2, wq_a_runs);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_deinit_inst(wq_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_deinit_inst(wq_b));
    __delete_event_group();
}

void test_work_queue_instance_capacity_is_per_instance(void)
{
    __init_event_group();
    wq_a = __make_queue("test_wq_cap_a");
    wq_b = __make_queue("test_wq_cap_b");

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_init_inst(wq_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_init_inst(wq_b));

    /* Neither is started, so nothing drains and the queue fills to its own configured depth. */
    for (int i = 0; i < TEST_WQ_INSTANCE_QUEUE_SIZE; i++) {
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_add_task_inst(wq_a, __dummy, NULL, 0));
    }
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL, esp_rmaker_work_queue_add_task_inst(wq_a, __dummy, NULL, 0));
    /* Filling one must not consume the other's capacity. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_add_task_inst(wq_b, __dummy, NULL, 0));

    /* A finite timeout on a full queue reports the failure rather than dropping silently. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_FAIL,
                      esp_rmaker_work_queue_add_task_inst(wq_a, __dummy, NULL, osal_ticks_from_ms(50)));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_deinit_inst(wq_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_deinit_inst(wq_b));
    __delete_event_group();
}

void test_work_queue_instance_reports_its_own_task(void)
{
    __init_event_group();
    wq_a = __make_queue("test_wq_cur_a");
    wq_b = __make_queue("test_wq_cur_b");
    wq_current_inside = false;

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_init_inst(wq_a));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_start_inst(wq_a));

    /* False outside any work function. */
    TEST_ASSERT_FALSE(esp_rmaker_work_queue_is_current_task_inst(wq_a));
    TEST_ASSERT_FALSE(esp_rmaker_work_queue_is_current_task_inst(NULL));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK,
                      esp_rmaker_work_queue_add_task_inst(wq_a, __probe_current_task, NULL, 0));
    TEST_ASSERT_EQUAL(TEST_WQ_A_DONE_FLAG,
                      osal_event_group_wait_bits(execution_event_group, TEST_WQ_A_DONE_FLAG, true, true,
                              osal_ticks_from_ms(2000)));
    TEST_ASSERT_TRUE(wq_current_inside);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_work_queue_deinit_inst(wq_a));
    __delete_event_group();
}

void test_work_queue_instance_arg_validation(void)
{
    TEST_ASSERT_NULL(esp_rmaker_work_queue_create_inst(NULL));
    const esp_rmaker_work_queue_config_t no_name = { .queue_size = 2, .task_stack = 4096 };
    TEST_ASSERT_NULL(esp_rmaker_work_queue_create_inst(&no_name));
    const esp_rmaker_work_queue_config_t no_depth = { .name = "x", .task_stack = 4096 };
    TEST_ASSERT_NULL(esp_rmaker_work_queue_create_inst(&no_depth));

    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_work_queue_init_inst(NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_work_queue_start_inst(NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_work_queue_stop_inst(NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_work_queue_deinit_inst(NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_work_queue_add_task_inst(NULL, __dummy, NULL, 0));

    esp_rmaker_work_queue_handle_t wq = __make_queue("test_wq_args");
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, esp_rmaker_work_queue_add_task_inst(wq, NULL, NULL, 0));
}
