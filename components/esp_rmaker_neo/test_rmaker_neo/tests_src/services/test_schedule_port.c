/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_schedule_port.c
 * @brief Unit tests for the esp_schedule osal port's timer-record pool.
 *
 * Includes esp_schedule_port_osal.c directly to reach the pool internals. The
 * behaviour under test - a dispatch that was already in flight when its timer
 * was cancelled - cannot be produced through the public API on any backend the
 * host build has (POSIX joins the callback thread, the virtual scheduler drops
 * the record under its own lock), which is precisely why it is driven here by
 * calling the trampoline with a stale id directly.
 *
 * Kept in its own translation unit rather than folded into test_schedules.c:
 * both that file and the port define a file-local ``TAG``.
 */

#include "unity.h"
#include "test_rmaker_neo_prototypes.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "osal_semaphore.h"
#include "osal_task.h"
#include "osal_ticks.h"

/* Include the port to access the static pool and trampoline. */
#include "esp_schedule_port_osal.c"

/* Helpers ******************************************************************/

static int __fired_a;
static int __fired_b;

static void __cb_a(void *priv_data)
{
    (void)priv_data;
    __fired_a++;
}

static void __cb_b(void *priv_data)
{
    (void)priv_data;
    __fired_b++;
}

/** Bring ::__cb_mutex up; the pool refuses to operate without it. */
static void __port_test_setup(void)
{
    TEST_ASSERT_NOT_NULL(esp_schedule_port_osal_get());
    TEST_ASSERT_NOT_NULL(__cb_mutex);
    __fired_a = 0;
    __fired_b = 0;
}

/* Tests ********************************************************************/

void test_schedule_port_release_bumps_generation(void)
{
    __port_test_setup();

    __port_timer_t *t = __pool_acquire();
    TEST_ASSERT_NOT_NULL(t);
    const uint32_t first_id = __pool_id_of(t);
    const uint16_t index = t->index;
    t->cb = __cb_a;

    __pool_release(t);

    /* Same slot comes back off the free list, with a different id. */
    __port_timer_t *again = __pool_acquire();
    TEST_ASSERT_EQUAL_PTR(t, again);
    TEST_ASSERT_EQUAL_UINT16(index, again->index);
    TEST_ASSERT_NOT_EQUAL(first_id, __pool_id_of(again));

    again->cb = __cb_b;
    __pool_release(again);
}

void test_schedule_port_lookup_rejects_released_slot(void)
{
    __port_test_setup();

    __port_timer_t *t = __pool_acquire();
    TEST_ASSERT_NOT_NULL(t);
    t->cb = __cb_a;
    const uint32_t id = __pool_id_of(t);

    TEST_ASSERT_EQUAL_PTR(t, __pool_lookup(id));

    __pool_release(t);
    TEST_ASSERT_NULL(__pool_lookup(id));
}

void test_schedule_port_lookup_rejects_out_of_range_index(void)
{
    __port_test_setup();

    /* Index beyond anything the pool has grown to. */
    const uint32_t bogus = 0xFFFEU;
    TEST_ASSERT_NULL(__pool_lookup(bogus));
}

/**
 * The regression this pool exists for: a dispatch issued before the slot was
 * cancelled must not run against whatever schedule reused the slot afterwards.
 */
void test_schedule_port_stale_dispatch_does_not_fire_recycled_slot(void)
{
    __port_test_setup();

    __port_timer_t *t = __pool_acquire();
    TEST_ASSERT_NOT_NULL(t);
    t->cb = __cb_a;
    t->priv_data = NULL;
    const uint32_t stale_id = __pool_id_of(t);

    /* Cancel, then arm a different schedule that lands on the same slot. */
    __pool_release(t);
    __port_timer_t *reused = __pool_acquire();
    TEST_ASSERT_EQUAL_PTR(t, reused);
    reused->cb = __cb_b;
    reused->priv_data = NULL;

    /* The in-flight dispatch finally runs, still carrying the old id. */
    __timer_trampoline((void *)(uintptr_t)stale_id);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, __fired_a, "cancelled schedule must not fire");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, __fired_b, "recycled slot must not fire for a stale id");

    /* The live id still works, so the check is not simply rejecting everything. */
    __timer_trampoline((void *)(uintptr_t)__pool_id_of(reused));
    TEST_ASSERT_EQUAL_INT(1, __fired_b);

    __pool_release(reused);
}

