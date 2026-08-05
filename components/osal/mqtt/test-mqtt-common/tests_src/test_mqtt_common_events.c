/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_mqtt_common_events.c
 * @brief Unit tests for osal_mqtt_events (event group init/set/clear/wait/get).
 */

#include "unity.h"
#include <stdint.h>

#include "osal_mqtt_events.h"
#include "osal_mqtt_prototypes.h"

void test_mqtt_event_init_returns_success(void)
{
    osal_mqtt_event_deinit();
    osal_err_t ret = osal_mqtt_event_init();
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, ret);
}

void test_mqtt_event_init_sets_initial_bits(void)
{
    osal_mqtt_event_deinit();
    osal_err_t ret = osal_mqtt_event_init();
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, ret);
    osal_event_group_bits_t bits = osal_mqtt_event_get_bits(
                                       OSAL_MQTT_NETWORK_DISCONNECTED_BIT | OSAL_MQTT_CLIENT_DISCONNECTED_BIT);
    TEST_ASSERT_EQUAL_HEX32(OSAL_MQTT_NETWORK_DISCONNECTED_BIT | OSAL_MQTT_CLIENT_DISCONNECTED_BIT, bits);
}

void test_mqtt_event_set_and_get_bits(void)
{
    osal_mqtt_event_deinit();
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_mqtt_event_init());
    osal_mqtt_event_set_bits(OSAL_MQTT_NETWORK_CONNECTED_BIT | OSAL_MQTT_CLIENT_CONNECTED_BIT);
    osal_event_group_bits_t bits = osal_mqtt_event_get_bits(
                                       OSAL_MQTT_NETWORK_CONNECTED_BIT | OSAL_MQTT_CLIENT_CONNECTED_BIT);
    TEST_ASSERT_EQUAL_HEX32(OSAL_MQTT_NETWORK_CONNECTED_BIT | OSAL_MQTT_CLIENT_CONNECTED_BIT, bits);
}

void test_mqtt_event_clear_bits(void)
{
    osal_mqtt_event_deinit();
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_mqtt_event_init());
    osal_mqtt_event_set_bits(OSAL_MQTT_NETWORK_CONNECTED_BIT);
    osal_mqtt_event_clear_bits(OSAL_MQTT_NETWORK_DISCONNECTED_BIT);
    osal_event_group_bits_t bits = osal_mqtt_event_get_bits(
                                       OSAL_MQTT_NETWORK_CONNECTED_BIT | OSAL_MQTT_NETWORK_DISCONNECTED_BIT);
    TEST_ASSERT_EQUAL_HEX32(OSAL_MQTT_NETWORK_CONNECTED_BIT, bits);
}

void test_mqtt_event_wait_for_all_bits(void)
{
    osal_mqtt_event_deinit();
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_mqtt_event_init());
    osal_mqtt_event_set_bits(OSAL_MQTT_CLIENT_CONNECTED_BIT);
    bool ok = osal_mqtt_event_wait_for_all_bits(OSAL_MQTT_CLIENT_CONNECTED_BIT, 100);
    TEST_ASSERT_TRUE(ok);
}

void test_mqtt_event_wait_for_all_bits_timeout(void)
{
    osal_mqtt_event_deinit();
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_mqtt_event_init());
    /* Do not set CLIENT_CONNECTED_BIT; wait should timeout */
    bool ok = osal_mqtt_event_wait_for_all_bits(OSAL_MQTT_CLIENT_CONNECTED_BIT, 10);
    TEST_ASSERT_FALSE(ok);
}

void test_mqtt_event_deinit_clears_handle(void)
{
    osal_mqtt_event_deinit();
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_mqtt_event_init());
    osal_mqtt_event_deinit();
    /* Next init should succeed and create a new group (initial bits set again) */
    TEST_ASSERT_EQUAL(OSAL_ERR_OK, osal_mqtt_event_init());
    osal_event_group_bits_t bits = osal_mqtt_event_get_bits(
                                       OSAL_MQTT_NETWORK_DISCONNECTED_BIT | OSAL_MQTT_CLIENT_DISCONNECTED_BIT);
    TEST_ASSERT_EQUAL_HEX32(OSAL_MQTT_NETWORK_DISCONNECTED_BIT | OSAL_MQTT_CLIENT_DISCONNECTED_BIT, bits);
}
