/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_ota_jobs.c
 * @brief Unit tests for OTA Jobs FSM (ota_jobs.c)
 *
 * Includes ota_jobs.c to test static state handlers and FSM behaviour.
 */

#include "unity.h"
#include "test_rmng_ota_prototypes.h"

#include <string.h>
#include <stdbool.h>

#include "esp_rmaker_error_types.h"
#include "esp_rmaker_work_queue.h"
#include "osal_event_loop.h"
#include "osal_scheduler.h"
#include "esp_rmaker_ota_error_reasons.h"
#define RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE (RMAKER_OTA_JOBS_TEST_WRAP_LINKER || RMAKER_OTA_JOBS_TEST_WRAP_DL_LIB)
#if RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
#include "retry/esp_rmaker_backoff.h"
#endif
#include "ota_jobs.h"

bool TEST_OTA_JOBS_DO_NOT_WRAP = true;

/* Fault-injection: when >0, the next `g_test_work_queue_fail_countdown` calls
 * to __wrap_esp_rmaker_work_queue_add_task return ESP_RMAKER_FAIL instead of
 * OK. Lets tests verify FSM handler escalation on enqueue failure. */
int g_test_work_queue_fail_countdown = 0;

/* Observability for the stubbed esp_rmaker_backoff_retry: records the last (context, task)
 * it was asked to schedule and a running call count. Lets tests assert that a
 * dropped terminal event was routed to the backoff scheduler with the right
 * context/callback. Only updated on the stub (non-real) path. */
const void *g_test_last_backoff_ctx = NULL;
const void *g_test_last_backoff_task = NULL;
int g_test_backoff_retry_calls = 0;

/**
 * Strategy A (Linux/ESP): linker --wrap; provide __wrap_* that get called instead of real symbols.
 * Strategy B (Apple): stub object with real symbol names linked first (see ota_job_test_stubs_apple.c).
 */
#if RMAKER_OTA_JOBS_TEST_WRAP_LINKER
/* Linker provides __real_* when --wrap=<symbol> is used; declare for compiler. */
extern esp_rmaker_error_t __real_esp_rmaker_work_queue_add_task(esp_rmaker_work_fn_t work_fn, void *priv_data);
extern osal_err_t __real_osal_event_post(osal_event_base_t event_base, int32_t event_id,
        void *event_data, size_t event_data_size,
        osal_tick_type_t ticks_to_wait);
extern osal_err_t __real_osal_scheduler_schedule_task(osal_scheduler_task_handle_t *handle, uint64_t delay_ms, osal_scheduler_task_t task, void *arg);
extern osal_err_t __real_osal_scheduler_reset_timer(osal_scheduler_task_handle_t handle, uint64_t delay_ms);
extern esp_rmaker_error_t __real_esp_rmaker_backoff_retry(esp_rmaker_backoff_retry_context_t *p_retry_context,
        osal_scheduler_task_t task, void *arg);

static int g_test_use_real_backoff = 0;

esp_rmaker_error_t __wrap_esp_rmaker_work_queue_add_task(esp_rmaker_work_fn_t work_fn, void *priv_data)
{
    if (TEST_OTA_JOBS_DO_NOT_WRAP) {
        return __real_esp_rmaker_work_queue_add_task(work_fn, priv_data);
    }
    if (g_test_work_queue_fail_countdown > 0) {
        g_test_work_queue_fail_countdown--;
        return ESP_RMAKER_FAIL;
    }
    (void)work_fn;
    (void)priv_data;
    return ESP_RMAKER_OK;
}

osal_err_t __wrap_osal_event_post(osal_event_base_t event_base, int32_t event_id,
                                  void *event_data, size_t event_data_size,
                                  osal_tick_type_t ticks_to_wait)
{
    if (TEST_OTA_JOBS_DO_NOT_WRAP) {
        return __real_osal_event_post(event_base, event_id, event_data, event_data_size, ticks_to_wait);
    }
    (void)event_base;
    (void)event_id;
    (void)event_data;
    (void)event_data_size;
    (void)ticks_to_wait;
    return OSAL_ERR_OK;
}

osal_err_t __wrap_osal_scheduler_schedule_task(osal_scheduler_task_handle_t *handle, uint64_t delay_ms, osal_scheduler_task_t task, void *arg)
{
    if (TEST_OTA_JOBS_DO_NOT_WRAP) {
        return __real_osal_scheduler_schedule_task(handle, delay_ms, task, arg);
    }
    (void)delay_ms;
    (void)task;
    (void)arg;
    if (handle != NULL) {
        *handle = (osal_scheduler_task_handle_t)1;
    }
    return OSAL_ERR_OK;
}

osal_err_t __wrap_osal_scheduler_reset_timer(osal_scheduler_task_handle_t handle, uint64_t delay_ms)
{
    if (TEST_OTA_JOBS_DO_NOT_WRAP) {
        return __real_osal_scheduler_reset_timer(handle, delay_ms);
    }
    (void)handle;
    (void)delay_ms;
    return OSAL_ERR_OK;
}

esp_rmaker_error_t __wrap_esp_rmaker_backoff_retry(esp_rmaker_backoff_retry_context_t *p_retry_context,
        osal_scheduler_task_t task, void *arg)
{
    if ((TEST_OTA_JOBS_DO_NOT_WRAP || g_test_use_real_backoff) && p_retry_context != NULL && task != NULL) {
        return __real_esp_rmaker_backoff_retry(p_retry_context, task, arg);
    }
    g_test_last_backoff_ctx = (const void *)p_retry_context;
    g_test_last_backoff_task = (const void *)task;
    g_test_backoff_retry_calls++;
    (void)arg;
    return ESP_RMAKER_OK;
}

#endif /* RMAKER_OTA_JOBS_TEST_WRAP_LINKER */

/* Include source to access static state handlers and g_ota_state_ctx (same TU) */
#include "ota_jobs.c"

static esp_rmaker_error_t mock_ota_cb(esp_rmaker_ota_handle_t handle,
                                      esp_rmaker_ota_data_t *ota_data,
                                      const esp_rmaker_ota_ft_ctx_t *ft_handler)
{
    (void)handle;
    (void)ota_data;
    (void)ft_handler;
    return ESP_RMAKER_OK;
}

static void reset_ctx_state(ota_job_state_t state)
{
    memset(&g_ota_state_ctx, 0, sizeof(g_ota_state_ctx));
    g_ota_state_ctx.state = state;
    g_ota_state_ctx.last_error = OTA_ERROR_NONE;
    g_ota_state_ctx.image_download.ota_cb = mock_ota_cb;
}

void rmng_ota_jobs_setUp(void)
{
#if RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_OTA_JOBS_DO_NOT_WRAP = false;
#endif
    g_test_last_backoff_ctx = NULL;
    g_test_last_backoff_task = NULL;
    g_test_backoff_retry_calls = 0;
    reset_ctx_state(OTA_JOB_STATE_UNINITIALIZED);
}

void rmng_ota_jobs_tearDown(void)
{
#if RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_OTA_JOBS_DO_NOT_WRAP = true;
    g_test_work_queue_fail_countdown = 0;
#endif
    rmaker_ota_status_test_set_publish_fail(0);
}

/* ========================================================================== */
/* State UNINITIALIZED: any event -> stay UNINITIALIZED                        */
/* ========================================================================== */

void test_fsm_uninitialized_any_event_returns_uninitialized(void)
{
    g_ota_state_ctx.state = OTA_JOB_STATE_UNINITIALIZED;

    /* Use events that do not trigger osal_event_post in on_event_ignored (e.g. not FETCH_REQUESTED) */
    ota_job_event_data_t ev_empty = { .event = OTA_JOB_EVENT_EMPTY_TRANSITION, .payload = NULL };
    bool should_free = true;
    ota_job_state_t next = handle_state_uninitialized(&ev_empty, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_UNINITIALIZED, next);

    ota_job_event_data_t ev_jobs = { .event = OTA_JOB_EVENT_JOBS_CHANGED, .payload = NULL };
    next = handle_state_uninitialized(&ev_jobs, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_UNINITIALIZED, next);

    next = handle_state_uninitialized(NULL, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_UNINITIALIZED, next);
}

