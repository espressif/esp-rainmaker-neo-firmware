/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Compile backoff.c against a fake scheduler that holds the race window open. */
#define osal_scheduler_schedule_task fake_sched_schedule_task
#define osal_scheduler_reset_timer   fake_sched_reset_timer
#define osal_scheduler_cancel_task   fake_sched_cancel_task
#define esp_rmaker_backoff_reset     race_backoff_reset
#define esp_rmaker_backoff_retry     race_backoff_retry
#define esp_rmaker_backoff_fire      race_backoff_fire
#include "../../../src/retry/backoff.c"

#include "unity.h"
#include "test_rmaker_neo_common_prototypes.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "osal_semaphore.h"
#include "osal_task.h"
#include "osal_ticks.h"

#define FAKE_SCHED_SLOTS          8
#define RACE_WINDOW_TIMEOUT_MS    100
#define RACE_TASK_STACK_SIZE      4096
#define RACE_TASK_PRIORITY        2

typedef struct {
    bool used;
    bool live;
} fake_sched_slot_t;

static fake_sched_slot_t s_slots[FAKE_SCHED_SLOTS];
static osal_semaphore_handle_t s_fake_lock;
static osal_semaphore_handle_t s_in_window;
static osal_semaphore_handle_t s_release;
static osal_semaphore_handle_t s_task_done;
static bool s_block_next_cancel;
static bool s_block_next_schedule;
static int s_reset_on_dead;
static esp_rmaker_backoff_retry_context_t s_race_ctx;

static void __race_noop_task(void *arg)
{
    (void)arg;
}

static int __live_count_locked(void)
{
    int live = 0;
    for (int i = 0; i < FAKE_SCHED_SLOTS; i++) {
        live += s_slots[i].live ? 1 : 0;
    }
    return live;
}

/* Signal that the caller is inside the window, then wait for the other task or the timeout. */
static void __hold_window(void)
{
    osal_semaphore_give(s_in_window);
    (void)osal_semaphore_take(s_release, osal_ticks_from_ms(RACE_WINDOW_TIMEOUT_MS));
}

osal_err_t fake_sched_schedule_task(osal_scheduler_task_handle_t *handle, uint64_t delay_ms, osal_scheduler_task_t task, void *arg)
{
    (void)delay_ms;
    (void)task;
    (void)arg;

    osal_semaphore_take(s_fake_lock, OSAL_MAX_DELAY);
    fake_sched_slot_t *slot = NULL;
    for (int i = 0; i < FAKE_SCHED_SLOTS && slot == NULL; i++) {
        if (!s_slots[i].used) {
            slot = &s_slots[i];
        }
    }
    if (slot == NULL) {
        osal_semaphore_give(s_fake_lock);
        return OSAL_ERR_NO_MEM;
    }
    slot->used = true;
    slot->live = true;
    bool block = s_block_next_schedule;
    s_block_next_schedule = false;
    osal_semaphore_give(s_fake_lock);

    if (block) {
        __hold_window();
    }
    *handle = (osal_scheduler_task_handle_t)slot;
    return OSAL_ERR_OK;
}

osal_err_t fake_sched_reset_timer(osal_scheduler_task_handle_t handle, uint64_t delay_ms)
{
    (void)delay_ms;
    fake_sched_slot_t *slot = (fake_sched_slot_t *)handle;

    osal_semaphore_take(s_fake_lock, OSAL_MAX_DELAY);
    bool live = slot->live;
    if (!live) {
        s_reset_on_dead++;
    }
    osal_semaphore_give(s_fake_lock);
    return live ? OSAL_ERR_OK : OSAL_ERR_INVALID_STATE;
}

osal_err_t fake_sched_cancel_task(osal_scheduler_task_handle_t *handle)
{
    fake_sched_slot_t *slot = (fake_sched_slot_t *)*handle;

    osal_semaphore_take(s_fake_lock, OSAL_MAX_DELAY);
    slot->live = false;
    bool block = s_block_next_cancel;
    s_block_next_cancel = false;
    osal_semaphore_give(s_fake_lock);

    /* The real cancel also frees the timer before it clears the handle. */
    if (block) {
        __hold_window();
    }
    *handle = NULL;
    return OSAL_ERR_OK;
}

