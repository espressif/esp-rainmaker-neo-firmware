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
#include "test_rmng_prototypes.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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