/* ========================================================================== */
/* State NETWORK_INIT: only EMPTY_TRANSITION advances (to IDLE/REBOOT_CHECK); null/other -> stay */
/* ========================================================================== */

void test_fsm_network_init_null_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_NETWORK_INIT);
    bool should_free = true;

    ota_job_state_t next = handle_state_network_init(NULL, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_NETWORK_INIT, next);
}

void test_fsm_network_init_wrong_event_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_NETWORK_INIT);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_FETCH_REQUESTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_network_init(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_NETWORK_INIT, next);
}

/* ========================================================================== */
/* State REBOOT_CHECK: REBOOT_CHECK_REQUESTED / UPDATE_ACCEPTED / FINAL_STATUS_REPORT advance; null/other -> stay */
/* ========================================================================== */

void test_fsm_reboot_check_null_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_REBOOT_CHECK);
    bool should_free = true;

    ota_job_state_t next = handle_state_reboot_check(NULL, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_REBOOT_CHECK, next);
}

void test_fsm_reboot_check_other_event_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_REBOOT_CHECK);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_FETCH_REQUESTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_reboot_check(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_REBOOT_CHECK, next);
}

/* A recoverable reject (throttling, version mismatch, transient broker error) is retried
 * by the status manager, so the FSM must keep waiting for the acceptance. */
void test_fsm_reboot_check_recoverable_reject_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_REBOOT_CHECK);
    strcpy(g_ota_state_ctx.current_job.job_id, "job-recoverable");
    /* fixed_data == NULL means "recoverable"; see mqtt_on_update_rejected. */
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_UPDATE_REJECTED, .payload = NULL, .fixed_data = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_reboot_check(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_REBOOT_CHECK, next);
    TEST_ASSERT_EQUAL_STRING("job-recoverable", g_ota_state_ctx.current_job.job_id);
}

/* Regression: an unrecoverable reject (job deleted or already terminal) must release
 * REBOOT_CHECK. Without this the FSM parks here forever - every fetch request is
 * ignored in this state, so no later job is ever picked up - and the retained job ID
 * brings the device straight back here on the next boot. */
void test_fsm_reboot_check_unrecoverable_reject_returns_idle_and_clears_job(void)
{
    reset_ctx_state(OTA_JOB_STATE_REBOOT_CHECK);
    strcpy(g_ota_state_ctx.current_job.job_id, "job-gone");
    strcpy(g_ota_state_ctx.current_job.filetype, "ft");
    g_ota_state_ctx.current_job.final_status_reported = true;
    /* Non-NULL fixed_data means "unrecoverable"; see mqtt_on_update_rejected. */
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_UPDATE_REJECTED, .payload = NULL, .fixed_data = (void *) true };
    bool should_free = true;

    ota_job_state_t next = handle_state_reboot_check(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_IDLE, next);
    TEST_ASSERT_EQUAL_STRING("", g_ota_state_ctx.current_job.job_id);
    TEST_ASSERT_EQUAL_STRING("", g_ota_state_ctx.current_job.filetype);
    TEST_ASSERT_FALSE(g_ota_state_ctx.current_job.final_status_reported);
}

/* ========================================================================== */
/* State IDLE: FETCH_REQUESTED -> FETCHING_PENDING_JOBS, JOBS_CHANGED -> JOBS_CHANGED, other -> IDLE */
/* ========================================================================== */

void test_fsm_idle_fetch_requested_returns_fetching_pending_jobs(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("enqueue_event used; wrap required");
#else
    reset_ctx_state(OTA_JOB_STATE_IDLE);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_FETCH_REQUESTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_idle(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_FETCHING_PENDING_JOBS, next);
#endif
}

void test_fsm_idle_jobs_changed_returns_jobs_changed(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("enqueue_event used; wrap required");
#else
    reset_ctx_state(OTA_JOB_STATE_IDLE);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_JOBS_CHANGED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_idle(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_JOBS_CHANGED, next);
#endif
}

void test_fsm_idle_other_events_stay_idle(void)
{
    reset_ctx_state(OTA_JOB_STATE_IDLE);
    bool should_free = true;

    ota_job_event_data_t ev_empty = { .event = OTA_JOB_EVENT_EMPTY_TRANSITION, .payload = NULL };
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_IDLE, handle_state_idle(&ev_empty, &should_free));

    TEST_ASSERT_EQUAL(OTA_JOB_STATE_IDLE, handle_state_idle(NULL, &should_free));
}

/* ========================================================================== */
/* State JOBS_CHANGED: no payload -> IDLE; invalid payload -> ERROR            */
/* ========================================================================== */

void test_fsm_jobs_changed_no_payload_returns_idle(void)
{
    reset_ctx_state(OTA_JOB_STATE_JOBS_CHANGED);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_JOBS_CHANGED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_jobs_changed(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_IDLE, next);
}

void test_fsm_jobs_changed_wrong_event_stay_jobs_changed(void)
{
    reset_ctx_state(OTA_JOB_STATE_JOBS_CHANGED);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_FETCH_REQUESTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_jobs_changed(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_JOBS_CHANGED, next);
}

void test_fsm_jobs_changed_invalid_json_enters_error(void)
{
    reset_ctx_state(OTA_JOB_STATE_JOBS_CHANGED);
    static char bad_json[] = "{ not valid json ";
    ota_job_event_data_payload_t payload = { .data = bad_json, .len = sizeof(bad_json) - 1 };
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_JOBS_CHANGED, .payload = &payload };
    bool should_free = true;

    ota_job_state_t next = handle_state_jobs_changed(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_ERROR, next);
    TEST_ASSERT_EQUAL(OTA_ERROR_GET_PENDING_INVALID_FORMAT, g_ota_state_ctx.last_error);
}

/* ========================================================================== */
/* State FETCHING_PENDING_JOBS: only EMPTY_TRANSITION advances; other events ignored */
/* ========================================================================== */

void test_fsm_fetching_pending_jobs_non_empty_transition_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_FETCHING_PENDING_JOBS);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_FETCH_REQUESTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_fetching_pending_jobs(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_FETCHING_PENDING_JOBS, next);
}

void test_fsm_fetching_pending_jobs_null_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_FETCHING_PENDING_JOBS);
    bool should_free = true;

    ota_job_state_t next = handle_state_fetching_pending_jobs(NULL, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_FETCHING_PENDING_JOBS, next);
}

/* ========================================================================== */
/* State WAITING_FOR_PENDING_JOBS: PENDING_JOBS_ACCEPTED -> PENDING_JOBS_RECEIVED, REJECTED/TIMEOUT -> error set, other -> stay */
/* ========================================================================== */

void test_fsm_waiting_for_pending_jobs_accepted_returns_pending_jobs_received(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("enqueue_event used; wrap required");
#else
    reset_ctx_state(OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS);
    ota_job_event_data_payload_t payload = { .data = NULL, .len = 0 };
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_PENDING_JOBS_ACCEPTED, .payload = &payload };
    bool should_free = true;

    ota_job_state_t next = handle_state_waiting_for_pending_jobs(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_PENDING_JOBS_RECEIVED, next);
#endif
}

void test_fsm_waiting_for_pending_jobs_rejected_enters_error(void)
{
    reset_ctx_state(OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_PENDING_JOBS_REJECTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_waiting_for_pending_jobs(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS, next);
    TEST_ASSERT_EQUAL(OTA_ERROR_GET_PENDING_REJECTED, g_ota_state_ctx.last_error);
}

void test_fsm_waiting_for_pending_jobs_timeout_enters_error(void)
{
    reset_ctx_state(OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_TIMEOUT, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_waiting_for_pending_jobs(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS, next);
    TEST_ASSERT_EQUAL(OTA_ERROR_RETRY_WITH_BACKOFF, g_ota_state_ctx.last_error);
}

void test_fsm_waiting_for_pending_jobs_other_event_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_JOBS_CHANGED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_waiting_for_pending_jobs(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS, next);
}

void test_fsm_waiting_for_pending_jobs_null_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS);
    bool should_free = true;

    ota_job_state_t next = handle_state_waiting_for_pending_jobs(NULL, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS, next);
}

