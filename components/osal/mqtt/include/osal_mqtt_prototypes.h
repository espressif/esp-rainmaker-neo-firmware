/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mqtt_prototypes.h
 * @brief Prototypes for the MQTT common component.
 */

#ifndef OSAL_MQTT_PROTOTYPES_H
#define OSAL_MQTT_PROTOTYPES_H

/* Standard, ESP-IDF includes */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "osal_err.h"

/* Platform common includes */
#include "osal_event_loop.h"

/* Configuration includes */
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Quality of Service (QoS) types */
typedef enum osal_mqtt_QoS {
    QoS0 = 0, /**< Delivery at most once. */
    QoS1 = 1, /**< Delivery at least once. */
    QoS2 = 2  /**< Delivery exactly once. */
} osal_mqtt_QoS_t;

/** MQTT Action types */
typedef enum {
    OSAL_MQTT_ACTION_PUBLISH = 0,
    OSAL_MQTT_ACTION_SUBSCRIBE,
    OSAL_MQTT_ACTION_UNSUBSCRIBE,
    OSAL_MQTT_ACTION_CONNECT,
    OSAL_MQTT_ACTION_DISCONNECT,
    OSAL_MQTT_ACTION_INIT,
    OSAL_MQTT_ACTION_DEINIT,
} osal_mqtt_action_type_t;

/**
 * @brief MQTT Last Will and Testament (LWT)
 *
 * If `enabled` is false, the broker will not publish any LWT on ungraceful disconnect
 * and the remaining fields are ignored.
 *
 * Pointed-to buffers must be kept alive until the MQTT client is deinitialized.
 */
typedef struct {
    /** If false, LWT is disabled and the rest of the struct is ignored. */
    bool enabled;
    /** LWT topic */
    const char *topic;
    /** Length of the LWT topic */
    size_t topic_len;
    /** LWT payload */
    const void *payload;
    /** Length of the LWT payload */
    size_t payload_len;
    /** QoS of the LWT publish */
    osal_mqtt_QoS_t qos;
    /** Retain flag of the LWT publish */
    bool retain;
} osal_mqtt_lwt_t;

/** MQTT Connection parameters */
typedef struct {
    /** MQTT Hostname */
    char *hostname;
    /** MQTT port */
    uint16_t port;
    /** ALPN protocols as a NULL-terminated list */
    const char **alpn_protocols;

    /** MQTT Username */
    char *username;
    /** MQTT Password */
    char *password;

    /** Client ID */
    char *client_id;
    /** Client Certificate in DER format or NULL-terminated PEM format */
    char *client_cert;
    /** Client Certificate length */
    size_t client_cert_len;
    /** Client Key in DER format or NULL-terminated PEM format */
    char *client_key;
    /** Client Key length */
    size_t client_key_len;

    /** Pointer for digital signature peripheral context */
    void *ds_data;

    /** If true, connect with a clean MQTT session (broker discards queued
     *  messages and subscriptions on disconnect). If false, the broker
     *  retains session state across disconnects (persistent session). */
    bool clean_session;

    /** Last Will and Testament. Set `lwt.enabled = false` to disable. */
    osal_mqtt_lwt_t lwt;
} osal_mqtt_conn_params_t;

/** MQTT Event Loop registration information */
typedef struct {
    /** Event base to post events to */
    osal_event_base_t event_base;

    /** Event ID to post events to */
    struct {
        int32_t connected;
        int32_t disconnected;
        int32_t published;
        int32_t subscribed;
        int32_t unsubscribed;
    } event_ids;
} osal_mqtt_event_loop_registration_info_t;

/** MQTT Event Loop identifier */
typedef struct {
    /** Main channel the data is intended for */
    uint32_t main;
    /** Sub-channel the data is intended for */
    uint32_t sub;
    /** Sequence number */
    uint32_t seq;
} osal_mqtt_event_loop_channel_t;

/** Event loop payload delivered when an MQTT operation completes */
typedef struct {
    /** Message ID */
    int32_t message_id;
    /** Status */
    osal_err_t status;
    /** Channel (for multiple handlers to route the data to the correct handler) */
    osal_mqtt_event_loop_channel_t channel;
} osal_mqtt_event_loop_data_on_complete_t;

/**
 * @brief MQTT Subscribe callback prototype
 *
 * @param[in] topic Topic on which the message was received
 * @param[in] topic_len Length of the topic
 * @param[in] payload Data received in the message
 * @param[in] payload_len Length of the data
 * @param[in] priv_data The private data passed during subscription
 */
typedef void (*osal_mqtt_subscribe_cb_t)( const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data );

/**
 * @brief MQTT SUBACK simulate function prototype
 *
 * @param[in] channel The channel information to post the event with.
 */
typedef void (*osal_mqtt_suback_simulate_t)( osal_mqtt_event_loop_channel_t *channel );

