/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_rmaker_neo_common_prototypes.h"

#include <stdint.h>

#include "retry/esp_rmaker_backoff.h"
#include "osal_scheduler.h"
#include "osal_task.h"
#include "osal_ticks.h"
#include "osal_time_control.h"

#define BASE_DELAY_MS 10000
#define MAX_DELAY_MS  (5 * 60 * 1000)
#define SCHEDULER_SETTLE_MS    1000

static volatile int s_pending_fired;
static volatile int s_restart_fired;

static void __noop_retry_task(void *arg)
{
    (void)arg;
}

static void __count_task(void *arg)
{
    (*(volatile int *)arg)++;
}

/* Move virtual time forward, then let the virtual scheduler run the due tasks. */
static void __advance_s(time_t seconds)
{
    osal_time_control_advance_time(seconds);
    osal_task_delay(osal_ticks_from_ms(SCHEDULER_SETTLE_MS));
}

static esp_rmaker_backoff_retry_context_t __keep_retry_ctx(uint64_t current_ms)
{
    esp_rmaker_backoff_retry_context_t ctx = ESP_RMAKER_BACKOFF_DEFAULT_RETRY_CONTEXT();
    ctx.delay_ctx.delay_ms.current = current_ms;
    ctx.delay_ctx.delay_ms.max = MAX_DELAY_MS;
    ctx.delay_ctx.params.max_jitter_ms = 0;
    return ctx;
}

void test_backoff_retry_successful_schedule(void)
{
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_init());

    esp_rmaker_backoff_retry_context_t retry_ctx = ESP_RMAKER_BACKOFF_DEFAULT_RETRY_CONTEXT();
    retry_ctx.delay_ctx.delay_ms.current = 5;
    retry_ctx.delay_ctx.delay_ms.max = 100;
    retry_ctx.delay_ctx.params.exp_factor = 2;
    retry_ctx.delay_ctx.params.max_jitter_ms = 0;

    esp_rmaker_backoff_reset(&retry_ctx, 5);
    TEST_ASSERT_EQUAL(5, retry_ctx.delay_ctx.delay_ms.current);

    esp_rmaker_error_t err = esp_rmaker_backoff_retry(&retry_ctx, __noop_retry_task, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_NOT_NULL(retry_ctx.handle);
    TEST_ASSERT_EQUAL(10, retry_ctx.delay_ctx.delay_ms.current);

    err = esp_rmaker_backoff_retry(&retry_ctx, __noop_retry_task, NULL);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);

    // Cancel any scheduled tasks before cleanup
    if (retry_ctx.handle != NULL) {
        osal_scheduler_cancel_task(&retry_ctx.handle);
    }

    esp_rmaker_backoff_reset(&retry_ctx, 5);
    // Note: scheduler remains initialized for other tests
}

void test_backoff_fire_keep_retry_pending(void)
{
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_init());
    s_pending_fired = 0;

    esp_rmaker_backoff_retry_context_t ctx = __keep_retry_ctx(MAX_DELAY_MS);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_backoff_retry(&ctx, __count_task, (void *)&s_pending_fired));
    osal_scheduler_task_handle_t retry_handle = ctx.handle;

    for (int i = 0; i < 3; i++) {
        __advance_s(90);
        TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_backoff_fire_keep_retry(&ctx, BASE_DELAY_MS, __count_task, (void *)&s_pending_fired));
    }
    TEST_ASSERT_EQUAL_PTR(retry_handle, ctx.handle);
    TEST_ASSERT_EQUAL(0, s_pending_fired);

    __advance_s(31);
    TEST_ASSERT_EQUAL_MESSAGE(1, s_pending_fired, "updates moved the pending retry later");

    /* The timer fired, so the next update arms it again with the backoff delay */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_backoff_fire_keep_retry(&ctx, BASE_DELAY_MS, __count_task, (void *)&s_pending_fired));
    __advance_s(290);
    TEST_ASSERT_EQUAL(1, s_pending_fired);
    __advance_s(15);
    TEST_ASSERT_EQUAL(2, s_pending_fired);

    esp_rmaker_backoff_reset(&ctx, BASE_DELAY_MS);
}

void test_backoff_fire_keep_retry_restarts_delay(void)
{
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_scheduler_init());
    s_restart_fired = 0;

    esp_rmaker_backoff_retry_context_t ctx = __keep_retry_ctx(BASE_DELAY_MS);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_backoff_fire_keep_retry(&ctx, BASE_DELAY_MS, __count_task, (void *)&s_restart_fired));
    __advance_s(6);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_backoff_fire_keep_retry(&ctx, BASE_DELAY_MS, __count_task, (void *)&s_restart_fired));

    __advance_s(6);
    TEST_ASSERT_EQUAL_MESSAGE(0, s_restart_fired, "the second update did not restart the delay");
    __advance_s(6);
    TEST_ASSERT_EQUAL(1, s_restart_fired);

    esp_rmaker_backoff_reset(&ctx, BASE_DELAY_MS);
}
