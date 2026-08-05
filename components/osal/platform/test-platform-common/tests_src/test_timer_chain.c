/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_timer_chain.c
 * @brief Tests for the split-delay decision logic in osal_timer_chain.h.
 *
 * This is the whole decision the FreeRTOS backend's expiry callback makes:
 * whether the delay has elapsed, and what to arm next when it has not.
 *
 * Chains are walked with small tick numbers so each arm can be asserted
 * exactly; the last test repeats one at the real cap. "Clock one tick short"
 * models a microsecond clock converted to ticks - why slack exists.
 */

#include "unity.h"
#include <stdint.h>
#include <stdbool.h>
#include "osal_timer_chain.h"

/* Largest single period the FreeRTOS scheduler backend arms (portMAX_DELAY / 2
 * for a 32-bit TickType_t). Hardcoded so the expectations below hold on POSIX
 * builds too, where portMAX_DELAY is not defined. */
#define TEST_MAX_TICKS ((uint64_t)(0xFFFFFFFFULL / 2))

/* Slack the backend uses: one tick. */
#define TEST_SLACK 1ULL

/* Small cap for the hand-walked chains, so arms can be checked one by one. */
#define TEST_SMALL_MAX 100ULL

/* See test_tick_math.c: Unity's 64-bit assertions need UNITY_SUPPORT_64, which
 * ESP-IDF gates behind CONFIG_UNITY_ENABLE_64BIT (default n), and when it is off
 * they always fail rather than fail to compile. Compare halves instead. */
#define TEST_ASSERT_EQUAL_TICKS(expected, actual)                                   \
    do {                                                                            \
        uint64_t __exp = (uint64_t)(expected);                                       \
        uint64_t __act = (uint64_t)(actual);                                         \
        TEST_ASSERT_EQUAL_UINT32((uint32_t)(__exp >> 32), (uint32_t)(__act >> 32));  \
        TEST_ASSERT_EQUAL_UINT32((uint32_t)__exp, (uint32_t)__act);                  \
    } while (0)

/* Within `tol` ticks either way. Used where a clock reading short of the timer's
 * own ticks legitimately shifts a fire by the slack; the point of those cases is
 * that the error stays bounded, not that it is zero. */
#define TEST_ASSERT_TICKS_NEAR(expected, actual, tol)                               \
    do {                                                                            \
        uint64_t __exp = (uint64_t)(expected);                                       \
        uint64_t __act = (uint64_t)(actual);                                         \
        uint64_t __diff = (__exp > __act) ? (__exp - __act) : (__act - __exp);        \
        TEST_ASSERT_TRUE_MESSAGE(__diff <= (uint64_t)(tol), "tick value out of tolerance"); \
    } while (0)

/**
 * @brief Walk a chain the way the expiry callback does, advancing a fake clock
 *        by each armed period.
 *
 * @param[in]  epoch       Tick the delay was armed at.
 * @param[in]  total       Full delay in ticks.
 * @param[in]  is_periodic Whether to keep going after the delay elapses.
 * @param[in]  max         Largest period one arm may use.
 * @param[in]  short_clock Report the clock one tick short of the truth, modelling
 *                         a converted microsecond clock.
 * @param[in]  want_fires  Stop once the task has run this many times.
 * @param[out] fire_ticks  Clock value at each fire; must hold @p want_fires.
 * @param[out] segments    Number of intermediate arms across the whole walk.
 *
 * @return Number of fires observed (may be short of @p want_fires if the walk
 *         hit its step ceiling, which the callers assert against).
 */
static int walk_chain(uint64_t epoch, uint64_t total, bool is_periodic, uint64_t max,
                      bool short_clock, int want_fires, uint64_t *fire_ticks, int *segments)
{
    uint64_t now = epoch - total;              /* armed `total` ticks before the deadline */
    uint64_t deadline = epoch;
    uint64_t arm = osal_tick_period_clamp(total, max);
    /* Clock reading when the first arm was issued. Guarded because `now` can be
     * 0 here and the short clock must not wrap below it. */
    uint64_t seg_start = (short_clock && now > 0) ? now - 1 : now;
    int fires = 0;

    *segments = 0;
    for (int step = 0; step < 1000 && fires < want_fires; step++) {
        now += arm;
        uint64_t clock = short_clock ? now - 1 : now;

        osal_timer_chain_step_t s = osal_timer_chain_next(clock, deadline, total, seg_start,
                                    is_periodic, max, TEST_SLACK);
        seg_start = clock;                     /* whatever is armed below starts now */
        if (s.rearm) {
            TEST_ASSERT_TRUE(s.arm_ticks > 0);   /* forward progress guaranteed */
            TEST_ASSERT_TRUE(s.arm_ticks <= max); /* never exceeds one period */
            arm = s.arm_ticks;
        }
        if (!s.run_task) {
            TEST_ASSERT_TRUE(s.rearm);           /* not firing means still chaining */
            (*segments)++;
            continue;
        }
        fire_ticks[fires++] = now;
        if (!is_periodic) {
            /* A one-shot must not ask to be re-armed once it has fired. */
            TEST_ASSERT_FALSE(s.rearm);
            break;
        }
        TEST_ASSERT_TRUE(s.rearm);               /* periodic restarts the chain */
    }
    return fires;
}

