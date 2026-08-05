/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_ota_status.c
 * @brief Unit tests for the OTA status manager (ota_status.c)
 *
 * Covers the dropped-terminal-response recovery fixes:
 *   - The retry task re-arms the response watchdog after a successful publish
 *     when no accepted/rejected response removes the cached terminal entry
 *     (previously it stopped, leaving the job state machine waiting forever).
 *   - ota_status_resend_pending_terminals() republishes every cached terminal
 *     update (used after a re-subscribe to recover a lost response).
 *
 * These exercise ota_status.c as a separately-compiled TU via its public
 * API plus the test-only seams declared in test_rmng_ota_prototypes.h.
 */

#include "unity.h"
#include "test_rmng_ota_prototypes.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_rmaker_error_types.h"
#include "esp_rmaker_mqtt_impl.h"
#include "jobs.h"
#include "ota_status.h"

#if defined(RMAKER_OTA_JOBS_TEST_WRAP_LINKER) || defined(RMAKER_OTA_JOBS_TEST_WRAP_DL_LIB)

static const char *TEST_THING = "test-thing";

/* Mock MQTT publish: count calls, capture the last payload, always succeed. */
static int g_mock_publish_count = 0;
static osal_mqtt_publish_t g_saved_publish = NULL;
static char g_last_payload[256];
static size_t g_last_payload_len = 0;

static osal_err_t mock_publish(osal_mqtt_event_loop_channel_t *channel,
                               const char *topic, size_t topic_len,
                               void *data, size_t data_len,
                               osal_mqtt_QoS_t qos, bool retain)
{
    (void)channel;
    (void)topic;
    (void)topic_len;
    (void)qos;
    (void)retain;
    g_mock_publish_count++;
    /* Capture the assembled message so tests can assert its exact JSON. */
    g_last_payload_len = 0;
    g_last_payload[0] = '\0';
    if (data != NULL && data_len < sizeof(g_last_payload)) {
        memcpy(g_last_payload, data, data_len);
        g_last_payload[data_len] = '\0';
        g_last_payload_len = data_len;
    }
    return OSAL_ERR_OK;
}

static void status_test_begin(void)
{
    /* Fresh status manager. */
    ota_status_deinit();
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, ota_status_init(TEST_THING, strlen(TEST_THING)));

    /* Deterministic scheduling: count (re)schedules, never start a real timer,
     * so we can drive the retry task synchronously without races. */
    rmaker_ota_status_test_set_intercept_scheduling(1);
    rmaker_ota_status_test_reset_retry_schedule_count();

    /* Install the counting publish mock. */
    g_saved_publish = esp_rmaker_mqtt_impl.publish;
    esp_rmaker_mqtt_impl.publish = mock_publish;
    g_mock_publish_count = 0;
    g_last_payload[0] = '\0';
    g_last_payload_len = 0;
}

static void status_test_end(void)
{
    esp_rmaker_mqtt_impl.publish = g_saved_publish;
    rmaker_ota_status_test_set_intercept_scheduling(0);
    ota_status_deinit();
}

static void make_terminal_update(ota_status_update_t *u, const char *job_id)
{
    memset(u, 0, sizeof(*u));
    size_t n = strlen(job_id);
    u->status = Succeeded;
    memcpy(u->job_id, job_id, n);
    u->job_id[n] = '\0';
    u->job_id_len = n;
    u->status_details_str = NULL;
    u->status_details_str_len = 0;
}

/* A terminal update whose ack is lost must keep the retry armed.
 * Sending caches + schedules once and publishes once. Running the retry task
 * (publish succeeds, no ack) must republish AND re-arm the watchdog. */
void test_ota_status_retry_rearms_after_successful_publish(void)
{
    status_test_begin();

    ota_status_update_t u;
    make_terminal_update(&u, "JOB-RETRY");

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, ota_status_send(&u, NULL));
    int sched_after_send = rmaker_ota_status_test_get_retry_schedule_count();
    TEST_ASSERT_EQUAL_INT(1, sched_after_send);   /* set_cached_terminal scheduled once */
    TEST_ASSERT_EQUAL_INT(1, g_mock_publish_count);

    /* Ack never arrives (no ota_status_on_update_response). Fire the retry once. */
    g_mock_publish_count = 0;
    rmaker_ota_status_test_run_first_retry();

    TEST_ASSERT_EQUAL_INT(1, g_mock_publish_count); /* republished the terminal update */
    /* Re-armed: the retry task scheduled again instead of stopping (the fix). */
    TEST_ASSERT_GREATER_THAN_INT(sched_after_send,
                                 rmaker_ota_status_test_get_retry_schedule_count());

    status_test_end();
}