/* ========================================================================== */
/* State PENDING_JOBS_RECEIVED: no payload -> error set, wrong event -> stay   */
/* ========================================================================== */

void test_fsm_pending_jobs_received_no_payload_enters_error(void)
{
    reset_ctx_state(OTA_JOB_STATE_PENDING_JOBS_RECEIVED);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_PENDING_JOBS_ACCEPTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_pending_jobs_received(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_PENDING_JOBS_RECEIVED, next);
    TEST_ASSERT_EQUAL(OTA_ERROR_GET_PENDING_INVALID_FORMAT, g_ota_state_ctx.last_error);
}

void test_fsm_pending_jobs_received_wrong_event_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_PENDING_JOBS_RECEIVED);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_FETCH_REQUESTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_pending_jobs_received(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_PENDING_JOBS_RECEIVED, next);
}

void test_fsm_pending_jobs_received_null_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_PENDING_JOBS_RECEIVED);
    bool should_free = true;

    ota_job_state_t next = handle_state_pending_jobs_received(NULL, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_PENDING_JOBS_RECEIVED, next);
}

/* ========================================================================== */
/* State FETCHING_JOB_DOC: only EMPTY_TRANSITION advances; null/other -> stay   */
/* ========================================================================== */

void test_fsm_fetching_job_doc_null_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_FETCHING_JOB_DOC);
    bool should_free = true;

    ota_job_state_t next = handle_state_fetching_job_doc(NULL, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_FETCHING_JOB_DOC, next);
}

void test_fsm_fetching_job_doc_other_event_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_FETCHING_JOB_DOC);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_JOB_DOC_ACCEPTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_fetching_job_doc(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_FETCHING_JOB_DOC, next);
}

/* ========================================================================== */
/* State WAITING_FOR_JOB_DOC: JOB_DOC_ACCEPTED -> JOB_DOC_RECEIVED, REJECTED/TIMEOUT -> error, other -> stay */
/* ========================================================================== */

void test_fsm_waiting_for_job_doc_rejected_enters_error(void)
{
    reset_ctx_state(OTA_JOB_STATE_WAITING_FOR_JOB_DOC);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_JOB_DOC_REJECTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_waiting_for_job_doc(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_WAITING_FOR_JOB_DOC, next);
    TEST_ASSERT_EQUAL(OTA_ERROR_DESCRIBE_JOB_REJECTED, g_ota_state_ctx.last_error);
}

void test_fsm_waiting_for_job_doc_timeout_enters_error(void)
{
    reset_ctx_state(OTA_JOB_STATE_WAITING_FOR_JOB_DOC);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_TIMEOUT, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_waiting_for_job_doc(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_WAITING_FOR_JOB_DOC, next);
    TEST_ASSERT_EQUAL(OTA_ERROR_RETRY_WITH_BACKOFF, g_ota_state_ctx.last_error);
}

void test_fsm_waiting_for_job_doc_accepted_returns_job_doc_received(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("enqueue_event used; wrap required");
#else
    reset_ctx_state(OTA_JOB_STATE_WAITING_FOR_JOB_DOC);
    ota_job_event_data_payload_t payload = { .data = NULL, .len = 0 };
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_JOB_DOC_ACCEPTED, .payload = &payload };
    bool should_free = true;

    ota_job_state_t next = handle_state_waiting_for_job_doc(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_JOB_DOC_RECEIVED, next);
#endif
}

void test_fsm_waiting_for_job_doc_other_event_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_WAITING_FOR_JOB_DOC);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_FETCH_REQUESTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_waiting_for_job_doc(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_WAITING_FOR_JOB_DOC, next);
}

void test_fsm_waiting_for_job_doc_null_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_WAITING_FOR_JOB_DOC);
    bool should_free = true;

    ota_job_state_t next = handle_state_waiting_for_job_doc(NULL, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_WAITING_FOR_JOB_DOC, next);
}

/* ========================================================================== */
/* State JOB_DOC_RECEIVED: JOB_DOC_ACCEPTED with payload advances; null/wrong event -> stay */
/* ========================================================================== */

void test_fsm_job_doc_received_null_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_JOB_DOC_RECEIVED);
    bool should_free = true;

    ota_job_state_t next = handle_state_job_doc_received(NULL, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_JOB_DOC_RECEIVED, next);
}

void test_fsm_job_doc_received_wrong_event_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_JOB_DOC_RECEIVED);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_TIMEOUT, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_job_doc_received(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_JOB_DOC_RECEIVED, next);
}

/* ========================================================================== */
/* JOB_DOC_RECEIVED: jobId length guard (defensive against memcpy overflow)    */
/* of g_ota_state_ctx.current_job.job_id[JOBID_MAX_LENGTH + 1].                */
/* ========================================================================== */

/* 65 'A's: jobId one byte over the AWS-defined JOBS_JOBID_MAX_LENGTH (64). */
#define JOBID_OVER_LIMIT_LEN (JOBID_MAX_LENGTH + 1)

void test_fsm_job_doc_received_oversized_job_id_rejected(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("end-label uses enqueue_transition_event; wrap required");
#else
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());
    reset_ctx_state(OTA_JOB_STATE_JOB_DOC_RECEIVED);

    /* Pre-poison the job_id buffer with a sentinel that is *not* '\0' so a
     * stray memcpy/null-terminator past the guard would be detectable. */
    memset(g_ota_state_ctx.current_job.job_id, 'X', sizeof(g_ota_state_ctx.current_job.job_id));
    g_ota_state_ctx.current_job.job_id[sizeof(g_ota_state_ctx.current_job.job_id) - 1] = 'X';

    /* Build payload: {"execution":{"jobId":"<65 A's>","jobDocument":{},"versionNumber":1}} */
    char jobid_buf[JOBID_OVER_LIMIT_LEN + 1];
    memset(jobid_buf, 'A', JOBID_OVER_LIMIT_LEN);
    jobid_buf[JOBID_OVER_LIMIT_LEN] = '\0';

    char payload_buf[256];
    int n = snprintf(payload_buf, sizeof(payload_buf),
                     "{\"execution\":{\"jobId\":\"%s\",\"jobDocument\":{},\"versionNumber\":1}}",
                     jobid_buf);
    TEST_ASSERT_TRUE(n > 0 && n < (int)sizeof(payload_buf));

    ota_job_event_data_payload_t payload = { .data = payload_buf, .len = (size_t)n };
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_JOB_DOC_ACCEPTED, .payload = &payload };
    bool should_free = true;

    ota_job_state_t next = handle_state_job_doc_received(&ev, &should_free);

    /* Guard in handle_state_job_doc_received rejects oversize jobId before any memcpy into the
     * fixed-size current_job.job_id buffer. Sentinel must be intact. */
    for (size_t i = 0; i < sizeof(g_ota_state_ctx.current_job.job_id); ++i) {
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('X', g_ota_state_ctx.current_job.job_id[i],
                                       "oversized jobId must not be written into current_job.job_id");
    }
    TEST_ASSERT_FALSE_MESSAGE(g_ota_state_ctx.current_job.has_pending_job,
                              "rejection must leave has_pending_job unchanged");
    /* End-label posts EMPTY_TRANSITION -> next state is FETCHING_JOB_DOC. */
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_FETCHING_JOB_DOC, next);

    event_pool_deinit();
#endif
}

#if CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
static esp_rmaker_error_t test_custom_job_cb_noop(const char *doc, size_t len)
{
    (void)doc;
    (void)len;
    return ESP_RMAKER_OK;
}
#endif /* CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT */

