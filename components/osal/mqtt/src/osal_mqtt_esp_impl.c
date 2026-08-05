/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * esp-mqtt implementation of common/osal_mqtt_impl.h.
 */

/* Public function declaration include. */
#include "osal_mqtt_impl.h"

/* Standard includes. */
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdatomic.h>

/* Configuration include. */
#include "osal_mqtt_config.h"

/* ESP-IDF includes. */
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_crt_bundle.h"

/* Platform common includes. */
#include "osal_semaphore.h"
#include "osal_mem_alloc.h"
#include "osal_event_loop.h"

/* Structure definitions *************************************/

/**
 * @brief channel registry entry.
 */
typedef struct __channel_registry_entry {
    int32_t message_id;
    osal_mqtt_event_loop_channel_t channel;
    struct __channel_registry_entry *next;
} channel_registry_entry_t;

/**
 * @brief channel registry.
 */
typedef struct {
    channel_registry_entry_t *head;
    osal_semaphore_handle_t mutex;
} channel_registry_t;

/* Global variables ***************************************/

/**
 * @brief Logging tag for ESP-IDF logging functions.
 */
static const char *TAG = "osal_mqtt_esp";

/**
 * @brief esp-mqtt client handle in use.
 */
static esp_mqtt_client_handle_t pClient = NULL;

/**
 * @brief esp-mqtt channel registry.
 */
static channel_registry_t xChannelRegistry;

/**
 * @brief event loop registration information.
 */
static osal_mqtt_event_loop_registration_info_t xEventLoopRegistrationInfo;

/**
 * @brief flag to indicate if the client is connected.
 */
static atomic_bool xIsConnected = false;

/**
 * @brief flag to indicate if the client has disconnected prior to a reconnect.
 */
static bool xHasDisconnectOccurred = false;



/* Static function declarations ***************************/

/**
 * @brief esp-mqtt event handler.
 */
static void osal_mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

/**
 * @brief initialize the channel registry.
 */
static void init_channel_registry( void );

/**
 * @brief deinitialize the callback registry.
 */
static void deinit_channel_registry( void );

/**
 * @brief lock the channel registry.
 * @return true if the lock was successful, false otherwise.
 */
static bool lock_channel_registry( void );

/**
 * @brief unlock the channel registry.
 * @return true if the unlock was successful, false otherwise.
 */
static bool unlock_channel_registry( void );

/**
 * @brief register a channel for a message ID.
 * @note This function must be called with the channel registry locked.
 * @param[in] message_id The message ID to register the channel for.
 * @param[in] channel The channel to register.
 *
 * @return OSAL_ERR_OK on success.
 * @return OSAL_ERR_NO_MEM if the channel registry is full.
 */
static osal_err_t register_channel_locked( int32_t message_id, osal_mqtt_event_loop_channel_t *channel );

/**
 * @brief get a channel for a message ID.
 * @note This function must be called with the channel registry locked.
 * @param[in] message_id the message ID to get the channel for.
 * @param[out] channel the channel to get.
 *
 * @return OSAL_ERR_OK on success.
 * @return OSAL_ERR_NO_MEM if the channel registry is full.
 */
static osal_err_t get_channel_locked( int32_t message_id, osal_mqtt_event_loop_channel_t *channel );

/**
 * @brief simulate SUBACKs for a given channel.
 * @param[in] channel The channel to simulate SUBACKs for.
 */
static void simulate_suback( osal_mqtt_event_loop_channel_t *channel );

/* Public function definitions ******************************/

/** MQTT Init function.
 * @param[in] conn_params The MQTT connection parameters.
 * @param[in] event_loop_registration_info The event loop registration information. This is used to inform the implementation which events to post to the event loop.
 *
 * @return ESP_OK on success.
 * @return error in case of any error.
 */
static osal_err_t init( osal_mqtt_conn_params_t *conn_params,
                        osal_mqtt_event_loop_registration_info_t *event_loop_registration_info );