void test_timer_chain_boundary(void)
{
    /* Before the epoch, the epoch itself is the next boundary. */
    TEST_ASSERT_EQUAL_TICKS(1000, osal_timer_chain_boundary(1000, 250, 0));
    TEST_ASSERT_EQUAL_TICKS(1000, osal_timer_chain_boundary(1000, 250, 999));

    /* Strictly after: standing exactly on a boundary yields the next one, which
     * is what lets the callback that just fired arm the following period. */
    TEST_ASSERT_EQUAL_TICKS(1250, osal_timer_chain_boundary(1000, 250, 1000));
    TEST_ASSERT_EQUAL_TICKS(1250, osal_timer_chain_boundary(1000, 250, 1001));
    TEST_ASSERT_EQUAL_TICKS(1250, osal_timer_chain_boundary(1000, 250, 1249));
    TEST_ASSERT_EQUAL_TICKS(1500, osal_timer_chain_boundary(1000, 250, 1250));

    /* Boundaries are measured from the epoch, so a late observation lands on the
     * true grid rather than period-after-now: this is the anti-drift property. */
    TEST_ASSERT_EQUAL_TICKS(3000, osal_timer_chain_boundary(1000, 250, 2999));
    TEST_ASSERT_EQUAL_TICKS(1000 + 250 * 4001, osal_timer_chain_boundary(1000, 250, 1000 + 250 * 4000));

    /* A zero period has no boundaries to step through; it must not divide by 0. */
    TEST_ASSERT_EQUAL_TICKS(1000, osal_timer_chain_boundary(1000, 0, 5000));
}