void test_fsm_job_doc_received_shorter_job_id_terminated_correctly(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE || !CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT
    TEST_IGNORE_MESSAGE("custom-job branch + wrap required to reach memcpy");
#else
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());
    reset_ctx_state(OTA_JOB_STATE_JOB_DOC_RECEIVED);
    g_ota_state_ctx.custom_job_cb = test_custom_job_cb_noop;

    /* Simulate a stale prior jobId in the buffer ("abcdef", 6 chars) so a
     * shorter incoming jobId would leave a tail unless explicitly null-
     * terminated after memcpy. */
    const char *prev_id = "abcdef";
    size_t prev_len = strlen(prev_id);
    memcpy(g_ota_state_ctx.current_job.job_id, prev_id, prev_len);
    g_ota_state_ctx.current_job.job_id[prev_len] = '\0';
    TEST_ASSERT_EQUAL_STRING(prev_id, g_ota_state_ctx.current_job.job_id);

    /* Job document with a *shorter* jobId "1234" and no rmng_ota field -
     * parse_custom_fields_from_job_doc fails -> invalid_format -> custom path
     * -> memcpy + '\0' in the custom-job branch of handle_state_job_doc_received. */
    static const char payload_str[] =
        "{\"execution\":{\"jobId\":\"1234\",\"jobDocument\":{\"foo\":\"bar\"},\"versionNumber\":1}}";
    ota_job_event_data_payload_t payload = {
        .data = (void *)payload_str,
        .len = sizeof(payload_str) - 1,
    };
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_JOB_DOC_ACCEPTED, .payload = &payload };
    bool should_free = true;

    ota_job_state_t next = handle_state_job_doc_received(&ev, &should_free);

    TEST_ASSERT_EQUAL(OTA_JOB_STATE_CUSTOM_JOB_EXECUTION, next);
    /* "1234" must replace "abcdef" cleanly, NOT bleed into "1234ef". */
    TEST_ASSERT_EQUAL_STRING_MESSAGE("1234", g_ota_state_ctx.current_job.job_id,
                                     "shorter jobId must overwrite + null-terminate, not leave stale tail");
    TEST_ASSERT_EQUAL_UINT(4, strlen(g_ota_state_ctx.current_job.job_id));

    event_pool_deinit();
#endif
}

/* ========================================================================== */
/* Standard (non-custom-job) path: memcpy + null-terminator in the success     */
/* branch of handle_state_job_doc_received. Stub a filetype handler with no   */
/* version handlers so the version-comparison block is skipped, letting the   */
/* success path reach the memcpy.                                             */
/* ========================================================================== */

static esp_rmaker_error_t test_ft_dl_begin(esp_rmaker_ota_ft_download_handle_t *h, size_t s)
{
    (void)h;
    (void)s;
    return ESP_RMAKER_OK;
}
static esp_rmaker_error_t test_ft_dl_chunk(esp_rmaker_ota_ft_download_handle_t h, const uint8_t *d, size_t s, size_t o)
{
    (void)h;
    (void)d;
    (void)s;
    (void)o;
    return ESP_RMAKER_OK;
}
static esp_rmaker_error_t test_ft_dl_complete(esp_rmaker_ota_ft_download_handle_t h, bool ok)
{
    (void)h;
    (void)ok;
    return ESP_RMAKER_OK;
}
static esp_rmaker_error_t test_ft_get_hash(esp_rmaker_ota_ft_download_handle_t h, uint8_t hash[32])
{
    (void)h;
    (void)hash;
    return ESP_RMAKER_OK;
}
static esp_rmaker_error_t test_ft_integ(esp_rmaker_ota_ft_download_handle_t h)
{
    (void)h;
    return ESP_RMAKER_OK;
}
static esp_rmaker_error_t test_ft_post_dl(esp_rmaker_ota_ft_download_handle_t h, bool ok, bool *r)
{
    (void)h;
    (void)ok;
    if (r) {
        *r = false;
    } return ESP_RMAKER_OK;
}

static const esp_rmaker_ota_ft_ctx_t test_ft_ctx_no_version = {
    .get_version = NULL,
    .version_to_uint32 = NULL,
    .on_download_begin = test_ft_dl_begin,
    .on_download_chunk = test_ft_dl_chunk,
    .on_download_complete = test_ft_dl_complete,
    .get_sha256_hash = test_ft_get_hash,
    .perform_integration_check = test_ft_integ,
    .on_post_download_checks_complete = test_ft_post_dl,
};

static const esp_rmaker_ota_ft_ctx_t *test_ft_lookup(const char *t, size_t l)
{
    (void)t;
    (void)l;
    return &test_ft_ctx_no_version;
}

void test_fsm_job_doc_received_standard_path_shorter_job_id_terminated_correctly(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("standard success path enqueues; wrap required");
#else
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());
    reset_ctx_state(OTA_JOB_STATE_JOB_DOC_RECEIVED);
    g_ota_state_ctx.filetype_handler_lookup = test_ft_lookup;

    /* Simulate a stale prior jobId in the buffer ("abcdef", 6 chars). */
    const char *prev_id = "abcdef";
    size_t prev_len = strlen(prev_id);
    memcpy(g_ota_state_ctx.current_job.job_id, prev_id, prev_len);
    g_ota_state_ctx.current_job.job_id[prev_len] = '\0';
    TEST_ASSERT_EQUAL_STRING(prev_id, g_ota_state_ctx.current_job.job_id);

    /* Full afr_ota + rmng_ota payload that drives the success path. The
     * shorter "1234" replaces "abcdef" via memcpy in the success branch of
     * handle_state_job_doc_received, with the trailing null-terminator
     * truncating the stale tail. */
    static const char payload_str[] =
        "{\"execution\":{"
        "\"jobId\":\"1234\","
        "\"jobDocument\":{"
        "\"afr_ota\":{"
        "\"protocols\":[\"MQTT\"],"
        "\"streamname\":\"s\","
        "\"files\":[{"
        "\"filepath\":\"f\","
        "\"filesize\":1,"
        "\"fileid\":0,"
        "\"certfile\":\"c\","
        "\"sig-sha256-ecdsa\":\"sig\""
        "}]"
        "},"
        "\"rmng_ota\":{\"filetype\":\"test\"}"
        "},"
        "\"versionNumber\":1"
        "}}";
    ota_job_event_data_payload_t payload = {
        .data = (void *)payload_str,
        .len = sizeof(payload_str) - 1,
    };
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_JOB_DOC_ACCEPTED, .payload = &payload };
    bool should_free = true;

    ota_job_state_t next = handle_state_job_doc_received(&ev, &should_free);

    /* Standard success: ends via enqueue_transition_event -> FETCHING_JOB_DOC. */
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_FETCHING_JOB_DOC, next);
    TEST_ASSERT_TRUE_MESSAGE(g_ota_state_ctx.current_job.has_pending_job,
                             "standard success path must mark job as pending");
    /* "1234" must replace "abcdef", not bleed into "1234ef". */
    TEST_ASSERT_EQUAL_STRING_MESSAGE("1234", g_ota_state_ctx.current_job.job_id,
                                     "standard-path shorter jobId must be null-terminated");
    TEST_ASSERT_EQUAL_UINT(4, strlen(g_ota_state_ctx.current_job.job_id));

    /* Free heap allocations made by set_ota_data_from_job_doc. */
    clear_ota_data();
    event_pool_deinit();
#endif
}

void test_fsm_job_doc_received_missing_job_id_rejected(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("end-label uses enqueue_transition_event; wrap required");
#else
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());
    reset_ctx_state(OTA_JOB_STATE_JOB_DOC_RECEIVED);

    memset(g_ota_state_ctx.current_job.job_id, 'X', sizeof(g_ota_state_ctx.current_job.job_id));
    g_ota_state_ctx.current_job.job_id[sizeof(g_ota_state_ctx.current_job.job_id) - 1] = 'X';

    /* Payload has no execution.jobId - Jobs_GetJobId returns 0 -> guard rejects. */
    static const char payload_str[] =
        "{\"execution\":{\"jobDocument\":{},\"versionNumber\":1}}";
    ota_job_event_data_payload_t payload = { .data = (void *)payload_str, .len = sizeof(payload_str) - 1 };
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_JOB_DOC_ACCEPTED, .payload = &payload };
    bool should_free = true;

    ota_job_state_t next = handle_state_job_doc_received(&ev, &should_free);

    for (size_t i = 0; i < sizeof(g_ota_state_ctx.current_job.job_id); ++i) {
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('X', g_ota_state_ctx.current_job.job_id[i],
                                       "missing jobId must not write current_job.job_id");
    }
    TEST_ASSERT_FALSE(g_ota_state_ctx.current_job.has_pending_job);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_FETCHING_JOB_DOC, next);

    event_pool_deinit();
