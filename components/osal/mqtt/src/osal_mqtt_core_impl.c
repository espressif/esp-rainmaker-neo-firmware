/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * Core-MQTT implementation of osal_mqtt_impl.h.
 */

/* Public functions include. */
#include "core_mqtt_serializer.h"
#include "osal_mqtt_impl.h"

/* Configurations include. */
#include "osal_mqtt_config.h"

/* Standard includes. */
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

/* coreMQTT libraries include. */
#include "core_mqtt_agent.h"
#include "osal_mqtt_prototypes.h"
#include "network_transport_port.h"
#include "backoff_algorithm.h"

/* coreMQTT-Agent port include. */
#include "core_agent_message.h"
#include "core_agent_command_pool.h"

/* Platform common includes. */
#include "osal_mem_alloc.h"
#include "osal_log.h"
#include "osal_queue.h"
#include "osal_task.h"

/* Struct definitions *********************************************************/

/**
 * @brief Defines the structure to use as the command callback context in this
 * implementation.
 */
struct MQTTAgentCommandContext {
    int msg_id; /* Message ID of the command sent. */
    osal_mqtt_action_type_t command_type; /* Type of MQTT command. */
    void *p_command_payload;  /* Pointer to MQTT command payload. */
    void *p_command_info;  /* Pointer to MQTT command info. */
    int32_t event_loop_event_id; /* Event loop event ID to post to. */
    osal_mqtt_event_loop_channel_t event_loop_channel; /* channel to post to the event loop. */
};

/* Global variables ***********************************************************/

/**
 * @brief Logging tag for ESP-IDF logging functions.
 */
static const char *TAG = "osal_mqtt_core";

/**
 * @brief Global entry time into the application to use as a reference timestamp
 * in the #prvGetTimeMs function. #prvGetTimeMs will always return the difference
 * between the current time and the global entry time. This will reduce the chances
 * of overflow for the 32 bit unsigned integer used for holding the timestamp.
 */
static uint32_t ulGlobalEntryTimeMs;

/**
 * @brief Network buffer for coreMQTT.
 */
static uint8_t *ucNetworkBuffer = NULL;

/**
 * @brief Message queue used to deliver commands to the agent task.
 */
static MQTTAgentMessageContext_t xCommandQueue;

/**
 * @brief Global MQTT Agent context used by every task.
 */
MQTTAgentContext_t xGlobalMqttAgentContext;

/**
 * @brief The global network context used to store the credentials
 * and TLS connection.
 */
static NetworkContext_t xNetworkContext = {0};

/**
 * @brief MQTT Common event loop registration information.
 */
static osal_mqtt_event_loop_registration_info_t xEventLoopRegistrationInfo;

/**
 * @brief Pointer to the connection parameters supplied by the caller at init().
 * The caller owns the underlying buffers and must keep them alive until deinit().
 * Used by prvCoreMqttAgentConnect() to read LWT settings.
 */
static osal_mqtt_conn_params_t *pxConnParams = NULL;

/**
 * @brief Boolean to track the lifespan of agent task.
 */
static bool agent_task_is_alive = false;

/**
 * @brief Boolean to track if the agent task should keep running.
 */
static bool should_keep_agent_task_alive = false;

/**
 * @brief Boolean to track if the client is connected.
 */
static atomic_bool xIsConnected = false;

/**
 * @brief Boolean to track if the session should be cleaned.
 */
static bool xCleanSession = false;

/**
 * @brief The agent task handle.
 */
static osal_task_handle_t agent_task_handle = NULL;

/**
 * @brief The agent task event group.
 */
static osal_event_group_handle_t sync_event_group = NULL;
#define SYNC_EVENT_GROUP_BITS_AGENT_TASK_EXIT (1 << 0) // bit 0 is used to signal the agent task to exit.

/**
 * @brief The message ID for the next message sent by this implementation.
 */
static atomic_int ulMessageId = 0;

/**
 * @brief Default command info (do nothing on complete).
 */
static MQTTAgentCommandInfo_t xDefaultCommandInfo;

/* Static function declarations ***************************/

/**
 * @brief Convert coreMQTT MQTTStatus_t to osal_err_t.
 */
static osal_err_t mqtt_agent_status_to_os_err( MQTTStatus_t status );

/**
 * @brief The timer query function provided to the MQTT context.
 *
 * @return Time in milliseconds.
 */
static uint32_t prvGetTimeMs( void );


/**
 * @brief Copy the connection parameters to the network context.
 *
 * @param[in] conn_params The connection parameters.
 *
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
static osal_err_t prvCopyToNetworkContext( osal_mqtt_conn_params_t *conn_params );

/**
 * @brief Get a command context buffer from the queue.
 *
 * @param[in] msg_id The message ID to set in the command context.
 * @param[in] command_type The type of command.
 * @param[in] p_command_payload The command payload to set in the command context.
 * @param[in] channel Channel for the command.
 *
 * @return A pointer to the command context buffer.
 */
static MQTTAgentCommandContext_t *prvGetCommandContext( int msg_id, osal_mqtt_action_type_t command_type, void *p_command_payload, osal_mqtt_event_loop_channel_t *channel );

/**
 * @brief Free a command context buffer.
 *
 * @param[in] pxCommandContext The command context buffer to free.
 */
static void prvFreeCommandContext( MQTTAgentCommandContext_t *pxCommandContext );

/**
 * @brief Free a command payload buffer.
 *
 * @param[in] command_type The type of command.
 * @param[in] p_command_payload The command payload buffer to free.
 */
static void prvFreeCommandPayload( osal_mqtt_action_type_t command_type, void *p_command_payload );

/**
 * @brief Fan out the incoming publishes to the callbacks registered by different
 * tasks. If there are no callbacks registered for the incoming publish, it will be
 * passed to the unsolicited publish handler.
 *
 * @param[in] pMqttAgentContext Agent context.
 * @param[in] packetId Packet ID of publish.
 * @param[in] pxPublishInfo Info of incoming publish.
 */
static void prvIncomingPublishCallback( MQTTAgentContext_t *pMqttAgentContext,
                                        uint16_t packetId,
                                        MQTTPublishInfo_t *pxPublishInfo );

/**
 * @brief The callback to execute when the broker ACKs a PUBLISH/SUBSCRIBE/UNSUBSCRIBE message.
 *
 * @param[in] pCmdCallbackContext Context of the initial command.
 * @param[in] pReturnInfo The result of the command.
 */
static void prvOnCompleteCommandCallback( MQTTAgentCommandContext_t *pCmdCallbackContext,
        MQTTAgentReturnInfo_t *pReturnInfo );

