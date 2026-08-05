/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mqtt_subscription_manager.c
 * @brief Functions for managing MQTT subscriptions.
 */

/* Standard includes. */
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

/* Subscription manager header include. */
#include "osal_mqtt_subscription_manager.h"

/* Logging include and tag constant. */
#include "osal_log.h"
static const char *TAG = "osal_mqtt_submgr";

/* Platform common includes. */
#include "osal_semaphore.h"
#include "osal_ticks.h"
#include "osal_log.h"

/* Utility includes. */
#include "osal_mqtt_util.h"

/* Static variables *******************************************/

/**
 * @brief The global array of subscription elements.
 *
 * @note The subscription manager implementation expects that the array of the
 * subscription elements used for storing subscriptions to be initialized to 0.
 */
static osal_mqtt_subscription_element_t pxSubscriptionList[ OSAL_MQTT_MAX_SUBSCRIPTIONS ];

static uint8_t subscription_count = 0;
/**
 * @brief The mutex for the subscription list.
 */
static osal_semaphore_handle_t xSubscriptionListMutex;

/* Static function declarations ******************************/
/**
 * @brief Lock the subscription list.
 */
static bool lock_subscription_list( void );

/**
 * @brief Unlock the subscription list.
 */
static bool unlock_subscription_list( void );

/** Static function definitions *******************************/


static bool lock_subscription_list( void )
{
    return osal_semaphore_take( xSubscriptionListMutex, OSAL_MAX_DELAY ) == OSAL_ERR_OK;
}

static bool unlock_subscription_list( void )
{
    return osal_semaphore_give( xSubscriptionListMutex ) == OSAL_ERR_OK;
}
/** Public function definitions ********************************/

void osal_mqtt_subscription_init( void )
{
    // set array to all 0's.
    memset(
        pxSubscriptionList,
        0x00,
        sizeof(osal_mqtt_subscription_element_t) * OSAL_MQTT_MAX_SUBSCRIPTIONS
    );

    /* Initialize the subscription list mutex. */
    xSubscriptionListMutex = osal_semaphore_create_mutex();
    if ( xSubscriptionListMutex == NULL ) {
        OSAL_LOGE( TAG, "Failed to create subscription list mutex." );
    }
}

void osal_mqtt_subscription_deinit( void )
{
    /* Delete the subscription list mutex. */
    if (xSubscriptionListMutex != NULL) {
        osal_semaphore_delete(xSubscriptionListMutex);
        xSubscriptionListMutex = NULL;
    }

    /* Clear the subscription list. */
    memset(
        pxSubscriptionList,
        0x00,
        sizeof(osal_mqtt_subscription_element_t) * OSAL_MQTT_MAX_SUBSCRIPTIONS
    );
}

osal_mqtt_subscription_element_t *osal_mqtt_subscription_get_list( void )
{
    return pxSubscriptionList;
}

