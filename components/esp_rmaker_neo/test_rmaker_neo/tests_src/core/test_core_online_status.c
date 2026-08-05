/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_core_online_status.c
 */

#include "unity.h"
#include "test_rmng_prototypes.h"

#include "core_internal.h"
#include "network/common.h"
#include "constants/network.h"

#include "osal_event_group.h"

static bool network_has_bits(osal_event_group_bits_t bits)
{
    return esp_rmaker_network_get_bits() & bits;
}

void test_core_online_status_conditions(void)
{
    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_network_init());

    /* Initially not subscribed to cloud or params; check all relevant bits at once */
    TEST_ASSERT_FALSE(esp_rmaker_core_is_subscribed_to_cloud());
    TEST_ASSERT_FALSE(esp_rmaker_core_is_subscribed_to_params());
    TEST_ASSERT_FALSE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_CLOUD | RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_CLOUD));
    TEST_ASSERT_FALSE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_STATE_CHANGES | RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_STATE_CHANGES));

    /* Subscribe to cloud, verify correct flags */
    esp_rmaker_core_subscribed_to_cloud();
    TEST_ASSERT_TRUE(esp_rmaker_core_is_subscribed_to_cloud());
    TEST_ASSERT_FALSE(esp_rmaker_core_is_subscribed_to_params());
    TEST_ASSERT_TRUE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_CLOUD));
    TEST_ASSERT_FALSE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_CLOUD));
    TEST_ASSERT_FALSE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_STATE_CHANGES | RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_STATE_CHANGES));

    /* Subscribe to params as well, check all "subscribed" bits */
    esp_rmaker_core_subscribed_to_params();
    TEST_ASSERT_TRUE(esp_rmaker_core_is_subscribed_to_cloud());
    TEST_ASSERT_TRUE(esp_rmaker_core_is_subscribed_to_params());
    TEST_ASSERT_TRUE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_CLOUD | RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_STATE_CHANGES));
    TEST_ASSERT_FALSE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_CLOUD | RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_STATE_CHANGES));

    /* Unsubscribe from cloud, but keep params */
    esp_rmaker_core_unsubscribed_from_cloud();
    TEST_ASSERT_FALSE(esp_rmaker_core_is_subscribed_to_cloud());
    TEST_ASSERT_TRUE(esp_rmaker_core_is_subscribed_to_params());
    TEST_ASSERT_TRUE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_CLOUD));
    TEST_ASSERT_FALSE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_CLOUD));
    TEST_ASSERT_TRUE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_STATE_CHANGES));
    TEST_ASSERT_FALSE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_STATE_CHANGES));

    /* Finally, unsubscribe from params; check both unsubscribed bits at once */
    esp_rmaker_core_unsubscribed_from_params();
    TEST_ASSERT_FALSE(esp_rmaker_core_is_subscribed_to_cloud());
    TEST_ASSERT_FALSE(esp_rmaker_core_is_subscribed_to_params());
    TEST_ASSERT_TRUE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_CLOUD | RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_STATE_CHANGES));
    TEST_ASSERT_FALSE(network_has_bits(RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_CLOUD | RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_STATE_CHANGES));

    TEST_ASSERT_EQUAL(ESP_RMAKER_OK, esp_rmaker_network_deinit());
}
