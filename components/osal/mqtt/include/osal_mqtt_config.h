/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mqtt_config.h
 * @brief Configuration for the MQTT common component.
 */

#ifndef OSAL_MQTT_CONFIG_H
#define OSAL_MQTT_CONFIG_H

/* ESP-IDF sdkconfig include. */
#include "sdkconfig.h"

/**
 * @brief The maximum number of subscriptions tracked by subscription manager.
 */
#define configOSAL_MQTT_SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS    ( CONFIG_OSAL_MQTT_SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS )

/**
 * @brief The task stack size of the agent task.
 */
#define configOSAL_MQTT_AGENT_TASK_STACK_SIZE                    ( CONFIG_OSAL_MQTT_AGENT_TASK_STACK_SIZE )

/**
 * @brief The task priority of the agent task.
 */
#define configOSAL_MQTT_AGENT_TASK_PRIORITY                      ( CONFIG_OSAL_MQTT_AGENT_TASK_PRIORITY )

/**
 * @brief The keep alive interval (in seconds) for the MQTT client.
 */
#define configOSAL_MQTT_KEEP_ALIVE_INTERVAL_S                    ( CONFIG_OSAL_MQTT_KEEP_ALIVE_INTERVAL_S )

#ifdef CONFIG_OSAL_MQTT_IMPL_CORE

/**
 * @brief Dimensions the buffer used to serialize and deserialize MQTT packets.
 * @note Specified in bytes.  Must be large enough to hold the maximum
 * anticipated MQTT payload.
 */
#define configOSAL_MQTT_CORE_NETWORK_BUFFER_SIZE                         ( CONFIG_OSAL_MQTT_CORE_NETWORK_BUFFER_SIZE )

/**
 * @brief The length of the queue used to hold commands for the agent.
 */
#define configOSAL_MQTT_CORE_COMMAND_QUEUE_LENGTH                        ( CONFIG_OSAL_MQTT_CORE_COMMAND_QUEUE_LENGTH )

/**
 * @brief The task stack size of the connection handling task.
 */
#define configOSAL_MQTT_CORE_CONNECTION_TASK_STACK_SIZE                  ( CONFIG_OSAL_MQTT_CORE_CONNECTION_TASK_STACK_SIZE )

/**
 * @brief The task priority of the connection handling task.
 */
#define configOSAL_MQTT_CORE_CONNECTION_TASK_PRIORITY                    ( CONFIG_OSAL_MQTT_CORE_CONNECTION_TASK_PRIORITY )

/**
 * @brief The timeout for receiving CONNACK after sending an MQTT CONNECT packet.
 *
 * Defined in milliseconds.
 */
#define configOSAL_MQTT_CORE_CONNACK_RECV_TIMEOUT_MS                     ( CONFIG_OSAL_MQTT_CORE_CONNACK_RECV_TIMEOUT_MS )

/**
 * @brief The timeout for establishing a TLS connection, in milliseconds.
 */
#define configOSAL_MQTT_CORE_TLS_CONNECT_TIMEOUT_MS                      ( CONFIG_OSAL_MQTT_CORE_TLS_CONNECT_TIMEOUT_MS )

/**
 * @brief The maximum back-off delay (in milliseconds) for retrying failed operation
 *  with server.
 */
#define configOSAL_MQTT_CORE_RETRY_MAX_BACKOFF_DELAY_MS                  ( CONFIG_OSAL_MQTT_CORE_RETRY_MAX_BACKOFF_DELAY_MS )

/**
 * @brief The base back-off delay (in milliseconds) to use for network operation retry
 * attempts.
 */
#define configOSAL_MQTT_CORE_RETRY_BACKOFF_BASE_MS                       ( CONFIG_OSAL_MQTT_CORE_RETRY_BACKOFF_BASE_MS )

#endif /* CONFIG_OSAL_MQTT_IMPL_CORE */

#ifdef CONFIG_OSAL_MQTT_IMPL_ESP
/**
 * @brief Dimensions the buffer used to serialize and deserialize MQTT packets.
 * @note Specified in bytes.  Must be large enough to hold the maximum
 * anticipated MQTT payload.
 * @note This buffer is used for reading from the network.
 */
#define configOSAL_MQTT_ESP_NETWORK_BUFFER_SIZE_IN                      ( CONFIG_OSAL_MQTT_ESP_NETWORK_BUFFER_SIZE_IN )

/**
 * @brief Dimensions the buffer used to serialize and deserialize MQTT packets.
 * @note Specified in bytes.  Must be large enough to hold the maximum
 * anticipated MQTT payload.
 * @note This buffer is used for writing to the network.
 */
#define configOSAL_MQTT_ESP_NETWORK_BUFFER_SIZE_OUT                     ( CONFIG_OSAL_MQTT_ESP_NETWORK_BUFFER_SIZE_OUT )

/**
 * @brief The size (number of entries) of the callback registry used by esp-mqtt.
 */
#define configOSAL_MQTT_ESP_CALLBACK_REGISTRY_SIZE                        ( CONFIG_OSAL_MQTT_ESP_CALLBACK_REGISTRY_SIZE )

/**
 * @brief The limit of the outbox, in bytes, for the MQTT agent. 0 means no limit.
 */
#define configOSAL_MQTT_ESP_OUTBOX_LIMIT                                 ( CONFIG_OSAL_MQTT_ESP_OUTBOX_LIMIT )

/**
 * @brief The reconnect timeout (in milliseconds) for the MQTT client.
 */
#define configOSAL_MQTT_ESP_RECONNECT_TIMEOUT_MS                          ( CONFIG_OSAL_MQTT_ESP_RECONNECT_TIMEOUT_MS )

/**
 * @brief The network timeout (in milliseconds) for the MQTT client.
 */
#define configOSAL_MQTT_ESP_NETWORK_TIMEOUT_MS                          ( CONFIG_OSAL_MQTT_ESP_NETWORK_TIMEOUT_MS )

#endif /* CONFIG_OSAL_MQTT_IMPL_ESP */

#endif /* OSAL_MQTT_CONFIG_H */
