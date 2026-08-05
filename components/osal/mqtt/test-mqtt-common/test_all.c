/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include "unity.h"
#include "test_mqtt_common_prototypes.h"

int test_mqtt_common_all_tests_unity(void)
{
    UNITY_BEGIN();

    /* --- osal_mqtt_util --- */
    RUN_TEST(test_mqtt_common_match_topic_exact);
    RUN_TEST(test_mqtt_common_match_topic_exact_mismatch);
    RUN_TEST(test_mqtt_common_match_topic_topic_longer_than_filter);
    RUN_TEST(test_mqtt_common_match_topic_filter_longer_than_topic);
    RUN_TEST(test_mqtt_common_match_topic_single_level_plus);
    RUN_TEST(test_mqtt_common_match_topic_plus_at_start);
    RUN_TEST(test_mqtt_common_match_topic_plus_at_end);
    RUN_TEST(test_mqtt_common_match_topic_multilevel_hash);
    RUN_TEST(test_mqtt_common_match_topic_hash_only);
    RUN_TEST(test_mqtt_common_match_topic_hash_must_be_last);
    RUN_TEST(test_mqtt_common_match_topic_empty_topic_empty_filter);
    RUN_TEST(test_mqtt_common_match_topic_empty_filter_non_empty_topic);
    RUN_TEST(test_mqtt_common_match_topic_length_limits);

    /* --- osal_mqtt_events --- */
    RUN_TEST(test_mqtt_event_init_returns_success);
    RUN_TEST(test_mqtt_event_init_sets_initial_bits);
    RUN_TEST(test_mqtt_event_set_and_get_bits);
    RUN_TEST(test_mqtt_event_clear_bits);
    RUN_TEST(test_mqtt_event_wait_for_all_bits);
    RUN_TEST(test_mqtt_event_wait_for_all_bits_timeout);
    RUN_TEST(test_mqtt_event_deinit_clears_handle);

    /* --- osal_mqtt_subscription_manager --- */
    RUN_TEST(test_mqtt_subscription_init_get_list);
    RUN_TEST(test_mqtt_subscription_add_and_remove);
    RUN_TEST(test_mqtt_subscription_add_invalid_params);
    RUN_TEST(test_mqtt_subscription_handle_publish_invokes_callback);
    RUN_TEST(test_mqtt_subscription_handle_publish_wildcard);
    RUN_TEST(test_mqtt_subscription_handle_publish_invalid_params);
    RUN_TEST(test_mqtt_subscription_duplicate_same_callback_not_added_twice);
    RUN_TEST(test_mqtt_subscription_attempt_resubscribe_all_called);
    RUN_TEST(test_mqtt_subscription_simulate_subacks_skips_qos0);

    /* --- Integration: TLS (server only) --- */
    RUN_TEST(test_mqtt_tls_server_only_sub_pub_unsub_pub);
    RUN_TEST(test_mqtt_tls_server_only_retain);
    RUN_TEST(test_mqtt_tls_server_only_lwt);

    /* --- Integration: TLS (mutual authentication) --- */
    RUN_TEST(test_mqtt_tls_mutual_auth_sub_pub_unsub_pub);
    RUN_TEST(test_mqtt_tls_mutual_auth_retain);
    RUN_TEST(test_mqtt_tls_mutual_auth_lwt);

    return UNITY_END();
}
