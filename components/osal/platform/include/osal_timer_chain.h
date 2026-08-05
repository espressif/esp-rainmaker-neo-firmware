/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_timer_chain.h
 * @brief Decision logic for a delay armed across several timer periods.
 *
 * FreeRTOS software timers take a 32-bit ``TickType_t``, so a delay beyond
 * ``portMAX_DELAY / 2`` is armed one segment at a time and re-armed from the
 * expiry callback until the whole delay has elapsed.
 *
 * Segments are derived from where the delay *ends*, never from how much is
 * left: an absolute target means the callback computes from the clock instead
 * of writing a balance back, so a concurrent reset cannot be clobbered, and a
 * periodic cadence measured from the epoch cannot drift.
 *
 * Free of FreeRTOS and ESP types, so it is unit-testable on any build.
 */

#ifndef __OSAL_TIMER_CHAIN_H__
#define __OSAL_TIMER_CHAIN_H__

#include <stdint.h>
#include <stdbool.h>

#include "osal_tick_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief What an expiry callback should do next.
 */
typedef struct {
    bool run_task;      /**< The full delay has elapsed: run the user task. */
    bool rearm;         /**< The timer must be armed for ::arm_ticks. */
    uint64_t arm_ticks; /**< Period to arm, in ``[1, max_ticks]``. Only set when ::rearm. */
} osal_timer_chain_step_t;

/**
 * @brief First periodic boundary strictly after @p now_ticks.
 *
 * Pure function of epoch and period, so nothing advances per cycle and the
 * cadence cannot drift.
 *
 * @param[in] epoch_ticks  Tick of the first fire.
 * @param[in] period_ticks Full period in ticks; 0 yields @p epoch_ticks.
 * @param[in] now_ticks    Current tick.
 *
 * @return The epoch when @p now_ticks precedes it, else the next boundary after it.
 */
static inline uint64_t osal_timer_chain_boundary(uint64_t epoch_ticks, uint64_t period_ticks, uint64_t now_ticks)
{
    if (period_ticks == 0 || now_ticks < epoch_ticks) {
        return epoch_ticks;
    }
    return epoch_ticks + (((now_ticks - epoch_ticks) / period_ticks) + 1) * period_ticks;
}

/**
 * @brief Decide what to do when a timer in a (possibly split) delay expires.
 *
 * @param[in] now_ticks      Current tick, from a monotonic 64-bit clock.
 * @param[in] deadline_ticks Absolute tick the delay targets. For a periodic
 *                           timer this is the *first* fire, i.e. the epoch the
 *                           later boundaries are measured from.
 * @param[in] total_ticks    Full delay in ticks; one whole period when periodic.
 * @param[in] segment_start  Tick the segment that just expired was armed at.
 *                           Periodic only, to identify the boundary it aimed
 *                           at. Must be the recorded arm time - deriving it
 *                           from @p now_ticks makes a late callback look like a
 *                           late arm, and skips the boundary.
 * @param[in] is_periodic    True to restart the delay after it elapses.
 * @param[in] max_ticks      Largest period a single arm may use.
 * @param[in] slack_ticks    Treat a target this close as reached; absorbs a
 *                           clock that reads a tick short of the timer's own.
 *
 * @return The step to take. A delay that fits one period returns
 *         ``{run_task = true, rearm = false}``: the platform timer already
 *         waited it out, and an auto-reloading one has re-armed itself.
 */
static inline osal_timer_chain_step_t osal_timer_chain_next(uint64_t now_ticks,
        uint64_t deadline_ticks,
        uint64_t total_ticks,
        uint64_t segment_start,
        bool is_periodic,
        uint64_t max_ticks,
        uint64_t slack_ticks)
{
    osal_timer_chain_step_t step = { .run_task = true, .rearm = false, .arm_ticks = 0 };

    if (total_ticks <= max_ticks) {
        return step;
    }

    /* Asking from the segment's start, not from now, is what separates "a
     * segment ended inside the period" from "the period is up" - and it holds
     * however late the callback ran. A one-shot just aims at its deadline. */
    uint64_t target = is_periodic
                      ? osal_timer_chain_boundary(deadline_ticks, total_ticks, segment_start)
                      : deadline_ticks;

    /* Written as a subtraction rather than now + slack to keep it overflow-safe. */
    if (target > now_ticks && (target - now_ticks) > slack_ticks) {
        /* Still short of the target: arm the next segment and run nothing. */
        step.run_task = false;
        step.rearm = true;
        step.arm_ticks = osal_tick_period_clamp(target - now_ticks, max_ticks);
        return step;
    }

    if (is_periodic) {
        /* Auto-reload would reload the last *segment*, so arm the first segment
         * of the next period instead. */
        uint64_t next = osal_timer_chain_boundary(deadline_ticks, total_ticks, now_ticks + slack_ticks);
        step.rearm = true;
        step.arm_ticks = osal_tick_period_clamp(next - now_ticks, max_ticks);
    }

    /* A one-shot that reached its target needs nothing: it does not reload. */
    return step;
}

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_TIMER_CHAIN_H__ */