/** MQTT Deinit function
 *
 * Call this function after MQTT has disconnected.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
static osal_err_t deinit( void );

/** MQTT Connect function
 *
 * Starts the connection attempts to the MQTT broker.
 * This should ideally be called after successful network connection.
 *
 * @return ESP_OK on success.
 * @return error in case of any error.
 */
static osal_err_t connect( void );

/** MQTT Disconnect function
 *
 * Disconnects from the MQTT broker.
 *
 * @return ESP_OK on success.
 * @return error in case of any error.
 */
static osal_err_t disconnect( void );

/** MQTT Force Reconnect function
 *
 * Forces a reconnect to the MQTT broker.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
static osal_err_t force_reconnect( void );

/** MQTT Publish Message function
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
 * @return ESP_OK on success.
 * @return error in case of any error.
 */
static osal_err_t publish( osal_mqtt_event_loop_channel_t *channel,
                           const char *topic,
                           size_t topic_len,
                           void *data,
                           size_t data_len,
                           osal_mqtt_QoS_t qos,
                           bool retain );

/** MQTT Subscribe function
 *
 * @param[in] channel The channel to post the event to.
 * @param[in] topic The topic to be subscribed to.
 * @param[in] topic_len Length of the topic.
 * @param[in] cb The callback to be invoked when a message is received on the given topic.
 * @param[in] qos Quality of service for the subscription.
 * @param[in] priv_data Optional private data to be passed to the callback.
 *
 * @return ESP_OK on success.
 * @return error in case of any error.
 */
static osal_err_t subscribe( osal_mqtt_event_loop_channel_t *channel,
                             const char *topic,
                             size_t topic_len,
                             osal_mqtt_subscribe_cb_t cb,
                             osal_mqtt_QoS_t qos,
                             void *priv_data );

/** MQTT Unsubscribe function
 *
 * @param[in] channel The channel to post the event to.
 * @param[in] topic Topic from which to unsubscribe.
 * @param[in] topic_len Length of the topic.
 * @param[in] qos Quality of service for the unsubscription.
 *
 * @return ESP_OK on success.
 * @return error in case of any error.
 */
static osal_err_t unsubscribe( osal_mqtt_event_loop_channel_t *channel,
                               const char *topic,
                               size_t topic_len,
                               osal_mqtt_QoS_t qos );

/** Static function definitions ****************************/