/* Resend republishes every cached terminal update. */
void test_ota_status_resend_pending_republishes_cached_terminals(void)
{
    status_test_begin();

    ota_status_update_t a, b;
    make_terminal_update(&a, "JOB-A");
    make_terminal_update(&b, "JOB-B");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, ota_status_send(&a, NULL));
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, ota_status_send(&b, NULL));
    TEST_ASSERT_FALSE(ota_status_is_cache_empty());

    /* Both acks lost: entries still cached. Resend must republish both. */
    g_mock_publish_count = 0;
    ota_status_resend_pending_terminals();
    TEST_ASSERT_EQUAL_INT(2, g_mock_publish_count);

    status_test_end();
}

/* Resend is a safe no-op with nothing cached. */
void test_ota_status_resend_pending_noop_when_empty(void)
{
    status_test_begin();

    TEST_ASSERT_TRUE(ota_status_is_cache_empty());
    g_mock_publish_count = 0;
    ota_status_resend_pending_terminals();
    TEST_ASSERT_EQUAL_INT(0, g_mock_publish_count);

    status_test_end();
}

/* Clearing a cached terminal removes it through the canonical removal path
 * (which stops/cancels the retry timer before freeing - no use-after-free). */
void test_ota_status_clear_job_entries_removes_cached_terminal(void)
{
    status_test_begin();

    ota_status_update_t u;
    make_terminal_update(&u, "JOB-CLR");
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, ota_status_send(&u, NULL));
    TEST_ASSERT_FALSE(ota_status_is_cache_empty());

    ota_status_clear_job_entries(u.job_id, u.job_id_len);
    TEST_ASSERT_TRUE(ota_status_is_cache_empty());

    status_test_end();
}

/* The assembled Update message must splice statusDetails (a raw JSON object) in before
 * the closing brace. This Jobs LTS's Jobs_UpdateMsg has no statusDetails support, so the
 * manual splice in ota_status_publish_update() is what this guards.
 *
 * A fresh job (init resets the cache) gets job_id_int=1 and starting version 1, so the
 * terminal clientToken char is '1' and expectedVersion is "1". */
void test_ota_status_update_message_includes_status_details(void)
{
    status_test_begin();

    ota_status_update_t u;
    make_terminal_update(&u, "JOB-SD");
    static char details[] = "{\"reason\":\"boom\"}";
    u.status_details_str = details;
    u.status_details_str_len = strlen(details);

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, ota_status_send(&u, NULL));
    TEST_ASSERT_EQUAL_INT(1, g_mock_publish_count);
    TEST_ASSERT_EQUAL_STRING(
        "{\"clientToken\":\"11\",\"status\":\"SUCCEEDED\",\"expectedVersion\":\"1\",\"statusDetails\":{\"reason\":\"boom\"}}",
        g_last_payload);

    status_test_end();
}

/* Without statusDetails the splice branch is skipped and the message ends right after
 * expectedVersion. Guards that the no-details path stays valid JSON. */
void test_ota_status_update_message_without_status_details(void)
{
    status_test_begin();

    ota_status_update_t u;
    make_terminal_update(&u, "JOB-ND");   /* status_details_str = NULL */

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, ota_status_send(&u, NULL));
    TEST_ASSERT_EQUAL_INT(1, g_mock_publish_count);
    TEST_ASSERT_EQUAL_STRING(
        "{\"clientToken\":\"11\",\"status\":\"SUCCEEDED\",\"expectedVersion\":\"1\"}",
        g_last_payload);

    status_test_end();
}

#else /* test wraps unavailable: keep the runner linkable */

void test_ota_status_retry_rearms_after_successful_publish(void)
{
    TEST_IGNORE_MESSAGE("requires test wrap build");
}
void test_ota_status_resend_pending_republishes_cached_terminals(void)
{
    TEST_IGNORE_MESSAGE("requires test wrap build");
}
void test_ota_status_resend_pending_noop_when_empty(void)
{
    TEST_IGNORE_MESSAGE("requires test wrap build");
}
void test_ota_status_clear_job_entries_removes_cached_terminal(void)
{
    TEST_IGNORE_MESSAGE("requires test wrap build");
}
void test_ota_status_update_message_includes_status_details(void)
{
    TEST_IGNORE_MESSAGE("requires test wrap build");
}
void test_ota_status_update_message_without_status_details(void)
{
    TEST_IGNORE_MESSAGE("requires test wrap build");
}

#endif