/**
 * @brief Initializes an MQTT Agent context, including transport interface,
 * network buffer, and publish callback.
 *
 * @return `MQTTSuccess` if the initialization succeeds, else `MQTTBadParameter`.
 */
static MQTTStatus_t prvCoreMqttAgentInit( NetworkContext_t *pxNetworkContext );

/**
 * @brief Sends an MQTT Connect packet over the already connected TCP socket.
 *
 * @param[in] xCleanSession If a clean session should be established.
 *
 * @return `MQTTSuccess` if connection succeeds, else appropriate error code
 * from MQTT_Connect.
 */
static MQTTStatus_t prvCoreMqttAgentConnect( bool xCleanSession );

/**
 * @brief Calculate and perform an exponential backoff with jitter delay for
 * the next retry attempt of a failed network operation with the server.
 *
 * The function generates a random number, calculates the next backoff period
 * with the generated random number, and performs the backoff delay operation if the
 * number of retries have not exhausted.
 *
 * @note The backoff period is calculated using the backoffAlgorithm library.
 *
 * @param[in,out] pxRetryParams The context to use for backoff period calculation
 * with the backoffAlgorithm library.
 *
 * @return true if calculating the backoff period was successful; otherwise false
 * if there was failure in random number generation OR all retry attempts had exhausted.
 */
static bool prvBackoffForRetry( BackoffAlgorithmContext_t *pxRetryParams );

/**
 * @brief The task that runs the MQTT agent.
 *
 * This task runs in a loop, and handles the following:
 * - Connect to MQTT broker, if not connected.
 * - If connected, run the command loop.
 * - If not connected, backoff and retry to connect unless explicitly disconnected.
 *
 * @param[in] pvParameters Parameters as passed at the time of task creation. Not
 * used in this example.
 */
static void prvCoreMqttAgentTask( void *pvParameters );

/**
 * @brief This function starts the coreMQTT-Agent task.
 *
 * @return true if task created successfully, false otherwise.
 */
static bool prvStartCoreMqttAgent( void );

/**
 * @brief Simulate SUBACKs for a given channel.
 *
 * @param[in] channel The channel to simulate SUBACKs for.
 */
static void prvCoreMqttAgentSubackSimulate( osal_mqtt_event_loop_channel_t *channel );

/** MQTT Init function.
 * @param[in] conn_params The MQTT connection parameters.
 * @param[in] event_loop_registration_info The event loop registration information. This is used to inform the implementation which events to post to the event loop.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
static osal_err_t init( osal_mqtt_conn_params_t *conn_params, osal_mqtt_event_loop_registration_info_t *event_loop_registration_info );

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
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
static osal_err_t connect( void );

/** MQTT Disconnect function
 *
 * Disconnects from the MQTT broker.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
static osal_err_t disconnect( void );

/** MQTT Drop function
 *
 * Ungracefully tears down the TCP/TLS connection without sending an MQTT
 * DISCONNECT so the broker publishes the LWT. Mirrors disconnect() in terms
 * of signals posted to the event loop.
 *
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
static osal_err_t drop( void );

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
 * @return OSAL_ERR_OK on success.
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
 * @return OSAL_ERR_OK on success.
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
 * @return OSAL_ERR_OK on success.
 * @return error in case of any error.
 */
static osal_err_t unsubscribe( osal_mqtt_event_loop_channel_t *channel,
                               const char *topic,
                               size_t topic_len,
                               osal_mqtt_QoS_t qos );

/** Static function definitions ****************************/

static osal_err_t mqtt_agent_status_to_os_err( MQTTStatus_t status )
{
    switch (status) {
    case MQTTSuccess:
        return OSAL_ERR_OK;
    case MQTTBadParameter:
        return OSAL_ERR_INVALID_ARG;
    case MQTTNoMemory:
        return OSAL_ERR_NO_MEM;
    case MQTTSendFailed:
        return OSAL_ERR_MQTT_SEND_FAILED;
    case MQTTRecvFailed:
        return OSAL_ERR_MQTT_RECV_FAILED;
    case MQTTBadResponse:
        return OSAL_ERR_MQTT_BAD_RESPONSE;
    case MQTTServerRefused:
        return OSAL_ERR_MQTT_SERVER_REFUSED;
    case MQTTNoDataAvailable:
        return OSAL_ERR_MQTT_NO_DATA_AVAILABLE;
    case MQTTIllegalState:
        return OSAL_ERR_INVALID_STATE;
    case MQTTStateCollision:
        return OSAL_ERR_MQTT_STATE_COLLISION;
    case MQTTKeepAliveTimeout:
        return OSAL_ERR_MQTT_KEEP_ALIVE_TIMEOUT;
    case MQTTNeedMoreBytes:
        return OSAL_ERR_MQTT_NEED_MORE_BYTES;
    default:
        return OSAL_ERR_MQTT_BAD_RESPONSE;
    }
}

static uint32_t prvGetTimeMs( void )
{
    osal_tick_type_t xTickCount = 0;
    uint32_t ulTimeMs = 0UL;

    /* Get the current tick count. */
    xTickCount = osal_task_get_tick_count();

    /* Convert the ticks to milliseconds. */
    ulTimeMs = ( uint32_t ) osal_ms_from_ticks( xTickCount );

    /* Reduce ulGlobalEntryTimeMs from obtained time so as to always return the
     * elapsed time in the application. */
    ulTimeMs = ( uint32_t ) ( ulTimeMs - ulGlobalEntryTimeMs );

    return ulTimeMs;
}