void test_timer_chain_no_split(void)
{
    /* A delay that fits one period was fully waited out by the platform timer,
     * so the task runs and nothing is armed - an auto-reloading periodic timer
     * has already re-armed itself. */
    /* The segment start is not read on this path: the delay fits one period. */
    osal_timer_chain_step_t s = osal_timer_chain_next(1000, 1000, TEST_SMALL_MAX, 900,
                                false, TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_TRUE(s.run_task);
    TEST_ASSERT_FALSE(s.rearm);

    s = osal_timer_chain_next(1000, 1000, TEST_SMALL_MAX, 900, true,
                              TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_TRUE(s.run_task);
    TEST_ASSERT_FALSE(s.rearm);

    /* True at the real cap too: exactly one period is not a split. */
    s = osal_timer_chain_next(TEST_MAX_TICKS, TEST_MAX_TICKS, TEST_MAX_TICKS, 0,
                              false, TEST_MAX_TICKS, TEST_SLACK);
    TEST_ASSERT_TRUE(s.run_task);
    TEST_ASSERT_FALSE(s.rearm);
}

void test_timer_chain_one_shot_split(void)
{
    /* 250 ticks with a 100-tick cap: arm 100, 100, then the 50-tick remainder.
     * A one-shot aims straight at its deadline, so the segment start is not read. */
    osal_timer_chain_step_t s = osal_timer_chain_next(1000, 1250, 250, 900,
                                false, TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_FALSE(s.run_task);
    TEST_ASSERT_TRUE(s.rearm);
    TEST_ASSERT_EQUAL_TICKS(TEST_SMALL_MAX, s.arm_ticks);

    s = osal_timer_chain_next(1200, 1250, 250, 1100, false, TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_FALSE(s.run_task);
    TEST_ASSERT_EQUAL_TICKS(50, s.arm_ticks);   /* the tail is not padded out */

    /* At the deadline it fires, and a one-shot asks for nothing more. */
    s = osal_timer_chain_next(1250, 1250, 250, 1200, false, TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_TRUE(s.run_task);
    TEST_ASSERT_FALSE(s.rearm);

    /* One tick short of the deadline is inside the slack: still fires. */
    s = osal_timer_chain_next(1249, 1250, 250, 1200, false, TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_TRUE(s.run_task);
    TEST_ASSERT_FALSE(s.rearm);

    /* Late is still fired, never re-armed into a second wait. */
    s = osal_timer_chain_next(1300, 1250, 250, 1200, false, TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_TRUE(s.run_task);
    TEST_ASSERT_FALSE(s.rearm);

    /* Walked end to end: exactly one fire, landing on the deadline. */
    uint64_t fires[1];
    int segments = 0;
    TEST_ASSERT_EQUAL_INT(1, walk_chain(1250, 250, false, TEST_SMALL_MAX, false, 1, fires, &segments));
    TEST_ASSERT_EQUAL_TICKS(1250, fires[0]);
    TEST_ASSERT_EQUAL_INT(2, segments);
}

void test_timer_chain_one_shot_split_boundaries(void)
{
    uint64_t fires[1];
    int segments = 0;

    /* One tick over the cap: the leftover tick is inside the slack, so the first
     * expiry fires rather than arming a one-tick segment for it. One tick early
     * out of 101 is the trade the slack buys; the alternative is a spurious wake
     * of the timer daemon for every rounding error. */
    TEST_ASSERT_EQUAL_INT(1, walk_chain(9999, TEST_SMALL_MAX + 1, false, TEST_SMALL_MAX,
                                        false, 1, fires, &segments));
    TEST_ASSERT_EQUAL_INT(0, segments);
    TEST_ASSERT_TICKS_NEAR(9999, fires[0], TEST_SLACK);

    /* An exact multiple of the cap divides evenly and fires once. */
    TEST_ASSERT_EQUAL_INT(1, walk_chain(9999, TEST_SMALL_MAX * 3, false, TEST_SMALL_MAX,
                                        false, 1, fires, &segments));
    TEST_ASSERT_EQUAL_TICKS(9999, fires[0]);
    TEST_ASSERT_EQUAL_INT(2, segments);

    /* A clock reading one tick short must not arm a spurious one-tick segment:
     * the slack absorbs it and the task still runs. */
    TEST_ASSERT_EQUAL_INT(1, walk_chain(9999, TEST_SMALL_MAX * 3, false, TEST_SMALL_MAX,
                                        true, 1, fires, &segments));
    TEST_ASSERT_EQUAL_INT(2, segments);
}

void test_timer_chain_periodic_no_drift(void)
{
    /* 250-tick period, 100-tick cap, four cycles, with the clock reading one
     * tick short every time. Every fire must stay on the epoch grid to within
     * the slack: the cadence is measured from the epoch, so the rounding error
     * stays bounded forever instead of accumulating a period at a time. */
    uint64_t fires[4];
    int segments = 0;
    const uint64_t epoch = 1000;
    const uint64_t period = 250;

    TEST_ASSERT_EQUAL_INT(4, walk_chain(epoch, period, true, TEST_SMALL_MAX, true, 4, fires, &segments));
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TICKS_NEAR(epoch + (uint64_t) i * period, fires[i], TEST_SLACK);
    }

    /* Restated as the gap between fires, which is what a caller observes. */
    for (int i = 1; i < 4; i++) {
        TEST_ASSERT_TICKS_NEAR(period, fires[i] - fires[i - 1], TEST_SLACK);
    }

    /* Same chain on an exact clock lands on the grid exactly. */
    TEST_ASSERT_EQUAL_INT(4, walk_chain(epoch, period, true, TEST_SMALL_MAX, false, 4, fires, &segments));
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_TICKS(epoch + (uint64_t) i * period, fires[i]);
    }
}

void test_timer_chain_periodic_restart(void)
{
    /* Standing on a boundary - reached by a tail segment armed at 1200 - a
     * periodic timer both runs the task and arms the next period's first segment,
     * since auto-reload would otherwise reload that tail. */
    osal_timer_chain_step_t s = osal_timer_chain_next(1250, 1000, 250, 1200, true,
                                TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_TRUE(s.run_task);
    TEST_ASSERT_TRUE(s.rearm);
    TEST_ASSERT_EQUAL_TICKS(TEST_SMALL_MAX, s.arm_ticks);

    /* A cycle observed one tick early still counts as elapsed (slack). */
    s = osal_timer_chain_next(1249, 1000, 250, 1200, true, TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_TRUE(s.run_task);
    TEST_ASSERT_TRUE(s.rearm);

    /* Regression: a callback that ran late must fire the boundary its segment was
     * aiming at, not skip the cycle. The segment start is recorded when the arm
     * is issued, so lateness cannot move it - 10 ticks late here... */
    s = osal_timer_chain_next(1260, 1000, 250, 1200, true, TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_TRUE(s.run_task);
    TEST_ASSERT_TRUE(s.rearm);

    /* ...and 350 ticks late here, past two whole boundaries. Deriving the start
     * from the clock instead would place it at 1550, whose next boundary (1750)
     * is still ahead, so the cycle would be skipped silently. The missed cycles
     * coalesce into one fire and the chain resumes on the epoch grid: the next
     * boundary after 1600 is 1750, 150 ticks out, capped to one 100-tick arm. */
    s = osal_timer_chain_next(1600, 1000, 250, 1200, true, TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_TRUE(s.run_task);
    TEST_ASSERT_TRUE(s.rearm);
    TEST_ASSERT_EQUAL_TICKS(TEST_SMALL_MAX, s.arm_ticks);

    /* A segment that ended inside the period does not fire: same clock distance
     * from a boundary as the 10-tick-late case above, but the segment started on
     * the boundary rather than short of it. */
    s = osal_timer_chain_next(1100, 1000, 250, 1000, true, TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_FALSE(s.run_task);
    TEST_ASSERT_TRUE(s.rearm);
    TEST_ASSERT_EQUAL_TICKS(TEST_SMALL_MAX, s.arm_ticks);

    /* And the segment just before a boundary arms only the remainder. */
    s = osal_timer_chain_next(1200, 1000, 250, 1100, true, TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_FALSE(s.run_task);
    TEST_ASSERT_EQUAL_TICKS(50, s.arm_ticks);

    /* A period that fits one arm needs no restart: auto-reload handles it. */
    s = osal_timer_chain_next(1250, 1000, TEST_SMALL_MAX, 1200, true,
                              TEST_SMALL_MAX, TEST_SLACK);
    TEST_ASSERT_TRUE(s.run_task);
    TEST_ASSERT_FALSE(s.rearm);
}

void test_timer_chain_real_cap(void)
{
    /* The production numbers: a 400-day delay at 100 Hz against the real cap
     * (248.6 days), so the chain is two arms. Repeated with a year-long periodic
     * delay to check the cadence holds at that scale. */
    const uint64_t day_ticks = 86400ULL * 100ULL;
    uint64_t fires[3];
    int segments = 0;

    const uint64_t total = 400ULL * day_ticks;
    TEST_ASSERT_TRUE(total > TEST_MAX_TICKS);
    TEST_ASSERT_EQUAL_INT(1, walk_chain(total, total, false, TEST_MAX_TICKS, true, 1, fires, &segments));
    TEST_ASSERT_TICKS_NEAR(total, fires[0], TEST_SLACK);
    TEST_ASSERT_EQUAL_INT(1, segments);

    const uint64_t year = 365ULL * day_ticks;
    TEST_ASSERT_EQUAL_INT(3, walk_chain(year, year, true, TEST_MAX_TICKS, true, 3, fires, &segments));
    TEST_ASSERT_TICKS_NEAR(year, fires[1] - fires[0], TEST_SLACK);
    TEST_ASSERT_TICKS_NEAR(year, fires[2] - fires[1], TEST_SLACK);

    /* Even at this scale the fires stay pinned to the epoch grid rather than
     * sliding by a segment per cycle, which is the drift the balance-based
     * countdown used to accumulate. */
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TICKS_NEAR(year + (uint64_t) i * year, fires[i], TEST_SLACK);
    }
}

void test_timer_chain_invariants(void)
{
    /* Sweep pseudorandom-but-deterministic chains, asserting the properties that
     * must hold rather than specific values: an arm is always a legal period, it
     * never overshoots the target, and the chain always converges on a fire. */
    uint32_t seed = 12345u;
    for (int i = 0; i < 2000; i++) {
        seed = seed * 1103515245u + 12345u;
        uint64_t max = 1 + (seed >> 20);                    /* 1 .. ~4096 */
        seed = seed * 1103515245u + 12345u;
        uint64_t total = 1 + (seed >> 16);                  /* 1 .. ~65536 */
        /* Advance again before drawing the mode: taking it from a bit of the
         * same word that produced `total` would tie periodic to the parity of
         * `total` and leave half the space unswept. */
        seed = seed * 1103515245u + 12345u;
        bool is_periodic = (seed & 0x10000u) != 0;
        uint64_t deadline = 1000000ULL + total;

        uint64_t now = 1000000ULL;
        uint64_t arm = osal_tick_period_clamp(total, max);
        uint64_t seg_start = now;
        int steps = 0;
        bool fired = false;

        while (steps++ < 200000 && !fired) {
            now += arm;
            osal_timer_chain_step_t s = osal_timer_chain_next(now, deadline, total, seg_start,
                                        is_periodic, max, TEST_SLACK);
            seg_start = now;
            if (s.rearm) {
                TEST_ASSERT_TRUE(s.arm_ticks >= 1);
                TEST_ASSERT_TRUE(s.arm_ticks <= max);
                arm = s.arm_ticks;
            }
            if (s.run_task) {
                fired = true;
                /* Fired within slack of the target, never early beyond it. */
                TEST_ASSERT_TRUE(now + TEST_SLACK >= deadline);
            } else {
                /* Not firing means the target is still ahead of the clock. */
                TEST_ASSERT_TRUE(s.rearm);
                TEST_ASSERT_TRUE(now < deadline);
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(fired, "chain failed to converge on a fire");
    }
}
