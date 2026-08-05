/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_mqtt_common_prototypes.h
 * @brief Prototypes for the MQTT Common test component
 */

/* Includes ********************************************************************/

/* Standard C includes */
#include <stdbool.h>

/* Functions ******************************************************************/

/* --- osal_mqtt_util --- */
void test_mqtt_common_match_topic_exact(void);
void test_mqtt_common_match_topic_exact_mismatch(void);
void test_mqtt_common_match_topic_topic_longer_than_filter(void);
void test_mqtt_common_match_topic_filter_longer_than_topic(void);
void test_mqtt_common_match_topic_single_level_plus(void);
void test_mqtt_common_match_topic_plus_at_start(void);
void test_mqtt_common_match_topic_plus_at_end(void);
void test_mqtt_common_match_topic_multilevel_hash(void);
void test_mqtt_common_match_topic_hash_only(void);
void test_mqtt_common_match_topic_hash_must_be_last(void);
void test_mqtt_common_match_topic_empty_topic_empty_filter(void);
void test_mqtt_common_match_topic_empty_filter_non_empty_topic(void);
void test_mqtt_common_match_topic_length_limits(void);

/* --- osal_mqtt_events --- */
void test_mqtt_event_init_returns_success(void);
void test_mqtt_event_init_sets_initial_bits(void);
void test_mqtt_event_set_and_get_bits(void);
void test_mqtt_event_clear_bits(void);
void test_mqtt_event_wait_for_all_bits(void);
void test_mqtt_event_wait_for_all_bits_timeout(void);
void test_mqtt_event_deinit_clears_handle(void);

/* --- osal_mqtt_subscription_manager --- */
void test_mqtt_subscription_init_get_list(void);
void test_mqtt_subscription_add_and_remove(void);
void test_mqtt_subscription_add_invalid_params(void);
void test_mqtt_subscription_handle_publish_invokes_callback(void);
void test_mqtt_subscription_handle_publish_wildcard(void);
void test_mqtt_subscription_handle_publish_invalid_params(void);
void test_mqtt_subscription_duplicate_same_callback_not_added_twice(void);
void test_mqtt_subscription_attempt_resubscribe_all_called(void);
void test_mqtt_subscription_simulate_subacks_skips_qos0(void);

/* --- Integration: Basic tests --- */

/**
 * @brief Test the basic MQTT functionality with TLS (server only)
 */
void test_mqtt_tls_server_only_sub_pub_unsub_pub(void);

/**
 * @brief Test the basic MQTT functionality with TLS (mutual authentication)
 */
void test_mqtt_tls_mutual_auth_sub_pub_unsub_pub(void);

/**
 * @brief Test MQTT retained-message delivery (TLS server only).
 */
void test_mqtt_tls_server_only_retain(void);

/**
 * @brief Test MQTT retained-message delivery (TLS mutual authentication).
 */
void test_mqtt_tls_mutual_auth_retain(void);

/**
 * @brief Test MQTT Last Will and Testament delivery (TLS server only).
 *
 * Uses two sequential wrapper clients: a phase-1 client registers the LWT
 * with retain=true and drop()s the connection; a phase-2 client subscribes
 * to the LWT topic and must receive the retained LWT payload.
 */
void test_mqtt_tls_server_only_lwt(void);

/**
 * @brief Test MQTT Last Will and Testament delivery (TLS mutual auth).
 */
void test_mqtt_tls_mutual_auth_lwt(void);

/* --- All tests --- */

/**
 * @brief Run all tests for the MQTT Common test component
 */
int test_mqtt_common_all_tests_unity(void);