static osal_err_t prvCopyToNetworkContext( osal_mqtt_conn_params_t *conn_params )
{
    if (conn_params == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    // translate connection parameters to Core-MQTT network context.
    vTlsContextSetEndpoint( &xNetworkContext, conn_params->hostname, conn_params->port, conn_params->alpn_protocols );

    /** CoreMQTT requires a client identifier. Generate a random one if not provided.
     * Note: If allocated here, the client ID will be dynamically assigned and should be freed later.
     *
     * Client identifier / username / password are MQTT CONNECT fields (not TLS transport fields);
     * they are read from conn_params directly when building MQTTConnectInfo, so they are not stored
     * on the network context. */
    if (conn_params->client_id == NULL) {
        int id_num = rand() % 1000;
        int len = snprintf(NULL, 0, "coremqtt_client_%d", id_num);
        char *pcDefaultClientIdentifier = (char *)OSAL_CALLOC_EXTRAM(len + 1, sizeof(char));
        if (pcDefaultClientIdentifier == NULL) {
            return OSAL_ERR_NO_MEM;
        }
        snprintf(pcDefaultClientIdentifier, len + 1, "coremqtt_client_%d", id_num);
        conn_params->client_id = pcDefaultClientIdentifier;
    }
    vTlsContextSetClientCredentials( &xNetworkContext,
                                     conn_params->client_cert, conn_params->client_cert_len,
                                     conn_params->client_key, conn_params->client_key_len );
    vTlsContextSetDsData( &xNetworkContext, conn_params->ds_data );

    return OSAL_ERR_OK;
}

static MQTTAgentCommandContext_t *prvGetCommandContext( int msg_id, osal_mqtt_action_type_t command_type, void *p_command_payload, osal_mqtt_event_loop_channel_t *channel )
{
    MQTTAgentCommandContext_t *pxCommandContext = OSAL_MALLOC_EXTRAM( sizeof( MQTTAgentCommandContext_t ) );
    if (pxCommandContext == NULL) {
        OSAL_LOGE( TAG, "Failed to allocate memory for command context." );
        return NULL;
    }

    pxCommandContext->msg_id = msg_id;
    pxCommandContext->command_type = command_type;
    pxCommandContext->p_command_payload = p_command_payload;
    pxCommandContext->p_command_info = NULL; /* This is set later. */
    pxCommandContext->event_loop_event_id = 0; /* This is set later. */
    pxCommandContext->event_loop_channel = (channel != NULL)
                                           ? *channel
    : (osal_mqtt_event_loop_channel_t) {
        .main = UINT32_MAX, .sub = UINT32_MAX, .seq = UINT32_MAX
    };
    return pxCommandContext;
}

static void prvFreeCommandContext( MQTTAgentCommandContext_t *pxCommandContext )
{
    if (pxCommandContext == NULL) {
        return;
    }

    if (pxCommandContext->p_command_payload != NULL) {
        prvFreeCommandPayload( pxCommandContext->command_type, pxCommandContext->p_command_payload );
    }
    if (pxCommandContext->p_command_info != NULL) {
        free( pxCommandContext->p_command_info );
    }

    free( pxCommandContext );
}

static void prvFreeCommandPayload( osal_mqtt_action_type_t command_type, void *p_command_payload )
{
    MQTTPublishInfo_t *p_publish_info = NULL;
    MQTTAgentSubscribeArgs_t *p_subscribe_args = NULL;
    MQTTSubscribeInfo_t *p_subscribe_info = NULL;

    switch (command_type) {
    case OSAL_MQTT_ACTION_PUBLISH:
        p_publish_info = (MQTTPublishInfo_t *) p_command_payload;
        if (p_publish_info != NULL) {
            if (p_publish_info->pTopicName != NULL) {
                free( (char *) p_publish_info->pTopicName );
            }
            if (p_publish_info->pPayload != NULL) {
                free( (void *) p_publish_info->pPayload );
            }
            free( p_publish_info );
        }
        break;
    case OSAL_MQTT_ACTION_SUBSCRIBE:
    case OSAL_MQTT_ACTION_UNSUBSCRIBE:
        p_subscribe_args = (MQTTAgentSubscribeArgs_t *) p_command_payload;
        if (p_subscribe_args != NULL) {
            p_subscribe_info = p_subscribe_args->pSubscribeInfo;
            if (p_subscribe_info != NULL) {
                for (size_t i = 0; i < p_subscribe_args->numSubscriptions; i++) {
                    if (p_subscribe_info[i].pTopicFilter != NULL) {
                        free( (char *) p_subscribe_info[i].pTopicFilter );
                    }
                }
                free( p_subscribe_info );
            }
            free( p_subscribe_args );
        }
        break;
    default:
        break;
    }
}

static void prvIncomingPublishCallback( MQTTAgentContext_t *pMqttAgentContext,
                                        uint16_t packetId,
                                        MQTTPublishInfo_t *pxPublishInfo )
{
    bool xPublishHandled = false;
    char cOriginalChar, * pcLocation;

    ( void ) packetId;

    /* Fan out the incoming publishes to the callbacks registered using
     * subscription manager. */
    xPublishHandled = osal_mqtt_subscription_handle_publish( pxPublishInfo->pTopicName,
                      pxPublishInfo->topicNameLength,
                      (void *) pxPublishInfo->pPayload,
                      pxPublishInfo->payloadLength );

    /* If there are no callbacks to handle the incoming publishes,
     * handle it as an unsolicited publish. */
    if ( !xPublishHandled ) {
        /* Ensure the topic string is terminated for printing.  This will over-
         * write the message ID, which is restored afterwards. */
        if ( pxPublishInfo->topicNameLength > 0 && pxPublishInfo->pTopicName != NULL ) {
            pcLocation = ( char * ) & ( pxPublishInfo->pTopicName[ pxPublishInfo->topicNameLength ] );
            cOriginalChar = *pcLocation;
            *pcLocation = 0x00;
            OSAL_LOGW( TAG, "WARN:  Received an unsolicited publish from topic %s", pxPublishInfo->pTopicName );
            *pcLocation = cOriginalChar;
        } else {
            OSAL_LOGW( TAG, "WARN:  Received an unsolicited publish from topic: unknown" );
        }
    }
}

static void prvOnCompleteCommandCallback( MQTTAgentCommandContext_t *pCmdCallbackContext,
        MQTTAgentReturnInfo_t *pReturnInfo )
{
    if (pCmdCallbackContext == NULL || pReturnInfo == NULL) {
        OSAL_LOGW( TAG, "WARN:  OnCompleteCommandCallback called with NULL context (%p) or return info (%p)", pCmdCallbackContext, pReturnInfo );
        return;
    }

    // Make the event loop data.
    osal_mqtt_event_loop_data_on_complete_t event_loop_data = {
        .message_id = pCmdCallbackContext->msg_id,
        .status = (osal_err_t) pReturnInfo->returnCode,
        .channel = pCmdCallbackContext->event_loop_channel,
    };

    // Post the event loop data.
    osal_event_post(
        xEventLoopRegistrationInfo.event_base,
        pCmdCallbackContext->event_loop_event_id,
        &event_loop_data, sizeof( osal_mqtt_event_loop_data_on_complete_t ), OSAL_MAX_DELAY );

    // Free the command context buffer.
    prvFreeCommandContext( pCmdCallbackContext );
}

static void prvCoreMqttAgentSubackSimulate( osal_mqtt_event_loop_channel_t *channel )
{
    // Make the event loop data.
    osal_mqtt_event_loop_data_on_complete_t event_loop_data = {
        .message_id = 0, // this is not used for simulated SUBACKs
        .status = OSAL_ERR_OK,
        .channel = *channel,
    };

    // Post the event loop data.
    osal_event_post(
        xEventLoopRegistrationInfo.event_base,
        xEventLoopRegistrationInfo.event_ids.subscribed,
        &event_loop_data, sizeof( osal_mqtt_event_loop_data_on_complete_t ), OSAL_MAX_DELAY );
}

static MQTTStatus_t prvCoreMqttAgentInit( NetworkContext_t *pxNetworkContext )
{
    ulGlobalEntryTimeMs = prvGetTimeMs();
    MQTTStatus_t xReturn;

    TransportInterface_t xTransport = { 0 };

    /* Allocate the network buffer. */
    ucNetworkBuffer = OSAL_MALLOC_EXTRAM(configOSAL_MQTT_CORE_NETWORK_BUFFER_SIZE);
    if (ucNetworkBuffer == NULL) {
        OSAL_LOGE( TAG, "Failed to allocate network buffer." );
        return MQTTNoMemory;
    }
    MQTTFixedBuffer_t xFixedBuffer = { .pBuffer = ucNetworkBuffer, .size = configOSAL_MQTT_CORE_NETWORK_BUFFER_SIZE };

    xCommandQueue.queue = osal_queue_create_ext( configOSAL_MQTT_CORE_COMMAND_QUEUE_LENGTH,
                          sizeof( MQTTAgentCommand_t * ) );
    assert( xCommandQueue.queue );
    MQTTAgentMessageInterface_t xMessageInterface = {
        .pMsgCtx        = &xCommandQueue,
        .send           = Agent_MessageSend,
        .recv           = Agent_MessageReceive,
        .getCommand     = Agent_GetCommand,
        .releaseCommand = Agent_ReleaseCommand
    };

    /* Set default command info. */
    xDefaultCommandInfo.cmdCompleteCallback = NULL; /* No callback. */
    xDefaultCommandInfo.pCmdCompleteCallbackContext = NULL; /* No context. */
    xDefaultCommandInfo.blockTimeMs = 0; /* No block time. */

    /* Initialize the task pool. */
    Agent_InitializePool();

    /* Fill in Transport Interface send and receive function pointers. */
    xTransport.pNetworkContext = pxNetworkContext;
    xTransport.send = iTlsTransportSend;
    xTransport.recv = iTlsTransportRecv;

    vTlsSetConnectTimeout( 3000 );
    vTlsSetRecvTimeout( 100 );

    /* Initialize MQTT library. */
    xReturn = MQTTAgent_Init( &xGlobalMqttAgentContext,
                              &xMessageInterface,
                              &xFixedBuffer,
                              &xTransport,
                              prvGetTimeMs,
                              prvIncomingPublishCallback,
                              osal_mqtt_subscription_get_list() );
    if (xReturn != MQTTSuccess) {
        OSAL_LOGE( TAG, "Failed to initialize CoreMQTT-Agent. Error code=%s", MQTT_Status_strerror( xReturn ) );
        osal_queue_delete( xCommandQueue.queue );
        xCommandQueue.queue = NULL;
        return xReturn;
    }
    return xReturn;
}

static MQTTStatus_t prvCoreMqttAgentDeinit( void )
{
    if ( xCommandQueue.queue != NULL ) {
        osal_queue_delete( xCommandQueue.queue );
        xCommandQueue.queue = NULL;
    }
    if (ucNetworkBuffer != NULL) {
        free(ucNetworkBuffer);
        ucNetworkBuffer = NULL;
    }
    return MQTTSuccess;
}

static MQTTStatus_t prvCoreMqttAgentConnect( bool xCleanSession )
{
    MQTTStatus_t xResult;
    MQTTConnectInfo_t xConnectInfo;
    bool xSessionPresent = false;

    /* Ensure previous MQTT connection is not in a connected or disconnect-pending state. */
    if ( xGlobalMqttAgentContext.mqttContext.connectStatus != MQTTNotConnected ) {
        OSAL_LOGD( TAG, "MQTT context not clean (status=%d). Forcing disconnect before connect.", xGlobalMqttAgentContext.mqttContext.connectStatus );
        ( void ) MQTT_Disconnect( &( xGlobalMqttAgentContext.mqttContext ) );
    }

    /* Many fields are not used in this implementation so start with everything at 0. */
    memset( &xConnectInfo, 0x00, sizeof( xConnectInfo ) );

    /* Start with a clean session i.e. direct the MQTT broker to discard any
     * previous session data. Also, establishing a connection with clean session
     * will ensure that the broker does not store any data when this client
     * gets disconnected. */
    xConnectInfo.cleanSession = xCleanSession;

    /* The client identifier is used to uniquely identify this MQTT client to
     * the MQTT broker. In a production device the identifier can be something
     * unique, such as a device serial number. */
    xConnectInfo.pClientIdentifier = pxConnParams->client_id;
    xConnectInfo.clientIdentifierLength = ( uint16_t ) strlen( xConnectInfo.pClientIdentifier );

    /* Add username and password if provided. */
    if (pxConnParams->username != NULL) {
        xConnectInfo.pUserName = pxConnParams->username;
        xConnectInfo.userNameLength = ( uint16_t ) strlen( pxConnParams->username );
    }
    if (pxConnParams->password != NULL) {
        xConnectInfo.pPassword = pxConnParams->password;
        xConnectInfo.passwordLength = ( uint16_t ) strlen( pxConnParams->password );
    }

    /* Set MQTT keep-alive period. It is the responsibility of the application
     * to ensure that the interval between Control Packets being sent does not
     * exceed the Keep Alive value. In the absence of sending any other Control
     * Packets, the Client MUST send a PINGREQ Packet.  This responsibility will
     * be moved inside the agent. */
    xConnectInfo.keepAliveSeconds = configOSAL_MQTT_KEEP_ALIVE_INTERVAL_S;

    /* Build the LWT publish info from conn_params, if enabled. */
    MQTTPublishInfo_t xLwtInfo;
    MQTTPublishInfo_t *pxLwtInfo = NULL;
    if ( pxConnParams != NULL && pxConnParams->lwt.enabled ) {
        memset( &xLwtInfo, 0x00, sizeof( xLwtInfo ) );
        xLwtInfo.qos             = ( MQTTQoS_t ) pxConnParams->lwt.qos;
        xLwtInfo.retain          = pxConnParams->lwt.retain;
        xLwtInfo.pTopicName      = pxConnParams->lwt.topic;
        xLwtInfo.topicNameLength = ( uint16_t ) pxConnParams->lwt.topic_len;
        xLwtInfo.pPayload        = pxConnParams->lwt.payload;
        xLwtInfo.payloadLength   = pxConnParams->lwt.payload_len;
        pxLwtInfo = &xLwtInfo;
    }

    /* Send MQTT CONNECT packet to broker. */
    xResult = MQTT_Connect( &( xGlobalMqttAgentContext.mqttContext ),
                            &xConnectInfo,
                            pxLwtInfo,
                            configOSAL_MQTT_CORE_CONNACK_RECV_TIMEOUT_MS,
                            &xSessionPresent );


    OSAL_LOGI( TAG, "Session present: %d\n", xSessionPresent );

    if ( xResult == MQTTSuccess ) {
        /* Resubscribe/Resume logic */
        if ( xCleanSession == true ) {
#if CONFIG_OSAL_MQTT_RESUBSCRIBE_ON_CLEAN_SESSION
            OSAL_LOGI( TAG, "Clean session in use. Resubscribing to all subscriptions" );
            osal_mqtt_subscription_attempt_resubscribe_all( subscribe );
#else
            OSAL_LOGI( TAG, "Clean session in use. Clearing subscription manager" );
            osal_mqtt_subscription_clear();
#endif /* CONFIG_OSAL_MQTT_RESUBSCRIBE_ON_CLEAN_SESSION */
        } else {
            xResult = MQTTAgent_ResumeSession( &xGlobalMqttAgentContext, xSessionPresent );
            if ( xResult == MQTTSuccess ) {
                if ( xSessionPresent ) {
                    OSAL_LOGI( TAG, "Session present. Simulating SUBACKs for all subscriptions" );
                    osal_mqtt_subscription_simulate_subacks( prvCoreMqttAgentSubackSimulate );
                } else {
#if CONFIG_OSAL_MQTT_RESUBSCRIBE_ON_CLEAN_SESSION
                    OSAL_LOGI( TAG, "No prior session. Resubscribing to all subscriptions" );
                    osal_mqtt_subscription_attempt_resubscribe_all( subscribe );
#else
                    OSAL_LOGI( TAG, "No prior session. Clearing subscription manager" );
                    osal_mqtt_subscription_clear();
#endif /* CONFIG_OSAL_MQTT_RESUBSCRIBE_ON_CLEAN_SESSION */
                }
            }
        }

        /* Reset the clean session flag to the configured default for future
         * reconnect attempts. force_reconnect() may temporarily override this. */
        xCleanSession = ( pxConnParams != NULL ) ? pxConnParams->clean_session : false;

        /* Local connected flag */
        xIsConnected = true;

        /* Notify the client connected event. */
        osal_mqtt_event_notify_client_connected();

        /* Then post the connected event to the event loop. */
        osal_event_post(
            xEventLoopRegistrationInfo.event_base,
            xEventLoopRegistrationInfo.event_ids.connected,
            NULL, 0, OSAL_MAX_DELAY );
    }

    return xResult;
}

static bool prvBackoffForRetry( BackoffAlgorithmContext_t *pxRetryParams )
{
    bool xReturnStatus = false;
    uint16_t usNextRetryBackOff = 0U;
    BackoffAlgorithmStatus_t xBackoffAlgStatus = BackoffAlgorithmSuccess;

    uint32_t ulRandomNum = rand();

    /* Get back-off value (in milliseconds) for the next retry attempt. */
    xBackoffAlgStatus = BackoffAlgorithm_GetNextBackoff( pxRetryParams,
                        ulRandomNum,
                        &usNextRetryBackOff );

    if ( xBackoffAlgStatus == BackoffAlgorithmRetriesExhausted ) {
        OSAL_LOGI( TAG, "All retry attempts have exhausted. Operation will not be retried." );
        xReturnStatus = false;
    } else if ( xBackoffAlgStatus == BackoffAlgorithmSuccess ) {
        /* Perform the backoff delay. */
        osal_task_delay( osal_ticks_from_ms( usNextRetryBackOff ) );

        xReturnStatus = true;

        OSAL_LOGI( TAG, "Retry attempt %u.", (unsigned int) pxRetryParams->attemptsDone );
    }

    return xReturnStatus;
}

static void prvCoreMqttAgentTask( void *pvParameters )
{
    MQTTStatus_t xResult;
    TlsTransportStatus_t xTlsRet = TLS_TRANSPORT_CONNECT_FAILURE;
    xCleanSession = ( pxConnParams != NULL ) ? pxConnParams->clean_session : false;
    agent_task_is_alive = true;
    osal_event_group_clear_bits(sync_event_group, SYNC_EVENT_GROUP_BITS_AGENT_TASK_EXIT);

    while ( should_keep_agent_task_alive ) {
        /* If client is connected, handle process loop here. */
        if ( xIsConnected ) {
            OSAL_LOGI( TAG, "Entering command loop" );
            xResult = MQTTAgent_CommandLoop( &xGlobalMqttAgentContext );

            if ( xResult != MQTTSuccess ) {
                OSAL_LOGE( TAG, "MQTTAgent_CommandLoop failed. Error code=%s", MQTT_Status_strerror( xResult ) );
            }

            bool shouldDisconnect = ( xResult == MQTTRecvFailed ) ||
                                    ( xResult == MQTTSendFailed ) ||
                                    ( xResult == MQTTKeepAliveTimeout ) ||
                                    ( xGlobalMqttAgentContext.mqttContext.connectStatus != MQTTConnected ) ||
                                    ( !should_keep_agent_task_alive );

            if ( shouldDisconnect ) {
                /* Disconnect existing TLS connection, if any. */
                if ( ( xTlsContextIsConnected( &xNetworkContext ) ) ) {
                    xTlsDisconnect( &xNetworkContext );
                    OSAL_LOGI( TAG, "TLS connection was disconnected." );
                }

                xIsConnected = false;

                /* Notify client disconnected if loop exits. */
                osal_mqtt_event_notify_client_disconnected();
                osal_event_post(
                    xEventLoopRegistrationInfo.event_base,
                    xEventLoopRegistrationInfo.event_ids.disconnected,
                    NULL, 0, OSAL_MAX_DELAY );

                /* Cancel all commands. */
                MQTTAgent_CancelAll( &xGlobalMqttAgentContext );
            }
        }

        /* Re-connect to MQTT broker if network is disconnected, and task should be kept alive. */
        else if ( should_keep_agent_task_alive ) {
            OSAL_LOGI( TAG, "Entering reconnect loop" );
            BackoffAlgorithmContext_t xReconnectParams;
            /* Disconnect existing TLS connection, if any. */
            if ( ( xTlsContextIsConnected( &xNetworkContext ) ) ) {
                xTlsDisconnect( &xNetworkContext );
                OSAL_LOGI( TAG, "TLS connection was disconnected." );
            }

            /* Retry MQTT connection until success. */
            OSAL_LOGI( TAG, "Attempting to establish MQTT connection..." );
            xResult = MQTTBadParameter;
            bool xBackoffRet = false;
            BackoffAlgorithm_InitializeParams( &xReconnectParams,
                                               configOSAL_MQTT_CORE_RETRY_BACKOFF_BASE_MS,
                                               configOSAL_MQTT_CORE_RETRY_MAX_BACKOFF_DELAY_MS,
                                               BACKOFF_ALGORITHM_RETRY_FOREVER );
            do {
                xTlsRet = xTlsConnect( &xNetworkContext );
                if ( xTlsRet == TLS_TRANSPORT_SUCCESS ) {
                    OSAL_LOGI( TAG, "TLS connection established." );
                    if ( iTlsGetSocketFd( &xNetworkContext ) != -1 ) {
                        /* Socket is valid, try to connect to MQTT broker. */
                        xResult = prvCoreMqttAgentConnect( xCleanSession );
                    } else {
                        /* Socket is not valid, retry TLS connection. */
                        OSAL_LOGE( TAG, "Failed to get socket file descriptor. Retrying TLS connection..." );
                        xResult = MQTTBadParameter;
                    }
                }

                if (xResult != MQTTSuccess) {
                    xTlsDisconnect( &xNetworkContext );
                    xBackoffRet = prvBackoffForRetry( &xReconnectParams );
                }

                if (!should_keep_agent_task_alive) {
                    break;
                }
            } while (xResult != MQTTSuccess && xBackoffRet);
        }

        if (!should_keep_agent_task_alive) {
            break;
        }
    }

    /* Clean up TLS connection to prevent socket leak before task deletion */
    if (xTlsContextIsConnected( &xNetworkContext )) {
        OSAL_LOGD(TAG, "Closing TLS connection during task exit to prevent socket leak");
        xTlsDisconnect(&xNetworkContext);
        vTlsContextClear( &xNetworkContext );
    }

    /* Clean up any remaining commands in the command queue. */
    MQTTAgent_CancelAll( &xGlobalMqttAgentContext );

    OSAL_LOGI( TAG, "Exiting coreMQTT-Agent task" );
    // clean up
    agent_task_is_alive = false;
    osal_event_group_set_bits(sync_event_group, SYNC_EVENT_GROUP_BITS_AGENT_TASK_EXIT);
    osal_task_delete(NULL);
}

static bool prvStartCoreMqttAgent( void )
{
    bool xRet = true;
    should_keep_agent_task_alive = true;
    if ( osal_task_create( prvCoreMqttAgentTask,
                           "coreMQTT-Agent",
                           configOSAL_MQTT_AGENT_TASK_STACK_SIZE,
                           NULL,
                           configOSAL_MQTT_AGENT_TASK_PRIORITY,
                           &agent_task_handle ) != OSAL_ERR_OK ) {
        OSAL_LOGE( TAG, "Failed to create coreMQTT-Agent task." );
        xRet = false;
    }

    return xRet;
}

/* MQTT Common implementations **********************************************/

static osal_err_t init( osal_mqtt_conn_params_t *conn_params, osal_mqtt_event_loop_registration_info_t *event_loop_registration_info )
{
    if (!conn_params || !event_loop_registration_info) {
        OSAL_LOGE( TAG, "Invalid parameters: conn_params: %p, event_loop_registration_info: %p", conn_params, event_loop_registration_info );
        return OSAL_ERR_INVALID_ARG;
    }

    osal_err_t ret = OSAL_ERR_OK;

    // common pre-init.
    ret = osal_mqtt_pre_init();
    if (ret != OSAL_ERR_OK) {
        return ret;
    }

    // initialize sync event group.
    sync_event_group = osal_event_group_create();
    if (sync_event_group == NULL) {
        OSAL_LOGE( TAG, "Failed to create sync event group." );
        return OSAL_ERR_NO_MEM;
    }

    // initialize event loop registration information.
    xEventLoopRegistrationInfo = *event_loop_registration_info;

    // keep conn_params pointer for later use (LWT, clean_session).
    pxConnParams = conn_params;

    // initialize TLS parameters, e.g., semaphore.
    if ( !xTlsContextSemaphoreInit( &xNetworkContext ) ) {
        OSAL_LOGE( TAG, "Not enough memory to create TLS semaphore for global network context." );
        goto init_fail;
    }

    ret = prvCopyToNetworkContext(conn_params);
    if (ret != OSAL_ERR_OK) {
        OSAL_LOGE( TAG, "Failed to copy connection parameters to network context." );
        goto init_fail;
    }


    // Core-MQTT specific.
    ret = mqtt_agent_status_to_os_err( prvCoreMqttAgentInit(&xNetworkContext) );
    if (ret != OSAL_ERR_OK) {
        OSAL_LOGE( TAG, "Failed to initialize CoreMQTT-Agent." );
        goto init_fail;
    }

    return ret;

init_fail:
    pxConnParams = NULL;
    vTlsContextSemaphoreDeinit( &xNetworkContext );
    if (sync_event_group != NULL) {
        osal_event_group_delete(sync_event_group);
        sync_event_group = NULL;
    }
    return ret;
}

static osal_err_t deinit( void )
{
    // drop conn_params pointer (caller owns the memory).
    pxConnParams = NULL;

    // delete event loop registration information.
    xEventLoopRegistrationInfo.event_base = NULL;
    xEventLoopRegistrationInfo.event_ids.connected = 0;
    xEventLoopRegistrationInfo.event_ids.disconnected = 0;
    xEventLoopRegistrationInfo.event_ids.published = 0;
    xEventLoopRegistrationInfo.event_ids.subscribed = 0;
    xEventLoopRegistrationInfo.event_ids.unsubscribed = 0;

    /* Delete agent task if it is alive. */
    if (agent_task_is_alive) {
        /* Kill task and wait for graceful exit. */
        should_keep_agent_task_alive = false;
        OSAL_LOGI( TAG, "Waiting for agent task to exit." );
        osal_event_group_wait_bits(sync_event_group, SYNC_EVENT_GROUP_BITS_AGENT_TASK_EXIT, true, true, OSAL_MAX_DELAY);

        /* Task will delete itself. */
        agent_task_handle = NULL;
        agent_task_is_alive = false;
    }

    /* Delete CoreMQTT-Agent related resources. */
    if ( prvCoreMqttAgentDeinit() != MQTTSuccess ) {
        OSAL_LOGE( TAG, "Failed to deinitialize CoreMQTT-Agent." );
        return OSAL_ERR_INVALID_STATE;
    }

    /* Delete used semaphores. */
    vTlsContextSemaphoreDeinit( &xNetworkContext );

    // delete sync event group.
    if (sync_event_group != NULL) {
        osal_event_group_delete(sync_event_group);
        sync_event_group = NULL;
    }

    // common post-deinit.
    osal_mqtt_post_deinit();

    OSAL_LOGI( TAG, "CoreMQTT-Agent deinit completed." );
    return OSAL_ERR_OK;
}

static osal_err_t connect( void )
{
    bool xRet = false;

    /* Start agent task if needed. */
    if (!agent_task_is_alive) {
        xRet = prvStartCoreMqttAgent();
        if (!xRet) {
            should_keep_agent_task_alive = false;
            return OSAL_ERR_INVALID_STATE;
        }
    }

    return OSAL_ERR_OK;
}

static osal_err_t disconnect( void )
{
    /* Signal agent task to exit, and disconnect from MQTT broker. */
    should_keep_agent_task_alive = false;
    return mqtt_agent_status_to_os_err( MQTTAgent_Disconnect( &xGlobalMqttAgentContext, &xDefaultCommandInfo ) );
}

static osal_err_t force_reconnect( void )
{
    /* Force a disconnect, but keep the agent task alive. */
    xCleanSession = true;
    return mqtt_agent_status_to_os_err( MQTTAgent_Disconnect( &xGlobalMqttAgentContext, &xDefaultCommandInfo ) );
}

static osal_err_t drop( void )
{
    /* Stop the agent task from attempting any reconnects. */
    should_keep_agent_task_alive = false;
    return mqtt_agent_status_to_os_err( MQTTAgent_Terminate( &xGlobalMqttAgentContext, &xDefaultCommandInfo ) );
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
        OSAL_LOGE( TAG, "No topic provided for publish" );
        return OSAL_ERR_INVALID_ARG;
    }

    if ( !xIsConnected ) {
        OSAL_LOGE( TAG, "MQTT client is disconnected" );
        return OSAL_ERR_MQTT_NOT_CONNECTED;
    }

    // Get a unique message ID (and save to pointer if required)
    int msg_id_scope = ++ulMessageId;

    // setup publish information
    MQTTPublishInfo_t *p_publish_info = (MQTTPublishInfo_t *) OSAL_CALLOC_EXTRAM( 1, sizeof( MQTTPublishInfo_t ) );
    if ( p_publish_info == NULL ) {
        return OSAL_ERR_NO_MEM;
    }

    void *p_payload = NULL;
    if (data != NULL && data_len > 0) {
        p_payload = OSAL_MALLOC_EXTRAM( data_len );
        if (p_payload == NULL) {
            free( p_publish_info );
            return OSAL_ERR_NO_MEM;
        }
        memcpy( p_payload, data, data_len );
    }

    p_publish_info->qos = (MQTTQoS_t) qos;
    p_publish_info->retain = retain;
    p_publish_info->pTopicName = strndup( topic, topic_len );
    p_publish_info->topicNameLength = topic_len;
    p_publish_info->pPayload = p_payload;
    p_publish_info->payloadLength = data_len;
    if (p_publish_info->pTopicName == NULL) {
        prvFreeCommandPayload( OSAL_MQTT_ACTION_PUBLISH, p_publish_info );
        return OSAL_ERR_NO_MEM;
    }

    // setup callback and its context
    MQTTAgentCommandContext_t *pxCallbackContext = prvGetCommandContext( msg_id_scope, OSAL_MQTT_ACTION_PUBLISH, p_publish_info, channel );
    if (pxCallbackContext == NULL) {
        prvFreeCommandPayload( OSAL_MQTT_ACTION_PUBLISH, p_publish_info );
        return OSAL_ERR_NO_MEM;
    }

    MQTTAgentCommandInfo_t *p_command_info = (MQTTAgentCommandInfo_t *) OSAL_CALLOC_EXTRAM( 1, sizeof( MQTTAgentCommandInfo_t ) );
    if ( p_command_info == NULL ) {
        prvFreeCommandContext( pxCallbackContext );
        return OSAL_ERR_NO_MEM;
    }
    p_command_info->cmdCompleteCallback = prvOnCompleteCommandCallback;
    p_command_info->pCmdCompleteCallbackContext = pxCallbackContext;
    pxCallbackContext->p_command_info = p_command_info;
    pxCallbackContext->event_loop_event_id = xEventLoopRegistrationInfo.event_ids.published;

    // publish the message.
    MQTTStatus_t command_added = MQTTAgent_Publish( &xGlobalMqttAgentContext, p_publish_info, p_command_info );

    // could not add to command queue.
    if (command_added != MQTTSuccess) {
        OSAL_LOGE( TAG, "Failed to enqueue publish command. Error code=%s", MQTT_Status_strerror( command_added ) );

        prvFreeCommandContext( pxCallbackContext );
        return mqtt_agent_status_to_os_err( command_added );
    }

    return OSAL_ERR_OK;
}

