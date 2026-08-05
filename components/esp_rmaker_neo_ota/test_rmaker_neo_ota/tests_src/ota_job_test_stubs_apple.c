/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_job_test_stubs_apple.c
 * @brief Stub implementations for OTA Jobs FSM tests on macOS (no linker --wrap).
 *
 * Defines the real symbol names so they override the library implementations
 * when this object is linked first (same idea as socket_mocks on Apple).
 */

#include "esp_rmaker_error_types.h"
#include "esp_rmaker_work_queue.h"
#include "osal_event_loop.h"
#include "osal_ticks.h"
#include "retry/esp_rmaker_backoff.h"

#define _GNU_SOURCE // Required for RTLD_NEXT on some Linux systems
#include <dlfcn.h> // For dlsym
#include <stdbool.h>

extern bool TEST_OTA_JOBS_DO_NOT_WRAP;
extern int g_test_work_queue_fail_countdown;
extern const void *g_test_last_backoff_ctx;
extern const void *g_test_last_backoff_task;
extern int g_test_backoff_retry_calls;

typedef esp_rmaker_error_t (*esp_rmaker_work_queue_add_task_fn)(esp_rmaker_work_fn_t work_fn, void *priv_data);
typedef osal_err_t (*osal_event_post_fn)(osal_event_base_t event_base, int32_t event_id,
        void *event_data, size_t event_data_size,
        osal_tick_type_t ticks_to_wait);
typedef esp_rmaker_error_t (*backoff_retry_fn)(esp_rmaker_backoff_retry_context_t *p_retry_context,
        osal_scheduler_task_t task, void *arg);

esp_rmaker_error_t esp_rmaker_work_queue_add_task(esp_rmaker_work_fn_t work_fn, void *priv_data)
{
    if (TEST_OTA_JOBS_DO_NOT_WRAP) {
        esp_rmaker_work_queue_add_task_fn real_fn = (esp_rmaker_work_queue_add_task_fn)dlsym(RTLD_NEXT, "esp_rmaker_work_queue_add_task");
        if (real_fn != NULL) {
            return real_fn(work_fn, priv_data);
        }
    }
    if (g_test_work_queue_fail_countdown > 0) {
        g_test_work_queue_fail_countdown--;
        return ESP_RMAKER_FAIL;
    }
    (void)work_fn;
    (void)priv_data;
    return ESP_RMAKER_OK;
}

osal_err_t osal_event_post(osal_event_base_t event_base, int32_t event_id,
                           void *event_data, size_t event_data_size,
                           osal_tick_type_t ticks_to_wait)
{
    if (TEST_OTA_JOBS_DO_NOT_WRAP) {
        osal_event_post_fn real_fn = (osal_event_post_fn)dlsym(RTLD_NEXT, "osal_event_post");
        if (real_fn != NULL) {
            return real_fn(event_base, event_id, event_data, event_data_size, ticks_to_wait);
        }
    }
    (void)event_base;
    (void)event_id;
    (void)event_data;
    (void)event_data_size;
    (void)ticks_to_wait;
    return OSAL_ERR_OK;
}

esp_rmaker_error_t esp_rmaker_backoff_retry(esp_rmaker_backoff_retry_context_t *p_retry_context,
        osal_scheduler_task_t task, void *arg)
{
    if (TEST_OTA_JOBS_DO_NOT_WRAP) {
        backoff_retry_fn real_fn = (backoff_retry_fn)dlsym(RTLD_NEXT, "esp_rmaker_backoff_retry");
        if (real_fn != NULL && p_retry_context != NULL && task != NULL) {
            return real_fn(p_retry_context, task, arg);
        }
    }
    g_test_last_backoff_ctx = (const void *)p_retry_context;
    g_test_last_backoff_task = (const void *)task;
    g_test_backoff_retry_calls++;
    (void)arg;
    return ESP_RMAKER_OK;
}
