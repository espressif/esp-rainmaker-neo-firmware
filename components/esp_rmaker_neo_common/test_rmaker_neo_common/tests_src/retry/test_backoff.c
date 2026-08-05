/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "test_rmng_common_prototypes.h"

#include <stdint.h>

#include "retry/esp_rmaker_backoff.h"
#include "osal_scheduler.h"

static void __noop_retry_task(void *arg)
{
    (void)arg;
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