static void osal_mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    osal_err_t status = OSAL_ERR_OK;

    esp_mqtt_event_id_t osal_mqtt_event_id = (esp_mqtt_event_id_t) event_id;
    switch (osal_mqtt_event_id) {
    case MQTT_EVENT_ERROR:
        switch (event->error_handle->error_type) {
        case MQTT_ERROR_TYPE_TCP_TRANSPORT:
            ESP_LOGE( TAG, "MQTT client error: TCP transport error" );
            break;
        case MQTT_ERROR_TYPE_CONNECTION_REFUSED:
            ESP_LOGE( TAG, "MQTT client error: Connection refused" );
            break;
        case MQTT_ERROR_TYPE_SUBSCRIBE_FAILED:
            ESP_LOGE( TAG, "MQTT client error: Subscribe failed" );
            break;
        default:
            ESP_LOGE( TAG, "MQTT client error: Unknown error" );
            break;
        }
        break;

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI( TAG, "MQTT client connected" );
        xIsConnected = true;

        if ( xHasDisconnectOccurred ) {
            xHasDisconnectOccurred = false;
            if ( event->session_present ) {
                ESP_LOGI( TAG, "Session present. Simulating SUBACKs for all subscriptions" );
                osal_mqtt_subscription_simulate_subacks( simulate_suback );
            } else {
#if CONFIG_OSAL_MQTT_RESUBSCRIBE_ON_CLEAN_SESSION
                ESP_LOGI( TAG, "New session; attempting to resubscribe to all subscriptions" );
                osal_mqtt_subscription_attempt_resubscribe_all( subscribe );
#else
                ESP_LOGI( TAG, "New session; clearing subscription manager" );
                osal_mqtt_subscription_clear();
#endif
            }
        }

        /* Notify the client connected event. */
        osal_mqtt_event_notify_client_connected();
        osal_event_post(
            xEventLoopRegistrationInfo.event_base,
            xEventLoopRegistrationInfo.event_ids.connected,
            NULL, 0, OSAL_MAX_DELAY );
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI( TAG, "MQTT client disconnected" );
        xIsConnected = false;
        xHasDisconnectOccurred = true;

        /* Notify the client disconnected event. */
        osal_mqtt_event_notify_client_disconnected();
        osal_event_post(
            xEventLoopRegistrationInfo.event_base,
            xEventLoopRegistrationInfo.event_ids.disconnected,
            NULL, 0, OSAL_MAX_DELAY );
        break;

    case MQTT_EVENT_PUBLISHED:
    case MQTT_EVENT_UNSUBSCRIBED:
    case MQTT_EVENT_SUBSCRIBED:
        /* Make the event loop data. */
        osal_mqtt_event_loop_data_on_complete_t event_loop_data = {
            .message_id = event->msg_id,
            .status = OSAL_ERR_OK,
        };

        int32_t event_loop_event_id = 0;
        /* Set the status based on the event ID and error type. */
        if ( osal_mqtt_event_id == MQTT_EVENT_SUBSCRIBED ) {
            if ( event->error_handle->error_type == MQTT_ERROR_TYPE_SUBSCRIBE_FAILED ) {
                event_loop_data.status = OSAL_ERR_MQTT_SEND_FAILED;
            }
            event_loop_event_id = xEventLoopRegistrationInfo.event_ids.subscribed;
        } else if ( osal_mqtt_event_id == MQTT_EVENT_UNSUBSCRIBED ) {
            event_loop_event_id = xEventLoopRegistrationInfo.event_ids.unsubscribed;
        } else if ( osal_mqtt_event_id == MQTT_EVENT_PUBLISHED ) {
            event_loop_event_id = xEventLoopRegistrationInfo.event_ids.published;
        }

        /* Get the channel. */
        if ( !lock_channel_registry() ) {
            ESP_LOGW( TAG, "Failed to lock channel registry; channel will not be available" );
            goto __post_mqtt_event;
        }
        status = get_channel_locked( event->msg_id, &event_loop_data.channel );
        if ( status != OSAL_ERR_OK ) {
            ESP_LOGE( TAG, "Failed to get channel for message ID: %d; returning error.", event->msg_id );
            /* Ensure we release the registry mutex on error path */
            unlock_channel_registry();
            goto __post_mqtt_event;
        }
        unlock_channel_registry();
__post_mqtt_event:
        osal_event_post(
            xEventLoopRegistrationInfo.event_base,
            event_loop_event_id,
            &event_loop_data, sizeof( osal_mqtt_event_loop_data_on_complete_t ), OSAL_MAX_DELAY );
        break;

    case MQTT_EVENT_DATA:
        bool isHandled = osal_mqtt_subscription_handle_publish( event->topic, event->topic_len, (void *) event->data, event->data_len );
        if ( !isHandled && event->data_len > 0 && event->data != NULL ) {
            if ( event->topic_len > 0 && event->topic != NULL ) {
                ESP_LOGW( TAG, "Unsolicited message received from topic: %*s | data: %*s", event->topic_len, event->topic, event->data_len, event->data );
            } else {
                ESP_LOGW( TAG, "Unsolicited message received from topic: unknown | data: %*s", event->data_len, event->data );
            }
        }
        break;

    case MQTT_EVENT_BEFORE_CONNECT:
    case MQTT_EVENT_DELETED:
    case MQTT_USER_EVENT:
        /* These events are not handled. */
        break;

    default:
        ESP_LOGE( TAG, "Unhandled event ID: %" PRId32, event_id );
        break;
    }
}

static void init_channel_registry( void )
{
    xChannelRegistry.mutex = osal_semaphore_create_mutex();
}

static void deinit_channel_registry( void )
{
    osal_semaphore_delete( xChannelRegistry.mutex );
    xChannelRegistry.mutex = NULL;

    /* Free the channel registry. */
    channel_registry_entry_t *current = xChannelRegistry.head;
    while ( current ) {
        channel_registry_entry_t *next = current->next;
        free(current);
        current = next;
    }
    xChannelRegistry.head = NULL;
}