bool osal_mqtt_subscription_add( const char *pcTopicFilterString,
                                 uint16_t usTopicFilterLength,
                                 osal_mqtt_event_loop_channel_t *channel,
                                 osal_mqtt_subscribe_cb_t callback,
                                 osal_mqtt_QoS_t qos,
                                 void *priv_data )
{
    int32_t lIndex = 0;
    size_t xAvailableIndex = OSAL_MQTT_MAX_SUBSCRIPTIONS;
    bool xReturnStatus = false;

    if ( ( pcTopicFilterString == NULL ) ||
            ( usTopicFilterLength == 0U ) ||
            ( callback == NULL ) ) {
        OSAL_LOGE(TAG, "Invalid parameter. pcTopicFilterString=%p,"
                  " usTopicFilterLength=%u, pxIncomingPublishCallback=%p.",
                  pcTopicFilterString,
                  ( unsigned int ) usTopicFilterLength,
                  callback );
    } else {
        /* Acquire the subscription list mutex. */
        if ( !lock_subscription_list() ) {
            return false;
        }

        /* Start at end of array, so that we will insert at the first available index.
         * Scans backwards to find duplicates. */
        for ( lIndex = ( int32_t ) OSAL_MQTT_MAX_SUBSCRIPTIONS - 1; lIndex >= 0; lIndex-- ) {
            if ( pxSubscriptionList[ lIndex ].usFilterStringLength == 0 ) {
                xAvailableIndex = lIndex;
            } else if ( ( pxSubscriptionList[ lIndex ].usFilterStringLength == usTopicFilterLength ) &&
                        ( strncmp( pcTopicFilterString, pxSubscriptionList[ lIndex ].pcSubscriptionFilterString, ( size_t ) usTopicFilterLength ) == 0 ) ) {
                /* If a subscription already exists, don't do anything. */
                if ( pxSubscriptionList[ lIndex ].callback == callback && pxSubscriptionList[ lIndex ].priv_data == priv_data ) {
                    xAvailableIndex = OSAL_MQTT_MAX_SUBSCRIPTIONS;
                    xReturnStatus = true;
                    break;
                }
            }
        }

        if ( xAvailableIndex < OSAL_MQTT_MAX_SUBSCRIPTIONS ) {
            pxSubscriptionList[ xAvailableIndex ].pcSubscriptionFilterString = strndup( pcTopicFilterString, usTopicFilterLength );
            pxSubscriptionList[ xAvailableIndex ].usFilterStringLength = usTopicFilterLength;
            pxSubscriptionList[ xAvailableIndex ].channel = (channel != NULL)
                    ? *channel
            : (osal_mqtt_event_loop_channel_t) {
                .main = UINT32_MAX, .sub = UINT32_MAX, .seq = UINT32_MAX
            };
            pxSubscriptionList[ xAvailableIndex ].callback = callback;
            pxSubscriptionList[ xAvailableIndex ].qos = qos;
            pxSubscriptionList[ xAvailableIndex ].priv_data = priv_data;
            xReturnStatus = true;
            subscription_count++;
            OSAL_LOGD(TAG, "Added to subscription list for topic: %s, count: %d/%d", pcTopicFilterString, subscription_count, OSAL_MQTT_MAX_SUBSCRIPTIONS);
        }

        /* Release the subscription list mutex. */
        unlock_subscription_list();
    }

    return xReturnStatus;
}

/*-----------------------------------------------------------*/

void osal_mqtt_subscription_remove( const char *pcTopicFilterString,
                                    uint16_t usTopicFilterLength )
{
    uint32_t ulIndex = 0;

    if ( ( pcTopicFilterString == NULL ) ||
            ( usTopicFilterLength == 0U ) ) {
        OSAL_LOGE(TAG,
                  "Invalid parameter. pcTopicFilterString=%p,"
                  " usTopicFilterLength=%u.",
                  pcTopicFilterString,
                  ( unsigned int ) usTopicFilterLength  );
    } else {
        /* Acquire the subscription list mutex. */
        if ( !lock_subscription_list() ) {
            return;
        }

        for ( ulIndex = 0U; ulIndex < OSAL_MQTT_MAX_SUBSCRIPTIONS; ulIndex++ ) {
            if ( pxSubscriptionList[ ulIndex ].usFilterStringLength == usTopicFilterLength ) {
                if ( strncmp( pxSubscriptionList[ ulIndex ].pcSubscriptionFilterString, pcTopicFilterString, usTopicFilterLength ) == 0 ) {
                    free( pxSubscriptionList[ ulIndex ].pcSubscriptionFilterString );
                    memset( &( pxSubscriptionList[ ulIndex ] ), 0x00, sizeof( osal_mqtt_subscription_element_t ) );
                    subscription_count--;
                    OSAL_LOGD(TAG, "Removed from subscription list for topic: %s, count: %d/%d", pcTopicFilterString, subscription_count, OSAL_MQTT_MAX_SUBSCRIPTIONS);
                }
            }
        }

        /* Release the subscription list mutex. */
        unlock_subscription_list();
    }
}

void osal_mqtt_subscription_clear( void )
{
    if ( !lock_subscription_list() ) {
        return;
    }

    /* Clear the subscription list. */
    for ( uint32_t ulIndex = 0U; ulIndex < OSAL_MQTT_MAX_SUBSCRIPTIONS; ulIndex++ ) {
        if ( pxSubscriptionList[ ulIndex ].pcSubscriptionFilterString != NULL && pxSubscriptionList[ ulIndex ].usFilterStringLength > 0 ) {
            free( pxSubscriptionList[ ulIndex ].pcSubscriptionFilterString );
        }
    }
    memset( pxSubscriptionList, 0x00, sizeof( osal_mqtt_subscription_element_t ) * OSAL_MQTT_MAX_SUBSCRIPTIONS );
    subscription_count = 0;

    /* Release the subscription list mutex. */
    unlock_subscription_list();
}