#endif
}

/* ========================================================================== */
/* State JOB_EXECUTION: download events advance; null/other -> stay             */
/* ========================================================================== */

void test_fsm_job_execution_null_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_JOB_EXECUTION);
    bool should_free = true;

    ota_job_state_t next = handle_state_job_execution(NULL, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_JOB_EXECUTION, next);
}

void test_fsm_job_execution_other_event_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_JOB_EXECUTION);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_JOBS_CHANGED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_job_execution(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_JOB_EXECUTION, next);
}

/* ========================================================================== */
/* State POST_DOWNLOAD: final status / update events advance; null/other -> stay */
/* ========================================================================== */

void test_fsm_post_download_null_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_POST_DOWNLOAD);
    bool should_free = true;

    ota_job_state_t next = handle_state_post_download(NULL, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_POST_DOWNLOAD, next);
}

void test_fsm_post_download_other_event_stays(void)
{
    reset_ctx_state(OTA_JOB_STATE_POST_DOWNLOAD);
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_FETCH_REQUESTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_post_download(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_POST_DOWNLOAD, next);
}

/* ========================================================================== */
/* State ERROR: RECOVERY_REQUESTED uses recovery context; ERROR_OCCURRED stays; other ignored */
/* ========================================================================== */

void test_fsm_error_recovery_requested_transitions_to_recovery_state(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("recovery path uses enqueue_event; wrap required");
#else
    reset_ctx_state(OTA_JOB_STATE_ERROR);
    g_ota_state_ctx.last_error = OTA_ERROR_RETRY_WITH_BACKOFF;
    g_ota_state_ctx.recovery.state = OTA_JOB_STATE_IDLE;
    g_ota_state_ctx.recovery.event = OTA_JOB_EVENT_FETCH_REQUESTED;
    g_ota_state_ctx.recovery.event_data = NULL;

    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_RECOVERY_REQUESTED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_error(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_IDLE, next);
#endif
}

void test_fsm_error_null_stays_error(void)
{
    reset_ctx_state(OTA_JOB_STATE_ERROR);
    g_ota_state_ctx.last_error = OTA_ERROR_GET_PENDING_REJECTED;
    bool should_free = true;

    ota_job_state_t next = handle_state_error(NULL, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_ERROR, next);
}

void test_fsm_error_error_occurred_stays_error(void)
{
    reset_ctx_state(OTA_JOB_STATE_ERROR);
    g_ota_state_ctx.last_error = OTA_ERROR_SUBSCRIPTION_FAILED; //arbitrary error
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_ERROR_OCCURRED, .payload = NULL };
    bool should_free = true;

    ota_job_state_t next = handle_state_error(&ev, &should_free);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_ERROR, next);
}

void test_fsm_error_other_events_stay_error(void)
{
    reset_ctx_state(OTA_JOB_STATE_ERROR);
    g_ota_state_ctx.last_error = OTA_ERROR_GET_PENDING_REJECTED;

    ota_job_event_data_t ev_fetch = { .event = OTA_JOB_EVENT_FETCH_REQUESTED, .payload = NULL };
    bool should_free = true;
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_ERROR, handle_state_error(&ev_fetch, &should_free));
}

/* ========================================================================== */
/* Final status publish failure: MQTT mock forces fail; recovery state asserted */
/* ========================================================================== */

void test_fsm_reboot_check_final_status_publish_fail_recovery_state_reboot_check(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("post_error_event uses event loop; wrap required");
#else
    reset_ctx_state(OTA_JOB_STATE_REBOOT_CHECK);
    static esp_rmaker_ota_status_details_t final_status_details;
    esp_rmaker_ota_status_details_fill_succeeded(&final_status_details, NULL, "1.0.0");
    ota_job_event_data_payload_t payload = {
        .data = (void *) &final_status_details,
        .len = sizeof(final_status_details),
    };
    ota_job_event_data_t ev = {
        .event = OTA_JOB_EVENT_FINAL_STATUS_REPORT_REQUESTED,
        .payload = &payload,
    };
    bool should_free = true;

    rmaker_ota_status_test_set_publish_fail(1);
    ota_job_state_t next = handle_state_reboot_check(&ev, &should_free);
    rmaker_ota_status_test_set_publish_fail(0);

    TEST_ASSERT_EQUAL(OTA_JOB_STATE_ERROR, next);
    TEST_ASSERT_EQUAL(OTA_ERROR_RETRY_WITH_BACKOFF, g_ota_state_ctx.last_error);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_REBOOT_CHECK, g_ota_state_ctx.recovery.state);
#endif
}

void test_fsm_post_download_final_status_publish_fail_recovery_state_idle(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("post_error_event uses event loop; wrap required");
#else
    reset_ctx_state(OTA_JOB_STATE_POST_DOWNLOAD);
    static esp_rmaker_ota_status_details_t final_status_details;
    esp_rmaker_ota_status_details_fill_succeeded(&final_status_details, NULL, "1.0.0");
    ota_job_event_data_payload_t payload = {
        .data = (void *) &final_status_details,
        .len = sizeof(final_status_details),
    };
    ota_job_event_data_t ev = {
        .event = OTA_JOB_EVENT_FINAL_STATUS_REPORT_REQUESTED,
        .payload = &payload,
    };
    bool should_free = true;

    rmaker_ota_status_test_set_publish_fail(1);
    ota_job_state_t next = handle_state_post_download(&ev, &should_free);
    rmaker_ota_status_test_set_publish_fail(0);

    TEST_ASSERT_EQUAL(OTA_JOB_STATE_ERROR, next);
    TEST_ASSERT_EQUAL(OTA_ERROR_RETRY_WITH_BACKOFF, g_ota_state_ctx.last_error);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_IDLE, g_ota_state_ctx.recovery.state);
#endif
}

void test_fsm_post_download_queued_final_status_publish_fail_recovery_state_post_download(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("post_error_event uses event loop; wrap required");
#else
    reset_ctx_state(OTA_JOB_STATE_POST_DOWNLOAD);
    g_ota_state_ctx.current_job.final_status_queued = true;
    esp_rmaker_ota_status_details_fill_succeeded(&g_ota_state_ctx.current_job.current_status_details, NULL, "1.0.0");
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_EMPTY_TRANSITION, .payload = NULL };
    bool should_free = true;

    rmaker_ota_status_test_set_publish_fail(1);
    ota_job_state_t next = handle_state_post_download(&ev, &should_free);
    rmaker_ota_status_test_set_publish_fail(0);

    TEST_ASSERT_EQUAL(OTA_JOB_STATE_ERROR, next);
    TEST_ASSERT_EQUAL(OTA_ERROR_RETRY_WITH_BACKOFF, g_ota_state_ctx.last_error);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_POST_DOWNLOAD, g_ota_state_ctx.recovery.state);
#endif
}