static bool lock_channel_registry( void )
{
    return osal_semaphore_take( xChannelRegistry.mutex, OSAL_MAX_DELAY ) == OSAL_ERR_OK;
}

static bool unlock_channel_registry( void )
{
    return osal_semaphore_give( xChannelRegistry.mutex ) == OSAL_ERR_OK;
}

static osal_err_t register_channel_locked( int32_t message_id, osal_mqtt_event_loop_channel_t *channel )
{
    /* Create a new entry. */
    channel_registry_entry_t *new_entry = (channel_registry_entry_t *)OSAL_CALLOC_EXTRAM( 1, sizeof( channel_registry_entry_t ) );
    if ( new_entry == NULL ) {
        return OSAL_ERR_NO_MEM;
    }

    /* Store the message ID and channel. */
    new_entry->message_id = message_id;
    new_entry->channel = (channel != NULL)
                         ? *channel
    : (osal_mqtt_event_loop_channel_t) {
        .main = UINT32_MAX, .sub = UINT32_MAX, .seq = UINT32_MAX
    };

    /* Add into channel registry, sorted by message ID in ascending order. */
    channel_registry_entry_t *current = xChannelRegistry.head;
    channel_registry_entry_t *prev = NULL;
    while (current) {
        if (current->message_id > message_id) {
            break;
        }
        prev = current;
        current = current->next;
    }

    /* Add the new entry. */
    if (prev) {
        prev->next = new_entry;
    } else {
        xChannelRegistry.head = new_entry;
    }
    new_entry->next = current;

    /* Return success. */
    return OSAL_ERR_OK;
}

