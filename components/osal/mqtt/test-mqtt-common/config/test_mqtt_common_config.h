/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_mqtt_common_config.h
 * @brief Broker endpoints and credentials for the MQTT test suite.
 */

#if __has_include("sdkconfig.h")
#include "sdkconfig.h"

// ESP-IDF: Use the configuration file from the project
#define TEST_MQTT_COMMON_BROKER_URI                 ( CONFIG_TEST_MQTT_COMMON_BROKER_URI )
#define TEST_MQTT_COMMON_TCP_USERNAME               ( CONFIG_TEST_MQTT_COMMON_TCP_USERNAME )
#define TEST_MQTT_COMMON_TCP_PASSWORD               ( CONFIG_TEST_MQTT_COMMON_TCP_PASSWORD )
#define TEST_MQTT_COMMON_PORT_TCP                   ( CONFIG_TEST_MQTT_COMMON_PORT_TCP )
#define TEST_MQTT_COMMON_PORT_TLS_SERVER_ONLY       ( CONFIG_TEST_MQTT_COMMON_PORT_TLS_SERVER_ONLY )
#define TEST_MQTT_COMMON_PORT_TLS_MUTUAL_AUTH       ( CONFIG_TEST_MQTT_COMMON_PORT_TLS_MUTUAL_AUTH )
#else

// Use this file as the configuration file for the test app

/**
 * @brief MQTT Broker URI
 *
 * @note This is the URI of the MQTT broker to connect to.
 */
#define TEST_MQTT_COMMON_BROKER_URI                 "localhost"

/**
 * @brief MQTT Broker Username for TCP
 *
 * @note This is the username of the MQTT broker to connect to for TCP, with username/password authentication.
 */
#define TEST_MQTT_COMMON_TCP_USERNAME               ( "username" )

/**
 * @brief MQTT Broker Password for TCP
 *
 * @note This is the password of the MQTT broker to connect to for TCP, with username/password authentication.
 */
#define TEST_MQTT_COMMON_TCP_PASSWORD               ( "password" )

/**
 * @brief MQTT Broker Port for TCP
 *
 * @note This is the port of the MQTT broker to connect to for TCP, with username/password authentication.
 */
#define TEST_MQTT_COMMON_PORT_TCP                   ( 1883 )

/**
 * @brief MQTT Broker Port for TLS (server side only)
 *
 * @note This is the port of the MQTT broker to connect to for TLS (server side only).
 */
#define TEST_MQTT_COMMON_PORT_TLS_SERVER_ONLY       ( 8883 )
/**
 * @brief MQTT Broker Port for TLS (mutual authentication)
 *
 * @note This is the port of the MQTT broker to connect to for TLS (mutual authentication).
 */
#define TEST_MQTT_COMMON_PORT_TLS_MUTUAL_AUTH       ( 8884 )
#endif