/*-----------------------------------------------------------*/

bool osal_mqtt_subscription_handle_publish( const char *topic, size_t topic_len, void *payload, size_t payload_len )
{
    uint32_t ulIndex = 0;

    if ( topic == NULL ) {
        OSAL_LOGE(TAG,
                  "Invalid parameter. topic=%p",
                  topic );

        return false;
    }

    /* Callback and private data copies. */
    uint32_t callback_count = 0;
    osal_mqtt_subscribe_cb_t callbacks[ OSAL_MQTT_MAX_SUBSCRIPTIONS ];
    void *priv_data[ OSAL_MQTT_MAX_SUBSCRIPTIONS ];

    /* Acquire the subscription list mutex. */
    if ( !lock_subscription_list() ) {
        return false;
    }

    /* Look through subscriptions. */
    for ( ulIndex = 0U; ulIndex < OSAL_MQTT_MAX_SUBSCRIPTIONS; ulIndex++ ) {
        if ( pxSubscriptionList[ ulIndex ].usFilterStringLength > 0 ) {
            bool isMatched = osal_mqtt_match_topic( topic,
                                                    topic_len,
                                                    pxSubscriptionList[ ulIndex ].pcSubscriptionFilterString,
                                                    pxSubscriptionList[ ulIndex ].usFilterStringLength );

            if ( isMatched ) {
                callbacks[ callback_count ] = pxSubscriptionList[ ulIndex ].callback;
                priv_data[ callback_count ] = pxSubscriptionList[ ulIndex ].priv_data;
                callback_count++;
            }
        }
    }

    /* Release the subscription list mutex. */
    unlock_subscription_list();

    /* Call the callbacks. */
    for ( ulIndex = 0U; ulIndex < callback_count; ulIndex++ ) {
        callbacks[ ulIndex ]( topic,
                              topic_len,
                              payload,
                              payload_len,
                              priv_data[ ulIndex ] );
    }

    return callback_count > 0;
}

void osal_mqtt_subscription_attempt_resubscribe_all(osal_mqtt_subscribe_t subscribe_fn)
{
    /* Acquire the subscription list mutex. */
    if ( !lock_subscription_list() ) {
        return;
    }

    uint32_t ulIndex = 0;
    osal_mqtt_subscription_element_t resubscribe_list[ OSAL_MQTT_MAX_SUBSCRIPTIONS ];
    size_t resubscribe_count = 0;

    /* Resubscribe to all subscriptions. */
    for ( ulIndex = 0U; ulIndex < OSAL_MQTT_MAX_SUBSCRIPTIONS; ulIndex++ ) {
        if ( pxSubscriptionList[ ulIndex ].usFilterStringLength > 0 ) {
            resubscribe_list[ resubscribe_count ] = pxSubscriptionList[ ulIndex ];
            resubscribe_count++;
        }
    }

    /* Release the subscription list mutex. */
    unlock_subscription_list();

    /* Resubscribe to all subscriptions. */
    for ( ulIndex = 0U; ulIndex < resubscribe_count; ulIndex++ ) {
        subscribe_fn( &resubscribe_list[ ulIndex ].channel,
                      resubscribe_list[ ulIndex ].pcSubscriptionFilterString,
                      resubscribe_list[ ulIndex ].usFilterStringLength,
                      resubscribe_list[ ulIndex ].callback,
                      resubscribe_list[ ulIndex ].qos,
                      resubscribe_list[ ulIndex ].priv_data );
    }
}

void osal_mqtt_subscription_simulate_subacks( osal_mqtt_suback_simulate_t suback_simulate_fn )
{
    /* Acquire the subscription list mutex. */
    if ( !lock_subscription_list() ) {
        return;
    }

    /* Simulate the subacks for all subscriptions except for QoS0. */
    uint32_t ulIndex = 0;
    for ( ulIndex = 0U; ulIndex < OSAL_MQTT_MAX_SUBSCRIPTIONS; ulIndex++ ) {
        if ( pxSubscriptionList[ ulIndex ].usFilterStringLength > 0 &&
                pxSubscriptionList[ ulIndex ].qos != QoS0
           ) {
            suback_simulate_fn( &pxSubscriptionList[ ulIndex ].channel );
        }
    }

    /* Release the subscription list mutex. */
    unlock_subscription_list();
}