/** Churn must recycle slots rather than growing the pool without bound. */
void test_schedule_port_pool_recycles_slots(void)
{
    __port_test_setup();

    const uint16_t blocks_before = __block_count;

    for (int i = 0; i < 200; i++) {
        __port_timer_t *t = __pool_acquire();
        TEST_ASSERT_NOT_NULL(t);
        t->cb = __cb_a;
        __pool_release(t);
    }

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(blocks_before, __block_count,
                                     "sequential acquire/release must reuse one slot");
}

/** Concurrently-held slots are distinct, and all come back on release. */
void test_schedule_port_pool_grows_then_reuses(void)
{
    __port_test_setup();

    __port_timer_t *held[PORT_BLOCK_SLOTS * 2];
    for (size_t i = 0; i < sizeof(held) / sizeof(held[0]); i++) {
        held[i] = __pool_acquire();
        TEST_ASSERT_NOT_NULL(held[i]);
        held[i]->cb = __cb_a;
    }
    /* All distinct. */
    for (size_t i = 0; i < sizeof(held) / sizeof(held[0]); i++) {
        for (size_t j = i + 1; j < sizeof(held) / sizeof(held[0]); j++) {
            TEST_ASSERT_NOT_EQUAL(held[i]->index, held[j]->index);
        }
    }
    TEST_ASSERT_TRUE(__block_count >= 2);

    const uint16_t blocks_after_growth = __block_count;
    for (size_t i = 0; i < sizeof(held) / sizeof(held[0]); i++) {
        __pool_release(held[i]);
    }

    /* Re-acquiring the same count must not grow the pool again. */
    for (size_t i = 0; i < sizeof(held) / sizeof(held[0]); i++) {
        held[i] = __pool_acquire();
        TEST_ASSERT_NOT_NULL(held[i]);
    }
    TEST_ASSERT_EQUAL_UINT16(blocks_after_growth, __block_count);

    for (size_t i = 0; i < sizeof(held) / sizeof(held[0]); i++) {
        __pool_release(held[i]);
    }
}

static esp_schedule_timer_handle_t __race_handle;
static osal_semaphore_handle_t __race_cancel_done;
static atomic_bool __race_cancel_started;
static bool __race_rearm_ok;

static void __race_cancel_task(void *arg)
{
    (void)arg;
    atomic_store(&__race_cancel_started, true);
    __timer_cancel(&__race_handle);
    osal_semaphore_give(__race_cancel_done);
    osal_task_delete(NULL);
}

/* Lets a cancel on another task reach the port, then re-arms as a repeating schedule does. */
static void __cb_rearm_during_cancel(void *priv_data)
{
    (void)priv_data;
    osal_task_handle_t task = NULL;
    if (osal_task_create(__race_cancel_task, "sched_cancel", 4096, NULL, 5, &task) != OSAL_ERR_OK) {
        return;
    }
    while (!atomic_load(&__race_cancel_started)) {
        osal_task_delay(osal_ticks_from_ms(1));
    }
    osal_task_delay(osal_ticks_from_ms(100));
    __race_rearm_ok = __timer_start(&__race_handle, 60, __cb_rearm_during_cancel, NULL);
}

/** A cancel from another task must not take the timer's task away from a callback that runs. */
void test_schedule_port_cancel_waits_for_running_callback(void)
{
    __port_test_setup();
    __race_cancel_done = osal_semaphore_create_binary();
    TEST_ASSERT_NOT_NULL(__race_cancel_done);
    atomic_store(&__race_cancel_started, false);
    __race_rearm_ok = false;
    __race_handle = NULL;

    TEST_ASSERT_TRUE(__timer_start(&__race_handle, 60, __cb_rearm_during_cancel, NULL));
    const uint32_t id = __pool_id_of((__port_timer_t *)__race_handle);

    /* Dispatch on this task, as the scheduler would. */
    __timer_trampoline((void *)(uintptr_t)id);

    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_semaphore_take(__race_cancel_done, osal_ticks_from_ms(2000)));
    osal_semaphore_delete(__race_cancel_done);
    TEST_ASSERT_TRUE_MESSAGE(__race_rearm_ok, "re-arm in the callback lost its task to a concurrent cancel");
    TEST_ASSERT_NULL(__race_handle);
    TEST_ASSERT_NULL(__pool_lookup(id));
}
