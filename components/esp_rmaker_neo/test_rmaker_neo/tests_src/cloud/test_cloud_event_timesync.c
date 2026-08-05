/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_cloud_event_timesync.c
 * @brief Tests for the 'getTimeSync' cloud event.
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include <string.h>

#include "network/cloud/events.h"
#include "osal_timesync.h"

void test_cloud_event_timesync_builder(void)
{
    esp_rmaker_cloud_event_t event;
    memset(&event, 0xAA, sizeof(event));

    esp_rmaker_error_t err = esp_rmaker_cloud_event_getTimeSync(&event);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_EQUAL_STRING("getTimeSync", event.name);
    TEST_ASSERT_NULL(event.data);
    TEST_ASSERT_NULL(event.p_set_response_cb_context);
}

void test_cloud_event_timesync_builder_registered(void)
{
    /* The builders table must resolve the getTimeSync flag position to the
     * getTimeSync builder (table order must match the enum). */
    esp_rmaker_cloud_event_t event = {0};
    esp_rmaker_error_t err = RMAKER_CLOUD_EVENT_BUILDERS[RMAKER_CLOUD_EVENT_FLAG_POS_getTimeSync](&event);
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, err);
    TEST_ASSERT_EQUAL_STRING("getTimeSync", event.name);
}

void test_cloud_event_timesync_response_sets_processed_bit(void)
{
    /* Initialize timesync so osal_timesync_is_synced() reflects the (valid) host
     * clock and the handler skips the actual settimeofday. */
    TEST_ASSERT_EQUAL(0, osal_timesync_init(NULL));

    esp_rmaker_cloud_events_tracker_t tracker = {0};
    esp_rmaker_cloud_event_response_getTimeSync(&tracker, 1768464000912LL);
    TEST_ASSERT_EQUAL(1 << RMAKER_CLOUD_EVENT_FLAG_POS_getTimeSync, tracker.events_processed & (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getTimeSync));

    osal_timesync_deinit();
}

void test_cloud_event_timesync_response_ignores_invalid_time(void)
{
    esp_rmaker_cloud_events_tracker_t tracker = {0};

    /* Invalid values must not be applied, but the event must still be
     * marked processed so it is not re-requested forever. */
    esp_rmaker_cloud_event_response_getTimeSync(&tracker, 0);
    TEST_ASSERT_EQUAL(1 << RMAKER_CLOUD_EVENT_FLAG_POS_getTimeSync, tracker.events_processed & (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getTimeSync));

    tracker.events_processed = 0;
    esp_rmaker_cloud_event_response_getTimeSync(&tracker, -42);
    TEST_ASSERT_EQUAL(1 << RMAKER_CLOUD_EVENT_FLAG_POS_getTimeSync, tracker.events_processed & (1 << RMAKER_CLOUD_EVENT_FLAG_POS_getTimeSync));
}