static osal_err_t subscribe( osal_mqtt_event_loop_channel_t *channel,
                             const char *topic,
                             size_t topic_len,
                             osal_mqtt_subscribe_cb_t cb,
                             osal_mqtt_QoS_t qos,
                             void *priv_data )
{
    if ( topic == NULL || topic_len == 0 ) {
        OSAL_LOGE( TAG, "No topic provided for subscribe" );
        return OSAL_ERR_INVALID_ARG;
    }

    if ( !xIsConnected ) {
        OSAL_LOGE( TAG, "MQTT client is disconnected" );
        return OSAL_ERR_MQTT_NOT_CONNECTED;
    }

    // Get a unique message ID
    int msg_id = ++ulMessageId;

    // set up subscribe arguments
    MQTTSubscribeInfo_t *p_subscribe_info = (MQTTSubscribeInfo_t *) OSAL_CALLOC_EXTRAM( 1, sizeof( MQTTSubscribeInfo_t ) );
    if ( p_subscribe_info == NULL ) {
        return OSAL_ERR_NO_MEM;
    }
    p_subscribe_info->pTopicFilter = strndup( topic, topic_len );
    if (p_subscribe_info->pTopicFilter == NULL) {
        free( p_subscribe_info );
        return OSAL_ERR_NO_MEM;
    }
    p_subscribe_info->topicFilterLength = topic_len;
    p_subscribe_info->qos = (MQTTQoS_t) qos;

    MQTTAgentSubscribeArgs_t *p_subscribe_args = (MQTTAgentSubscribeArgs_t *) OSAL_CALLOC_EXTRAM( 1, sizeof( MQTTAgentSubscribeArgs_t ) );
    if ( p_subscribe_args == NULL ) {
        free( (char *) p_subscribe_info->pTopicFilter );
        free( p_subscribe_info );
        return OSAL_ERR_NO_MEM;
    }
    p_subscribe_args->pSubscribeInfo = p_subscribe_info;
    p_subscribe_args->numSubscriptions = 1;

    // setup callback and its context
    MQTTAgentCommandContext_t *pxCallbackContext = prvGetCommandContext( msg_id, OSAL_MQTT_ACTION_SUBSCRIBE, p_subscribe_args, channel );
    if (pxCallbackContext == NULL) {
        prvFreeCommandPayload( OSAL_MQTT_ACTION_SUBSCRIBE, p_subscribe_args );
        return OSAL_ERR_NO_MEM;
    }

    MQTTAgentCommandInfo_t *p_command_info = (MQTTAgentCommandInfo_t *) OSAL_CALLOC_EXTRAM( 1, sizeof( MQTTAgentCommandInfo_t ) );
    if ( p_command_info == NULL ) {
        prvFreeCommandContext( pxCallbackContext );
        return OSAL_ERR_NO_MEM;
    }
    p_command_info->cmdCompleteCallback = prvOnCompleteCommandCallback;
    p_command_info->pCmdCompleteCallbackContext = pxCallbackContext;
    pxCallbackContext->p_command_info = p_command_info;
    pxCallbackContext->event_loop_event_id = xEventLoopRegistrationInfo.event_ids.subscribed;

    // add to subscription list if successful.
    if (!osal_mqtt_subscription_add( topic, topic_len, channel, cb, qos, priv_data )) {
        OSAL_LOGE( TAG, "Failed to add subscription to list. topic: %s", topic );
        prvFreeCommandContext( pxCallbackContext );
        return OSAL_ERR_INVALID_STATE;
    }

    // send subscribe command.
    MQTTStatus_t command_added = MQTTAgent_Subscribe( &xGlobalMqttAgentContext,
                                 p_subscribe_args,
                                 p_command_info );

    // could not add to command queue.
    if (command_added != MQTTSuccess) {
        OSAL_LOGE( TAG, "Failed to enqueue subscribe command. Error code=%s", MQTT_Status_strerror( command_added ) );

        osal_mqtt_subscription_remove( topic, topic_len );
        prvFreeCommandContext( pxCallbackContext );
        return mqtt_agent_status_to_os_err( command_added );
    }

    return OSAL_ERR_OK;
}