void test_fsm_final_status_publish_fail_consecutive_increments_backoff_delay(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_LINKER
    TEST_IGNORE_MESSAGE("__real_esp_rmaker_backoff_retry and scheduler wraps required (linker wrap only)");
#else
    static esp_rmaker_ota_status_details_t final_status_details;
    esp_rmaker_ota_status_details_fill_succeeded(&final_status_details, NULL, "1.0.0");
    ota_job_event_data_payload_t payload = {
        .data = (void *) &final_status_details,
        .len = sizeof(final_status_details),
    };
    ota_job_event_data_t ev = {
        .event = OTA_JOB_EVENT_FINAL_STATUS_REPORT_REQUESTED,
        .payload = &payload,
    };
    bool should_free = true;

    rmaker_ota_jobs_test_reset_backoff();
    g_test_use_real_backoff = 1;

    reset_ctx_state(OTA_JOB_STATE_REBOOT_CHECK);
    rmaker_ota_status_test_set_publish_fail(1);
    (void)handle_state_reboot_check(&ev, &should_free);
    uint64_t delay_after_first = rmaker_ota_jobs_test_get_backoff_delay_ms();

    g_ota_state_ctx.state = OTA_JOB_STATE_REBOOT_CHECK;
    rmaker_ota_status_test_set_publish_fail(1);
    (void)handle_state_reboot_check(&ev, &should_free);
    uint64_t delay_after_second = rmaker_ota_jobs_test_get_backoff_delay_ms();

    g_test_use_real_backoff = 0;
    rmaker_ota_status_test_set_publish_fail(0);

    TEST_ASSERT_TRUE(delay_after_second > delay_after_first);
#endif
}

/* ========================================================================== */
/* Error path: enter_error_state sets last_error and recovery context          */
/* ========================================================================== */

void test_fsm_enter_error_sets_last_error_and_recovery(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("enter_error_state posts to event loop; wrap required");
#else
    reset_ctx_state(OTA_JOB_STATE_IDLE);
    enter_error_state(OTA_ERROR_SUBSCRIPTION_FAILED);
    TEST_ASSERT_EQUAL(OTA_ERROR_SUBSCRIPTION_FAILED, g_ota_state_ctx.last_error);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_NETWORK_INIT, g_ota_state_ctx.recovery.state);
    TEST_ASSERT_EQUAL(OTA_JOB_EVENT_EMPTY_TRANSITION, g_ota_state_ctx.recovery.event);

    reset_ctx_state(OTA_JOB_STATE_IDLE);
    enter_error_state(OTA_ERROR_RETRY_WITH_BACKOFF);
    TEST_ASSERT_EQUAL(OTA_ERROR_RETRY_WITH_BACKOFF, g_ota_state_ctx.last_error);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_IDLE, g_ota_state_ctx.recovery.state);
    TEST_ASSERT_EQUAL(OTA_JOB_EVENT_FETCH_REQUESTED, g_ota_state_ctx.recovery.event);
#endif
}

/* ========================================================================== */
/* Static event pool mechanics                                                */
/* ========================================================================== */

static bool is_in_event_pool(const ota_job_event_data_t *ptr)
{
    return ptr >= &g_event_pool_slots[0] && ptr < &g_event_pool_slots[OTA_JOB_EVENT_POOL_SIZE];
}

void test_event_pool_init_creates_queue_with_configured_size(void)
{
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    ota_job_event_data_t *slots[OTA_JOB_EVENT_POOL_SIZE];
    for (uint32_t i = 0; i < OTA_JOB_EVENT_POOL_SIZE; ++i) {
        slots[i] = event_pool_take();
        TEST_ASSERT_NOT_NULL(slots[i]);
        TEST_ASSERT_TRUE_MESSAGE(is_in_event_pool(slots[i]), "pool slot not within g_event_pool_slots range");
    }
    TEST_ASSERT_NULL_MESSAGE(event_pool_take(), "pool should be empty after draining all slots");

    for (uint32_t i = 0; i < OTA_JOB_EVENT_POOL_SIZE; ++i) {
        TEST_ASSERT_TRUE(event_pool_return(slots[i]));
    }
    event_pool_deinit();
}

void test_event_pool_return_recycles_slot(void)
{
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    ota_job_event_data_t *slot = event_pool_take();
    TEST_ASSERT_NOT_NULL(slot);
    slot->event = OTA_JOB_EVENT_FETCH_REQUESTED;

    TEST_ASSERT_TRUE(event_pool_return(slot));
    /* After return, slot memory is zeroed. */
    TEST_ASSERT_EQUAL(0, slot->event);
    TEST_ASSERT_NULL(slot->payload);

    /* Subsequent take must produce an in-pool pointer. */
    ota_job_event_data_t *again = event_pool_take();
    TEST_ASSERT_NOT_NULL(again);
    TEST_ASSERT_TRUE(is_in_event_pool(again));

    (void)event_pool_return(again);
    event_pool_deinit();
}

void test_event_pool_return_rejects_non_pool_pointer(void)
{
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    ota_job_event_data_t stack_event = { .event = OTA_JOB_EVENT_EMPTY_TRANSITION, .payload = NULL };
    TEST_ASSERT_FALSE(event_pool_return(&stack_event));
    TEST_ASSERT_FALSE(event_pool_return(NULL));

    /* Pool should still contain all N slots. */
    uint32_t drained = 0;
    ota_job_event_data_t *slots[OTA_JOB_EVENT_POOL_SIZE];
    for (; drained < OTA_JOB_EVENT_POOL_SIZE; ++drained) {
        slots[drained] = event_pool_take();
        if (slots[drained] == NULL) {
            break;
        }
    }
    TEST_ASSERT_EQUAL(OTA_JOB_EVENT_POOL_SIZE, drained);

    for (uint32_t i = 0; i < drained; ++i) {
        (void)event_pool_return(slots[i]);
    }
    event_pool_deinit();
}

/* ========================================================================== */
/* copy_event_data routing: pool for payload-free, heap otherwise             */
/* ========================================================================== */

void test_copy_event_data_uses_pool_for_payload_free_event(void)
{
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    ota_job_event_data_t *dst = copy_event_data(&empty_transition_event);
    TEST_ASSERT_NOT_NULL(dst);
    TEST_ASSERT_TRUE_MESSAGE(is_in_event_pool(dst), "payload-free copy must come from the static pool");
    TEST_ASSERT_EQUAL(OTA_JOB_EVENT_EMPTY_TRANSITION, dst->event);
    TEST_ASSERT_NULL(dst->payload);

    free_event_data(dst);
    /* After free, pool must still be fully populated. */
    uint32_t drained = 0;
    ota_job_event_data_t *slots[OTA_JOB_EVENT_POOL_SIZE];
    for (; drained < OTA_JOB_EVENT_POOL_SIZE; ++drained) {
        slots[drained] = event_pool_take();
        if (slots[drained] == NULL) {
            break;
        }
    }
    TEST_ASSERT_EQUAL(OTA_JOB_EVENT_POOL_SIZE, drained);
    for (uint32_t i = 0; i < drained; ++i) {
        (void)event_pool_return(slots[i]);
    }
    event_pool_deinit();
}

void test_copy_event_data_falls_back_to_heap_on_pool_exhaustion(void)
{
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    /* Drain the pool. */
    ota_job_event_data_t *slots[OTA_JOB_EVENT_POOL_SIZE];
    for (uint32_t i = 0; i < OTA_JOB_EVENT_POOL_SIZE; ++i) {
        slots[i] = event_pool_take();
        TEST_ASSERT_NOT_NULL(slots[i]);
    }

    /* Next payload-free copy must fall through to the heap. */
    ota_job_event_data_t *heap_copy = copy_event_data(&empty_transition_event);
    TEST_ASSERT_NOT_NULL(heap_copy);
    TEST_ASSERT_FALSE_MESSAGE(is_in_event_pool(heap_copy), "exhausted pool must trigger heap fallback");
    TEST_ASSERT_EQUAL(OTA_JOB_EVENT_EMPTY_TRANSITION, heap_copy->event);
    TEST_ASSERT_NULL(heap_copy->payload);

    free_event_data(heap_copy);
    for (uint32_t i = 0; i < OTA_JOB_EVENT_POOL_SIZE; ++i) {
        (void)event_pool_return(slots[i]);
    }
    event_pool_deinit();
}

