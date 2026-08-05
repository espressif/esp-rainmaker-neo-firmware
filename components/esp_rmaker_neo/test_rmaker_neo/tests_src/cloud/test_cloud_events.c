/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_cloud_events.c
 * @brief Test cloud events.
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

void test_cloud_events_payload(void)
{
    esp_rmaker_cloud_event_t event;
    event.name = "test";
    event.data = "test";
    event.p_set_response_cb_context = NULL;
}