static osal_err_t unsubscribe( osal_mqtt_event_loop_channel_t *channel,
                               const char *topic,
                               size_t topic_len,
                               osal_mqtt_QoS_t qos )
{
    if ( topic == NULL || topic_len == 0 ) {
        OSAL_LOGE( TAG, "No topic provided for unsubscribe" );
        return OSAL_ERR_INVALID_ARG;
    }

    if ( !xIsConnected ) {
        OSAL_LOGE( TAG, "MQTT client is disconnected" );
        return OSAL_ERR_MQTT_NOT_CONNECTED;
    }

    // Get a unique message ID
    int msg_id = ++ulMessageId;

    // set up unsubscribe information
    MQTTSubscribeInfo_t *p_unsubscribe_info = (MQTTSubscribeInfo_t *) OSAL_CALLOC_EXTRAM( 1, sizeof( MQTTSubscribeInfo_t ) );
    if ( p_unsubscribe_info == NULL ) {
        return OSAL_ERR_NO_MEM;
    }
    p_unsubscribe_info->pTopicFilter = strndup( topic, topic_len );
    if (p_unsubscribe_info->pTopicFilter == NULL) {
        free( p_unsubscribe_info );
        return OSAL_ERR_NO_MEM;
    }
    p_unsubscribe_info->topicFilterLength = topic_len;
    p_unsubscribe_info->qos = (MQTTQoS_t) qos;

    MQTTAgentSubscribeArgs_t *p_unsubscribe_args = (MQTTAgentSubscribeArgs_t *) OSAL_CALLOC_EXTRAM( 1, sizeof( MQTTAgentSubscribeArgs_t ) );
    if ( p_unsubscribe_args == NULL ) {
        free( (char *) p_unsubscribe_info->pTopicFilter );
        free( p_unsubscribe_info );
        return OSAL_ERR_NO_MEM;
    }
    p_unsubscribe_args->pSubscribeInfo = p_unsubscribe_info;
    p_unsubscribe_args->numSubscriptions = 1;

    // setup callback and its context
    MQTTAgentCommandContext_t *pxCallbackContext = prvGetCommandContext( msg_id, OSAL_MQTT_ACTION_UNSUBSCRIBE, p_unsubscribe_args, channel );
    if (pxCallbackContext == NULL) {
        prvFreeCommandPayload( OSAL_MQTT_ACTION_UNSUBSCRIBE, p_unsubscribe_args );
        return OSAL_ERR_NO_MEM;
    }

    MQTTAgentCommandInfo_t *p_command_info = (MQTTAgentCommandInfo_t *) OSAL_CALLOC_EXTRAM( 1, sizeof( MQTTAgentCommandInfo_t ) );
    if ( p_command_info == NULL ) {
        prvFreeCommandContext( pxCallbackContext );
        return OSAL_ERR_NO_MEM;
    }
    p_command_info->cmdCompleteCallback = prvOnCompleteCommandCallback;
    p_command_info->pCmdCompleteCallbackContext = pxCallbackContext;
    pxCallbackContext->p_command_info = p_command_info;
    pxCallbackContext->event_loop_event_id = xEventLoopRegistrationInfo.event_ids.unsubscribed;

    // send unsubscribe command.
    MQTTStatus_t command_added = MQTTAgent_Unsubscribe( &xGlobalMqttAgentContext,
                                 p_unsubscribe_args,
                                 p_command_info );

    // could not add to command queue.
    if (command_added != MQTTSuccess) {
        OSAL_LOGE( TAG, "Failed to enqueue unsubscribe command. Error code=%s", MQTT_Status_strerror( command_added ) );

        prvFreeCommandContext( pxCallbackContext );
        return mqtt_agent_status_to_os_err( command_added );
    }

    // remove from subscription list if successful.
    osal_mqtt_subscription_remove( topic, topic_len );
    return OSAL_ERR_OK;
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