void test_copy_event_data_uses_heap_for_payload_events(void)
{
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    static const char payload_bytes[] = "{\"foo\":1}";
    ota_job_event_data_payload_t src_payload = {
        .data = (void *)payload_bytes,
        .len = sizeof(payload_bytes) - 1,
    };
    ota_job_event_data_t src = { .event = OTA_JOB_EVENT_JOBS_CHANGED, .payload = &src_payload };

    ota_job_event_data_t *dst = copy_event_data(&src);
    TEST_ASSERT_NOT_NULL(dst);
    TEST_ASSERT_FALSE_MESSAGE(is_in_event_pool(dst), "payload-bearing copy must not come from the pool");
    TEST_ASSERT_NOT_NULL(dst->payload);
    TEST_ASSERT_EQUAL(sizeof(payload_bytes) - 1, dst->payload->len);

    free_event_data(dst);
    event_pool_deinit();
}

/* ========================================================================== */
/* Handler escalation when enqueue_event fails (pool-backed error path)       */
/* ========================================================================== */

void test_fsm_idle_fetch_enqueue_failure_enters_error_state(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("enqueue_event fault injection needs wrap support");
#else
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    reset_ctx_state(OTA_JOB_STATE_IDLE);
    /* Fail the transition enqueue; the subsequent error-event enqueue from
     * enter_error_state must still succeed (pool-backed, no fault left). */
    g_test_work_queue_fail_countdown = 1;

    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_FETCH_REQUESTED, .payload = NULL };
    bool should_free = true;
    ota_job_state_t next = handle_state_idle(&ev, &should_free);

    TEST_ASSERT_EQUAL(OTA_JOB_STATE_ERROR, next);
    TEST_ASSERT_EQUAL(OTA_ERROR_RETRY_WITH_BACKOFF, g_ota_state_ctx.last_error);
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_IDLE, g_ota_state_ctx.recovery.state);
    TEST_ASSERT_EQUAL(0, g_test_work_queue_fail_countdown);

    event_pool_deinit();
#endif
}

void test_fsm_waiting_for_pending_jobs_accepted_enqueue_failure_enters_error_state(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("enqueue_event fault injection needs wrap support");
#else
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    reset_ctx_state(OTA_JOB_STATE_WAITING_FOR_PENDING_JOBS);
    g_test_work_queue_fail_countdown = 1;

    /* event_data is allocated so that enqueue_event(..., should_copy=false)
     * takes ownership - on failure, enqueue_event must free it for us. */
    ota_job_event_data_t stack_event = { .event = OTA_JOB_EVENT_PENDING_JOBS_ACCEPTED, .payload = NULL };
    ota_job_event_data_t *owned = copy_event_data(&stack_event);
    TEST_ASSERT_NOT_NULL(owned);

    bool should_free = true;
    ota_job_state_t next = handle_state_waiting_for_pending_jobs(owned, &should_free);

    TEST_ASSERT_EQUAL(OTA_JOB_STATE_ERROR, next);
    TEST_ASSERT_EQUAL(OTA_ERROR_RETRY_WITH_BACKOFF, g_ota_state_ctx.last_error);
    /* Handler transferred ownership; the run_once caller must not free again. */
    TEST_ASSERT_FALSE_MESSAGE(should_free, "ownership was passed to enqueue_event; caller must not free");
    TEST_ASSERT_EQUAL(0, g_test_work_queue_fail_countdown);

    event_pool_deinit();
#endif
}

/* ========================================================================== */
/* Recovery path: recovery.event_data lifecycle                               */
/* ========================================================================== */

void test_fsm_error_recovery_event_data_nulled_on_enqueue_failure(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("recovery path uses enqueue_event; wrap required");
#else
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    reset_ctx_state(OTA_JOB_STATE_ERROR);
    g_ota_state_ctx.last_error = OTA_ERROR_RETRY_WITH_BACKOFF;
    g_ota_state_ctx.recovery.state = OTA_JOB_STATE_IDLE;
    g_ota_state_ctx.recovery.event = OTA_JOB_EVENT_ERROR_OCCURRED;
    g_ota_state_ctx.recovery.event_data = copy_event_data(&empty_transition_event);
    TEST_ASSERT_NOT_NULL(g_ota_state_ctx.recovery.event_data);
    TEST_ASSERT_TRUE(is_in_event_pool(g_ota_state_ctx.recovery.event_data));

    g_test_work_queue_fail_countdown = 1;

    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_RECOVERY_REQUESTED, .payload = NULL };
    bool should_free = true;
    ota_job_state_t next = handle_state_error(&ev, &should_free);

    /* Recovery enqueue failed -> FSM stays in ERROR, event_data must be nulled
     * (and freed back to the pool) to prevent use-after-free on retry. */
    TEST_ASSERT_EQUAL(OTA_JOB_STATE_ERROR, next);
    TEST_ASSERT_NULL_MESSAGE(g_ota_state_ctx.recovery.event_data, "recovery.event_data must be nulled after failed enqueue");
    TEST_ASSERT_EQUAL(0, g_test_work_queue_fail_countdown);

    /* Freed event returned to pool: draining must yield OTA_JOB_EVENT_POOL_SIZE slots. */
    uint32_t drained = 0;
    ota_job_event_data_t *slots[OTA_JOB_EVENT_POOL_SIZE];
    for (; drained < OTA_JOB_EVENT_POOL_SIZE; ++drained) {
        slots[drained] = event_pool_take();
        if (slots[drained] == NULL) {
            break;
        }
    }
    TEST_ASSERT_EQUAL_MESSAGE(OTA_JOB_EVENT_POOL_SIZE, drained, "failed recovery slot was not returned to pool");
    for (uint32_t i = 0; i < drained; ++i) {
        (void)event_pool_return(slots[i]);
    }
    event_pool_deinit();
#endif
}

void test_fsm_error_recovery_event_data_nulled_on_enqueue_success(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("recovery path uses enqueue_event; wrap required");
#else
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    reset_ctx_state(OTA_JOB_STATE_ERROR);
    g_ota_state_ctx.last_error = OTA_ERROR_RETRY_WITH_BACKOFF;
    g_ota_state_ctx.recovery.state = OTA_JOB_STATE_IDLE;
    g_ota_state_ctx.recovery.event = OTA_JOB_EVENT_ERROR_OCCURRED;
    g_ota_state_ctx.recovery.event_data = copy_event_data(&empty_transition_event);
    TEST_ASSERT_NOT_NULL(g_ota_state_ctx.recovery.event_data);

    /* No fault: enqueue succeeds, ownership passes to (wrapped) work queue. */
    ota_job_event_data_t ev = { .event = OTA_JOB_EVENT_RECOVERY_REQUESTED, .payload = NULL };
    bool should_free = true;
    ota_job_state_t next = handle_state_error(&ev, &should_free);

    TEST_ASSERT_EQUAL(OTA_JOB_STATE_IDLE, next);
    TEST_ASSERT_NULL(g_ota_state_ctx.recovery.event_data);

    event_pool_deinit();
#endif
}

/* ========================================================================== */
/* fixed_data scalar: copied by value, keeps terminal events payload-free       */
/* ========================================================================== */

void test_copy_event_data_preserves_fixed_data_in_pool_path(void)
{
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    /* Payload-free event carrying a scalar: must be served from the pool AND
     * carry the scalar through unchanged (no heap). */
    ota_job_event_data_t src = {
        .event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED,
        .payload = NULL,
        .fixed_data = (void *)0xABCD,
    };
    ota_job_event_data_t *dst = copy_event_data(&src);
    TEST_ASSERT_NOT_NULL(dst);
    TEST_ASSERT_TRUE_MESSAGE(is_in_event_pool(dst), "reboot-signalling SUCCEEDED must stay pool-served (heap-free)");
    TEST_ASSERT_NULL(dst->payload);
    TEST_ASSERT_EQUAL_PTR((void *)0xABCD, dst->fixed_data);

    free_event_data(dst);
    event_pool_deinit();
}

