/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mqtt_subscription_manager.h
 * @brief Functions for managing MQTT subscriptions.
 */
#ifndef OSAL_MQTT_SUBSCRIPTION_MANAGER_H
#define OSAL_MQTT_SUBSCRIPTION_MANAGER_H

#include <stdbool.h>
#include "osal_mqtt_config.h"
#include "osal_mqtt_prototypes.h"

/**
 * @brief Maximum number of subscriptions maintained by the subscription manager
 * simultaneously in a list.
 */
#ifndef OSAL_MQTT_MAX_SUBSCRIPTIONS
#define OSAL_MQTT_MAX_SUBSCRIPTIONS    ( configOSAL_MQTT_SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS )
#endif

/* *INDENT-OFF* */
    #ifdef __cplusplus
        extern "C" {
    #endif
/* *INDENT-ON* */

    /**
     * @brief An element in the list of subscriptions.
     *
     * This subscription manager implementation expects that the array of the
     * subscription elements used for storing subscriptions to be initialized to 0.
     *
     * @note This implementation allows multiple tasks to subscribe to the same topic.
     * In this case, another element is added to the subscription list, differing
     * in the intended publish callback. Also note that the topic filters are not
     * copied in the subscription manager and hence the topic filter strings need to
     * stay in scope until unsubscribed.
     */
    typedef struct {
        osal_mqtt_event_loop_channel_t channel; /**< Channel for the subscription. */
        osal_mqtt_subscribe_cb_t callback; /**< Callback function for the subscription. */
        void *priv_data;  /**< Private data for the subscription callback. */
        osal_mqtt_QoS_t qos; /**< Quality of service for the subscription. */
        uint16_t usFilterStringLength; /**< Length of the topic filter string. */
        char *pcSubscriptionFilterString;  /**< Topic filter string of subscription. */
    } osal_mqtt_subscription_element_t;

    /**
     * @brief Initialize the subscription manager, i.e., create a blank subscription list.
     */
    void osal_mqtt_subscription_init( void );

    /**
     * @brief Deinitialize the subscription manager.
     */
    void osal_mqtt_subscription_deinit( void );

    /**
     * @brief Get the subscription list used by the manager.
     * @return Pointer to the first element of the subscription list.
     */
    osal_mqtt_subscription_element_t *osal_mqtt_subscription_get_list( void );

    /**
     * @brief Add a subscription to the subscription list.
     *
     * @note Multiple tasks can be subscribed to the same topic with different
     * context-callback pairs. However, a single context-callback pair may only be
     * associated to the same topic filter once.
     *
     * @param[in] pcTopicFilterString Topic filter string of subscription.
     * @param[in] usTopicFilterLength Length of topic filter string.
     * @param[in] channel Pointer to the channel for the subscription.
     * @param[in] callback Callback function for the subscription.
     * @param[in] qos Quality of service for the subscription.
     * @param[in] priv_data Private data for the subscription callback.
     *
     * @return `true` if subscription added or exists, `false` if insufficient memory.
     */
    bool osal_mqtt_subscription_add( const char *pcTopicFilterString,
                                     uint16_t usTopicFilterLength,
                                     osal_mqtt_event_loop_channel_t *channel,
                                     osal_mqtt_subscribe_cb_t callback,
                                     osal_mqtt_QoS_t qos,
                                     void *priv_data );

    /**
     * @brief Remove a subscription from the subscription list.
     *
     * @note If the topic filter exists multiple times in the subscription list,
     * then every instance of the subscription will be removed.
     *
     * @param[in] pcTopicFilterString Topic filter of subscription.
     * @param[in] usTopicFilterLength Length of topic filter.
     */
    void osal_mqtt_subscription_remove( const char *pcTopicFilterString,
                                        uint16_t usTopicFilterLength );

    /**
     * @brief Clear the subscription list.
     */
    void osal_mqtt_subscription_clear( void );

    /**
     * @brief Handle incoming publishes by invoking the callbacks registered
     * for the incoming publish's topic filter.
     *
     * @param[in] topic Topic on which the message was received
     * @param[in] topic_len Length of the topic
     * @param[in] payload Data received in the message
     * @param[in] payload_len Length of the data
     *
     * @return `true` if an application callback could be invoked;
     *  `false` otherwise.
     */
    bool osal_mqtt_subscription_handle_publish( const char *topic,
            size_t topic_len,
            void *payload,
            size_t payload_len );

    /**
     * @brief Attempt to resubscribe to all subscriptions.
     *
     * @note This function is used to attempt to resubscribe to all subscriptions
     * after a connection has been lost.
     *
     * @param[in] subscribe_fn Function to subscribe to a topic.
     */
    void osal_mqtt_subscription_attempt_resubscribe_all(osal_mqtt_subscribe_t subscribe_fn);

    /**
     * @brief Simulate SUBACKs for all subscriptions except for QoS0.
     *
     * This is used when a session can be reused after a disconnect.
     * In this case, the SUBACKs are not sent by the broker, so we need to simulate them.
     * @param[in] suback_simulate_fn Function to simulate SUBACKs.
     */
    void osal_mqtt_subscription_simulate_subacks( osal_mqtt_suback_simulate_t suback_simulate_fn );

/* *INDENT-OFF* */
    #ifdef __cplusplus
        } /* extern "C" */
    #endif
/* *INDENT-ON* */

#endif /* OSAL_MQTT_SUBSCRIPTION_MANAGER_H */
