/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_rmng_common_prototypes.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_rmaker_work_queue.h"
#include "osal_event_group.h"
#include "osal_task.h"
#include "osal_log.h"

static const char *TAG = "test_work_queue";

#include "sdkconfig.h"
#define TEST_WORK_QUEUE_TASKS_TO_ADD CONFIG_RMAKER_WORK_QUEUE_TASK_QUEUE_SIZE /* Follow maximum queue size */
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