static osal_err_t get_channel_locked( int32_t message_id, osal_mqtt_event_loop_channel_t *channel )
{
    if (channel == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    /* Get the channel for the message ID. */
    channel_registry_entry_t *current = xChannelRegistry.head;
    channel_registry_entry_t *prev = NULL;
    while (current) {
        if (current->message_id == message_id) {
            *channel = current->channel;

            /* Remove the entry from the registry. */
            if (prev) {
                prev->next = current->next;
            } else {
                xChannelRegistry.head = current->next;
            }
            free(current);

            return OSAL_ERR_OK;
        }
        prev = current;
        current = current->next;
    }
    return OSAL_ERR_INVALID_ARG;
}

static void simulate_suback( osal_mqtt_event_loop_channel_t *channel )
{
    // Make the event loop data.
    osal_mqtt_event_loop_data_on_complete_t event_loop_data = {
        .message_id = 0, // this is not used for simulated SUBACKs
        .status = OSAL_ERR_OK,
        .channel = *channel,
    };
    osal_event_post(
        xEventLoopRegistrationInfo.event_base,
        xEventLoopRegistrationInfo.event_ids.subscribed,
        &event_loop_data, sizeof( osal_mqtt_event_loop_data_on_complete_t ), OSAL_MAX_DELAY );
}

static osal_err_t init( osal_mqtt_conn_params_t *conn_params,
                        osal_mqtt_event_loop_registration_info_t *event_loop_registration_info )
{
    if (!conn_params || !event_loop_registration_info) {
        ESP_LOGE( TAG, "Invalid parameters: conn_params: %p, event_loop_registration_info: %p", conn_params, event_loop_registration_info );
        return OSAL_ERR_INVALID_ARG;
    }

    /* Initialize the channel registry. */
    init_channel_registry();

    /* Common initialization. */
    ESP_ERROR_CHECK( osal_mqtt_pre_init() );

    /* Initialize with configuration and connection parameters. */
    esp_mqtt_client_config_t config = {
        .broker = {
            .address = {
                .hostname = conn_params->hostname,
                .port = conn_params->port,
                .transport = MQTT_TRANSPORT_OVER_SSL,
            },
            .verification = {
                .crt_bundle_attach = esp_crt_bundle_attach,
                .alpn_protos = conn_params->alpn_protocols,
            },
        },
        .session = {
            .disable_clean_session = !conn_params->clean_session,
            .disable_keepalive = false,
            .keepalive = configOSAL_MQTT_KEEP_ALIVE_INTERVAL_S,
        },
        .credentials = {
            .client_id = conn_params->client_id,
            .authentication = {
                .certificate = conn_params->client_cert,
                .certificate_len = conn_params->client_cert_len,
                .key = conn_params->client_key,
                .key_len = conn_params->client_key_len,
                .ds_data = conn_params->ds_data,
            }
        },
        .network = {
            .reconnect_timeout_ms = configOSAL_MQTT_ESP_RECONNECT_TIMEOUT_MS,
            .timeout_ms = configOSAL_MQTT_ESP_NETWORK_TIMEOUT_MS,
        },
        .task = {
            .priority = configOSAL_MQTT_AGENT_TASK_PRIORITY,
            .stack_size = configOSAL_MQTT_AGENT_TASK_STACK_SIZE,
        },
        .buffer = {
            .size = configOSAL_MQTT_ESP_NETWORK_BUFFER_SIZE_IN,
            .out_size = configOSAL_MQTT_ESP_NETWORK_BUFFER_SIZE_OUT,
        },
        .outbox.limit = configOSAL_MQTT_ESP_OUTBOX_LIMIT,
    };

    /* Wire Last Will and Testament if enabled. esp-mqtt copies these values internally. */
    if ( conn_params->lwt.enabled ) {
        config.session.last_will.topic  = conn_params->lwt.topic;
        config.session.last_will.msg    = (const char *) conn_params->lwt.payload;
        config.session.last_will.msg_len = (int) conn_params->lwt.payload_len;
        config.session.last_will.qos    = (int) conn_params->lwt.qos;
        config.session.last_will.retain = conn_params->lwt.retain ? 1 : 0;
    }
    pClient = esp_mqtt_client_init( &config );

    /* NULL means a failure to initialize. */
    if ( pClient == NULL ) {
        ESP_LOGE( TAG, "Failed to initialize esp-mqtt client." );
        return OSAL_ERR_INVALID_STATE;
    }

    /* Register the esp-mqtt event handler. */
    esp_mqtt_client_register_event( pClient, MQTT_EVENT_ANY, osal_mqtt_event_handler, NULL );

    /* Copy the event loop registration information. */
    xEventLoopRegistrationInfo = *event_loop_registration_info;

    return OSAL_ERR_OK;
}

static osal_err_t deinit( void )
{
    /* Deinitialize the esp-mqtt client. */
    esp_err_t err = esp_mqtt_client_destroy( pClient );
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "Failed to destroy esp-mqtt client: %d", (int)err );
        return OSAL_ERR_INVALID_STATE;
    }

    pClient = NULL;

    /* Deinitialize the channel registry. */
    deinit_channel_registry();

    /* Common post-deinit. */
    osal_mqtt_post_deinit();

    return OSAL_ERR_OK;
}

static osal_err_t connect( void )
{
    ESP_LOGD(TAG, "Connecting to MQTT broker");

    /* Start the connection attempts to the MQTT broker. */
    esp_err_t err = esp_mqtt_client_start( pClient );
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "Failed to start esp-mqtt client: %d", (int)err );
        return OSAL_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Connected to MQTT broker");
    return OSAL_ERR_OK;
}

static osal_err_t disconnect( void )
{
    /* Stop the connection attempts to the MQTT broker. */
    esp_err_t err = esp_mqtt_client_stop( pClient );
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "Failed to stop esp-mqtt client: %d", (int)err );
        return OSAL_ERR_INVALID_STATE;
    }

    /* Notify the client disconnected event. */
    osal_mqtt_event_notify_client_disconnected();

    /* Post the disconnected event to the event loop. */
    osal_event_post(
        xEventLoopRegistrationInfo.event_base,
        xEventLoopRegistrationInfo.event_ids.disconnected,
        NULL, 0, OSAL_MAX_DELAY );

    return OSAL_ERR_OK;
}