/**
 * @brief MQTT Init function prototype
 *
 * @param[in] conn_params The MQTT connection parameters.
 * - underlying buffers must be kept alive until the MQTT client is deinitialized, and are not managed by the MQTT implementation.
 * @param[in] event_loop_registration_info The event loop registration information. This is used to inform the implementation which events to post to the event loop.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_mqtt_init_t)( osal_mqtt_conn_params_t *conn_params, osal_mqtt_event_loop_registration_info_t *event_loop_registration_info );

/**
 * @brief MQTT Deinit function prototype
 *
 * Call this function after MQTT has disconnected.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_mqtt_deinit_t)( void );

/**
 * @brief MQTT Connect function prototype
 *
 * Starts the connection attempts to the MQTT broker.
 * This should ideally be called after successful network connection.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_mqtt_connect_t)( void );

/**
 * @brief MQTT Disconnect function prototype
 *
 * Disconnects from the MQTT broker.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_mqtt_disconnect_t)( void );

/**
 * @brief MQTT Drop function prototype
 *
 * Drops the connection to the MQTT broker *ungracefully*: tears down the
 * underlying TCP connection WITHOUT sending an MQTT DISCONNECT packet, so
 * the broker treats this client as abnormally disconnected and publishes
 * the client's Last Will and Testament (if one was registered).
 *
 * After drop(), the wrapper posts the same disconnect event and signals as
 * a normal disconnect() call, and the implementation is left in a state
 * where deinit() can be called safely.
 *
 * Intended for tests and fault-injection scenarios. Normal shutdown should
 * use disconnect().
 *
 * Platform support:
 *   - CoreMQTT impl: fully supported; tears the TLS transport directly.
 *   - esp-mqtt impl: best-effort ONLY. The public esp-mqtt API has no way
 *     to close the TCP connection without first sending an MQTT DISCONNECT
 *     packet, so the broker will NOT publish the LWT. The implementation
 *     falls back to a graceful disconnect and logs a warning.
 *
 * @see ::osal_mqtt_disconnect_t
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_mqtt_drop_t)( void );

/**
 * @brief MQTT Force Reconnect function prototype
 *
 * Forces a reconnect to the MQTT broker with a clean session.
 * Will block until a connection attempt is made.
 * This is useful for cases where the MQTT client is in an invalid state and needs to be re-initialized.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_mqtt_force_reconnect_t)( void );

/**
 * @brief MQTT Publish Message function prototype
 *
 * @param[in] channel The channel to post the event to.
 * @param[in] topic The MQTT topic on which the message should be published.
 * @param[in] topic_len Length of the topic.
 * @param[in] data Data to be published.
 * @param[in] data_len Length of the data.
 * @param[in] qos Quality of service for the message.
 * @param[in] retain If true, set the MQTT retain flag: broker stores this
 *            message as the retained message for the topic and delivers it
 *            to any future subscriber on match.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_mqtt_publish_t)( osal_mqtt_event_loop_channel_t *channel,
        const char *topic,
        size_t topic_len,
        void *data,
        size_t data_len,
        osal_mqtt_QoS_t qos,
        bool retain );

/**
 * @brief MQTT Subscribe function prototype
 *
 * @param[in] channel The channel to post the event to.
 * @param[in] topic The topic to be subscribed to.
 * @param[in] topic_len Length of the topic.
 * @param[in] cb The callback to be invoked when a message is received on the given topic.
 * @param[in] qos Quality of service for the subscription.
 * @param[in] priv_data Optional private data to be passed to the callback.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_mqtt_subscribe_t)( osal_mqtt_event_loop_channel_t *channel,
        const char *topic,
        size_t topic_len,
        osal_mqtt_subscribe_cb_t cb,
        osal_mqtt_QoS_t qos,
        void *priv_data );

/**
 * @brief MQTT Unsubscribe function prototype
 *
 * @param[in] channel The channel to post the event to.
 * @param[in] topic Topic from which to unsubscribe.
 * @param[in] topic_len Length of the topic.
 * @param[in] qos Quality of service for the unsubscription.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_mqtt_unsubscribe_t)( osal_mqtt_event_loop_channel_t *channel,
        const char *topic,
        size_t topic_len,
        osal_mqtt_QoS_t qos );

/**  MQTT implementation */
typedef struct {
    /** Flag to indicate if the MQTT config setup is done */
    bool setup_done;
    /** Pointer to MQTT Init function. */
    osal_mqtt_init_t init;
    /** Pointer to MQTT Deinit function. */
    osal_mqtt_deinit_t deinit;
    /** Pointer to MQTT Connect function. */
    osal_mqtt_connect_t connect;
    /** Pointer to MQTT Disconnect function */
    osal_mqtt_disconnect_t disconnect;
    /** Pointer to MQTT Drop function (ungraceful disconnect; triggers LWT) */
    osal_mqtt_drop_t drop;
    /** Pointer to MQTT Force Reconnect function */
    osal_mqtt_force_reconnect_t force_reconnect;
    /** Pointer to MQTT Publish function */
    osal_mqtt_publish_t publish;
    /** Pointer to MQTT Subscribe function */
    osal_mqtt_subscribe_t subscribe;
    /** Pointer to MQTT Unsubscribe function */
    osal_mqtt_unsubscribe_t unsubscribe;
} osal_mqtt_impl_t;

/**
 * @brief MQTT implementation setup function prototype
 *
 * @param[in] mqtt_impl Pointer to the MQTT implementation structure.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
typedef osal_err_t (*osal_mqtt_impl_setup_t)( osal_mqtt_impl_t *mqtt_impl );

#ifdef __cplusplus
}
#endif

#endif /* OSAL_MQTT_PROTOTYPES_H */