static void __race_setup(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    s_block_next_cancel = false;
    s_block_next_schedule = false;
    s_reset_on_dead = 0;

    s_fake_lock = osal_semaphore_create_mutex();
    s_in_window = osal_semaphore_create_binary();
    s_release = osal_semaphore_create_binary();
    s_task_done = osal_semaphore_create_binary();
    TEST_ASSERT_NOT_NULL(s_fake_lock);
    TEST_ASSERT_NOT_NULL(s_in_window);
    TEST_ASSERT_NOT_NULL(s_release);
    TEST_ASSERT_NOT_NULL(s_task_done);

    s_race_ctx = ESP_RMAKER_BACKOFF_DEFAULT_RETRY_CONTEXT();
}

static void __race_teardown(void)
{
    osal_semaphore_delete(s_task_done);
    osal_semaphore_delete(s_release);
    osal_semaphore_delete(s_in_window);
    osal_semaphore_delete(s_fake_lock);
}

static void __race_reset_task(void *arg)
{
    (void)arg;
    race_backoff_reset(&s_race_ctx, 1000);
    osal_semaphore_give(s_task_done);
    osal_task_delete(NULL);
}

static void __race_retry_task(void *arg)
{
    (void)arg;
    (void)race_backoff_retry(&s_race_ctx, __race_noop_task, NULL);
    osal_semaphore_give(s_task_done);
    osal_task_delete(NULL);
}

/* Run fn in a second task, wait until it is inside the window, then call retry from this task. */
static void __race_against_retry(osal_task_function_t fn)
{
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_task_create(fn, "backoff_race", RACE_TASK_STACK_SIZE, NULL, RACE_TASK_PRIORITY, NULL));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take(s_in_window, osal_ticks_from_ms(1000)));

    (void)race_backoff_retry(&s_race_ctx, __race_noop_task, NULL);

    osal_semaphore_give(s_release);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take(s_task_done, osal_ticks_from_ms(1000)));
}

void test_backoff_race_reset_vs_retry(void)
{
    __race_setup();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, race_backoff_retry(&s_race_ctx, __race_noop_task, NULL));
    TEST_ASSERT_NOT_NULL(s_race_ctx.handle);

    s_block_next_cancel = true;
    __race_against_retry(__race_reset_task);

    TEST_ASSERT_EQUAL_MESSAGE(0, s_reset_on_dead, "retry re-armed a cancelled timer");
    race_backoff_reset(&s_race_ctx, 1000);
    TEST_ASSERT_EQUAL(0, __live_count_locked());
    __race_teardown();
}

void test_backoff_race_concurrent_schedule(void)
{
    __race_setup();

    s_block_next_schedule = true;
    __race_against_retry(__race_retry_task);

    TEST_ASSERT_EQUAL_MESSAGE(1, __live_count_locked(), "concurrent retries left an orphaned timer");
    race_backoff_reset(&s_race_ctx, 1000);
    TEST_ASSERT_EQUAL(0, __live_count_locked());
    __race_teardown();
}

void test_backoff_race_reset_during_retry(void)
{
    __race_setup();

    s_block_next_schedule = true;
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_task_create(__race_retry_task, "backoff_race", RACE_TASK_STACK_SIZE, NULL, RACE_TASK_PRIORITY, NULL));
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take(s_in_window, osal_ticks_from_ms(1000)));

    race_backoff_reset(&s_race_ctx, 1000);

    osal_semaphore_give(s_release);
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take(s_task_done, osal_ticks_from_ms(1000)));

    TEST_ASSERT_EQUAL_MESSAGE(0, __live_count_locked(), "retry in progress outlived the reset");
    TEST_ASSERT_NULL(s_race_ctx.handle);
    __race_teardown();
}