static osal_err_t force_reconnect( void )
{
    osal_err_t status = OSAL_ERR_OK;

    /* Stop the connection attempts to the MQTT broker. */
    status = disconnect();
    if ( status != OSAL_ERR_OK ) {
        return status;
    }

    /* Start the connection attempts to the MQTT broker. */
    return connect();
}

static osal_err_t drop( void )
{
    /* Best-effort only on esp-mqtt.
     *
     * The public esp-mqtt API has no way to close the TCP connection without
     * first sending an MQTT DISCONNECT packet: esp_mqtt_client_disconnect(),
     * esp_mqtt_client_stop() and esp_mqtt_client_destroy() all go through
     * send_disconnect_msg() when the client is in CONNECTED state. As a
     * result the broker treats this as a graceful disconnect and will NOT
     * publish the Last Will and Testament.
     *
     * We therefore fall back to the same shutdown as disconnect() and log a
     * warning. Callers that rely on LWT firing must use the CoreMQTT impl. */
    ESP_LOGW( TAG,
              "drop() on esp-mqtt is a graceful disconnect: broker will not "
              "publish LWT. Use CoreMQTT impl if you need the ungraceful path." );

    return disconnect();
}

static osal_err_t publish( osal_mqtt_event_loop_channel_t *channel,
                           const char *topic,
                           size_t topic_len,
                           void *data,
                           size_t data_len,
                           osal_mqtt_QoS_t qos,
                           bool retain )
{
    if ( topic == NULL || topic_len == 0 ) {
        ESP_LOGE( TAG, "No topic provided for publish" );
        return OSAL_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Publishing message to topic: %s", topic);
    if ( !xIsConnected ) {
        ESP_LOGE( TAG, "MQTT client is disconnected" );
        return OSAL_ERR_MQTT_NOT_CONNECTED;
    }

    /* Lock the channel registry. */
    if ( !lock_channel_registry() ) {
        return OSAL_ERR_INVALID_STATE;
    }

    osal_err_t status = OSAL_ERR_OK;
    /* Publish and get message ID. */
    int msg_id_scope = esp_mqtt_client_publish( pClient, topic, data, data_len, qos, retain ? 1 : 0 );

    /* Handle errors. */
    if ( msg_id_scope == -1 ) {
        // generic error.
        ESP_LOGE( TAG, "Failed to publish message (send failed)" );
        status = OSAL_ERR_MQTT_SEND_FAILED;
        goto __publish_locked_cleanup;
    } else if ( msg_id_scope == -2 ) {
        // outbox is full.
        ESP_LOGE( TAG, "Failed to publish message (outbox is full)" );
        status = OSAL_ERR_NO_MEM;
        goto __publish_locked_cleanup;
    } else {
        /* Register the channel. */
        status = register_channel_locked( msg_id_scope, channel );
        if ( status != OSAL_ERR_OK ) {
            /* If the channel registration failed, return the error. */
            ESP_LOGE( TAG, "Task %s: Failed to register channel for message ID: %d; returning error.", pcTaskGetName( NULL ), msg_id_scope );
            goto __publish_locked_cleanup;
        }
    }

    ESP_LOGD(TAG, "Published message to topic: %s", topic);
__publish_locked_cleanup:
    unlock_channel_registry();
    return status;
}

static osal_err_t subscribe( osal_mqtt_event_loop_channel_t *channel,
                             const char *topic,
                             size_t topic_len,
                             osal_mqtt_subscribe_cb_t cb,
                             osal_mqtt_QoS_t qos,
                             void *priv_data )
{
    if ( topic == NULL || topic_len == 0 ) {
        ESP_LOGE( TAG, "No topic provided for subscribe" );
        return OSAL_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Subscribing to topic: %s", topic);
    if ( !xIsConnected ) {
        ESP_LOGE( TAG, "MQTT client is disconnected" );
        return OSAL_ERR_MQTT_NOT_CONNECTED;
    }

    /* Lock the channel registry. */
    if ( !lock_channel_registry() ) {
        return OSAL_ERR_INVALID_STATE;
    }

    osal_err_t status = OSAL_ERR_OK;
    /* Subscribe and get message ID. */
    int msg_id_scope = esp_mqtt_client_subscribe( pClient, topic, qos );

    /* Handle errors. */
    if ( msg_id_scope == -1 ) {
        ESP_LOGE( TAG, "Failed to subscribe to topic (send failed)" );
        status = OSAL_ERR_MQTT_SEND_FAILED;
        goto __subscribe_locked_cleanup;
    } else if ( msg_id_scope == -2 ) {
        ESP_LOGE( TAG, "Failed to subscribe to topic (no memory)" );
        status = OSAL_ERR_NO_MEM;
        goto __subscribe_locked_cleanup;
    } else {
        /* Register the channel. */
        status = register_channel_locked( msg_id_scope, channel );
        if ( status != OSAL_ERR_OK ) {
            /* If the channel registration failed, return the error. */
            ESP_LOGE( TAG, "Task %s: Failed to register channel for message ID: %d; returning error.", pcTaskGetName( NULL ), msg_id_scope );
            goto __subscribe_locked_cleanup;
        }
    }

    /* Add the subscription to the subscription list. */
    osal_mqtt_subscription_add( topic, topic_len, channel, cb, qos, priv_data );

    ESP_LOGD(TAG, "Subscribed to topic: %s", topic);
__subscribe_locked_cleanup:
    unlock_channel_registry();
    return status;
}

static osal_err_t unsubscribe( osal_mqtt_event_loop_channel_t *channel,
                               const char *topic,
                               size_t topic_len,
                               osal_mqtt_QoS_t qos )
{
    if ( topic == NULL || topic_len == 0 ) {
        ESP_LOGE( TAG, "No topic provided for unsubscribe" );
        return OSAL_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Unsubscribing from topic: %s", topic);
    if ( !xIsConnected ) {
        ESP_LOGE( TAG, "MQTT client is disconnected" );
        return OSAL_ERR_MQTT_NOT_CONNECTED;
    }

    /* Lock the channel registry. */
    if ( !lock_channel_registry() ) {
        return OSAL_ERR_INVALID_STATE;
    }

    osal_err_t status = OSAL_ERR_OK;
    /* Unsubscribe and get message ID. */
    int msg_id_scope = esp_mqtt_client_unsubscribe( pClient, topic );

    /* Handle errors. */
    if ( msg_id_scope == -1 ) {
        ESP_LOGE( TAG, "Failed to unsubscribe from topic (send failed)" );
        status = OSAL_ERR_MQTT_SEND_FAILED;
        goto __unsubscribe_locked_cleanup;
    } else if ( msg_id_scope == -2 ) {
        ESP_LOGE( TAG, "Failed to unsubscribe from topic (no memory)" );
        status = OSAL_ERR_NO_MEM;
        goto __unsubscribe_locked_cleanup;
    } else {
        /* Register the channel. */
        status = register_channel_locked( msg_id_scope, channel );
        if ( status != OSAL_ERR_OK ) {
            /* If the channel registration failed, return the error. */
            ESP_LOGE( TAG, "Task %s: Failed to register callback for message ID: %d; returning error.", pcTaskGetName( NULL ), msg_id_scope );
            goto __unsubscribe_locked_cleanup;
        }
    }

    /* Remove the subscription from the subscription list. */
    osal_mqtt_subscription_remove( topic, topic_len );

    ESP_LOGD(TAG, "Unsubscribed from topic: %s", topic);
__unsubscribe_locked_cleanup:
    unlock_channel_registry();
    return status;
}

/** Public function definitions ****************************/

osal_err_t osal_mqtt_impl_setup(osal_mqtt_impl_t *mqtt_impl)
{
    mqtt_impl->init = init;
    mqtt_impl->deinit = deinit;
    mqtt_impl->connect = connect;
    mqtt_impl->disconnect = disconnect;
    mqtt_impl->drop = drop;
    mqtt_impl->force_reconnect = force_reconnect;
    mqtt_impl->publish = publish;
    mqtt_impl->subscribe = subscribe;
    mqtt_impl->unsubscribe = unsubscribe;
    mqtt_impl->setup_done = true;

    return OSAL_ERR_OK;
}