void test_copy_event_data_preserves_fixed_data_in_heap_path(void)
{
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    /* Payload-bearing event goes via heap; fixed_data must still round-trip. */
    static const char payload_bytes[] = "x";
    ota_job_event_data_payload_t src_payload = {
        .data = (void *)payload_bytes,
        .len = sizeof(payload_bytes) - 1,
    };
    ota_job_event_data_t src = {
        .event = OTA_JOB_EVENT_JOBS_CHANGED,
        .payload = &src_payload,
        .fixed_data = (void *)0x1234,
    };
    ota_job_event_data_t *dst = copy_event_data(&src);
    TEST_ASSERT_NOT_NULL(dst);
    TEST_ASSERT_FALSE_MESSAGE(is_in_event_pool(dst), "payload-bearing copy must be on the heap");
    TEST_ASSERT_EQUAL_PTR((void *)0x1234, dst->fixed_data);

    free_event_data(dst);
    event_pool_deinit();
}

void test_copy_event_data_reboot_signal_survives_pool_exhaustion(void)
{
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());

    /* Drain the pool so the reboot-signalling SUCCEEDED falls through to heap;
     * it must still copy successfully (payload-free -> no data alloc) and keep
     * the scalar. This is the exact case that previously forced a heap payload. */
    ota_job_event_data_t *slots[OTA_JOB_EVENT_POOL_SIZE];
    for (uint32_t i = 0; i < OTA_JOB_EVENT_POOL_SIZE; ++i) {
        slots[i] = event_pool_take();
        TEST_ASSERT_NOT_NULL(slots[i]);
    }

    ota_job_event_data_t src = {
        .event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED,
        .payload = NULL,
        .fixed_data = (void *)true,
    };
    ota_job_event_data_t *dst = copy_event_data(&src);
    TEST_ASSERT_NOT_NULL(dst);
    TEST_ASSERT_FALSE(is_in_event_pool(dst));
    TEST_ASSERT_NULL_MESSAGE(dst->payload, "reboot signal must never allocate a payload");
    TEST_ASSERT_EQUAL_PTR((void *)true, dst->fixed_data);

    free_event_data(dst);
    for (uint32_t i = 0; i < OTA_JOB_EVENT_POOL_SIZE; ++i) {
        (void)event_pool_return(slots[i]);
    }
    event_pool_deinit();
}

/* ========================================================================== */
/* POST_DOWNLOAD SUCCEEDED: reboot decided by fixed_data, not payload           */
/* ========================================================================== */

void test_fsm_post_download_succeeded_fixed_data_requests_reboot(void)
{
    reset_ctx_state(OTA_JOB_STATE_POST_DOWNLOAD);
    g_ota_state_ctx.current_job.filetype_handler = &test_ft_ctx_no_version; /* set_version==NULL */

    ota_job_event_data_t ev = {
        .event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED,
        .payload = NULL,
        .fixed_data = (void *)true,
    };
    bool should_free = true;
    (void)handle_state_post_download(&ev, &should_free);

    TEST_ASSERT_TRUE(g_ota_state_ctx.current_job.should_reboot);
}

void test_fsm_post_download_succeeded_null_fixed_data_no_reboot(void)
{
    reset_ctx_state(OTA_JOB_STATE_POST_DOWNLOAD);
    g_ota_state_ctx.current_job.filetype_handler = &test_ft_ctx_no_version;

    ota_job_event_data_t ev = {
        .event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED,
        .payload = NULL,
        .fixed_data = NULL,
    };
    bool should_free = true;
    (void)handle_state_post_download(&ev, &should_free);

    TEST_ASSERT_FALSE(g_ota_state_ctx.current_job.should_reboot);
}

/* ========================================================================== */
/* ota_job_state_post_terminal_event: guaranteed handoff via backoff re-delivery */
/* ========================================================================== */

void test_post_terminal_event_null_returns_invalid_arg(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_INVALID_ARG, ota_job_state_post_terminal_event(NULL));
}

void test_post_terminal_event_direct_delivery_schedules_no_backoff(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("needs work-queue/backoff wrap");
#else
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());
    reset_ctx_state(OTA_JOB_STATE_JOB_EXECUTION);
    g_pre_init_event_queue.state_initialized = true; /* route straight to enqueue_event */

    ota_job_event_data_t ev = {
        .event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED,
        .payload = NULL,
        .fixed_data = (void *)true,
    };
    /* No fault: work queue accepts, handoff lands directly, no backoff needed. */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, ota_job_state_post_terminal_event(&ev));
    TEST_ASSERT_EQUAL_INT(0, g_test_backoff_retry_calls);

    g_pre_init_event_queue.state_initialized = false;
    event_pool_deinit();
#endif
}

void test_post_terminal_event_enqueue_failure_schedules_backoff_redelivery(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("needs work-queue/backoff wrap");
#else
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());
    reset_ctx_state(OTA_JOB_STATE_JOB_EXECUTION);
    g_pre_init_event_queue.state_initialized = true;

    /* Fault the direct handoff: enqueue fails once. */
    g_test_work_queue_fail_countdown = 1;

    ota_job_event_data_t ev = {
        .event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED,
        .payload = NULL,
        .fixed_data = (void *)true,
    };
    esp_rmaker_error_t err = ota_job_state_post_terminal_event(&ev);

    /* Post reports OK because a backoff re-delivery was scheduled (not dropped). */
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_EQUAL_INT(0, g_test_work_queue_fail_countdown);

    /* Event stashed for re-delivery, payload forced NULL (heap-free by contract). */
    TEST_ASSERT_EQUAL(OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED, g_terminal_event_pending.event);
    TEST_ASSERT_NULL(g_terminal_event_pending.payload);
    TEST_ASSERT_EQUAL_PTR((void *)true, g_terminal_event_pending.fixed_data);

    /* Routed onto the dedicated terminal retry context with the repost callback. */
    TEST_ASSERT_EQUAL_INT(1, g_test_backoff_retry_calls);
    TEST_ASSERT_EQUAL_PTR(&g_terminal_retry_ctx, g_test_last_backoff_ctx);
    TEST_ASSERT_EQUAL_PTR((const void *)terminal_event_repost_task, g_test_last_backoff_task);

    g_pre_init_event_queue.state_initialized = false;
    event_pool_deinit();
#endif
}

void test_terminal_repost_task_redelivers_then_stops(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("needs work-queue/backoff wrap");
#else
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());
    reset_ctx_state(OTA_JOB_STATE_JOB_EXECUTION);
    g_pre_init_event_queue.state_initialized = true;

    g_terminal_event_pending = (ota_job_event_data_t) {
        .event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED,
        .payload = NULL,
        .fixed_data = (void *)true,
    };

    /* Work queue accepts: event delivered, no re-arm. */
    terminal_event_repost_task(NULL);
    TEST_ASSERT_EQUAL_INT(0, g_test_backoff_retry_calls);

    g_pre_init_event_queue.state_initialized = false;
    event_pool_deinit();
#endif
}

void test_terminal_repost_task_reschedules_on_repeated_failure(void)
{
#if !RMAKER_OTA_JOBS_TEST_WRAP_AVAILABLE
    TEST_IGNORE_MESSAGE("needs work-queue/backoff wrap");
#else
    event_pool_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, event_pool_init());
    reset_ctx_state(OTA_JOB_STATE_JOB_EXECUTION);
    g_pre_init_event_queue.state_initialized = true;

    g_terminal_event_pending = (ota_job_event_data_t) {
        .event = OTA_JOB_EVENT_IMAGE_DOWNLOAD_SUCCEEDED,
        .payload = NULL,
        .fixed_data = (void *)true,
    };

    /* Work queue still failing: task must re-arm the backoff (never give up). */
    g_test_work_queue_fail_countdown = 1;
    terminal_event_repost_task(NULL);

    TEST_ASSERT_EQUAL_INT(1, g_test_backoff_retry_calls);
    TEST_ASSERT_EQUAL_PTR(&g_terminal_retry_ctx, g_test_last_backoff_ctx);
    TEST_ASSERT_EQUAL_PTR((const void *)terminal_event_repost_task, g_test_last_backoff_task);

    g_pre_init_event_queue.state_initialized = false;
    event_pool_deinit();
#endif
}
