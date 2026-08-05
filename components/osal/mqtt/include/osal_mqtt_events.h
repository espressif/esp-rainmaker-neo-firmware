/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mqtt_events.h
 * @brief Functions for managing MQTT events.
 */

#ifndef OSAL_MQTT_EVENTS_H
#define OSAL_MQTT_EVENTS_H

#include "osal_mqtt_prototypes.h"
#include "osal_event_group.h"

/* MQTT event group bit definitions */
#define OSAL_MQTT_NETWORK_CONNECTED_BIT          ( 1 << 0 )
#define OSAL_MQTT_NETWORK_DISCONNECTED_BIT       ( 1 << 1 )
#define OSAL_MQTT_CLIENT_CONNECTED_BIT           ( 1 << 2 )
#define OSAL_MQTT_CLIENT_DISCONNECTED_BIT        ( 1 << 3 )

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the necessary variables for handling MQTT Agent events.
 *
 * @return OSAL_ERR_OK if initialized properly, else returns the error met.
 */
osal_err_t osal_mqtt_event_init( void );

/**
 * @brief Deinitialize the MQTT Agent events.
 */
void osal_mqtt_event_deinit( void );

/**
 * @brief Set all specified bits.
 * @param[in] uxBitsToSet The bits to set.
 */
void osal_mqtt_event_set_bits( const osal_event_group_bits_t uxBitsToSet );
/**
 * @brief Clear all specified bits.
 * @param[in] uxBitsToClear The bits to clear.
 */
void osal_mqtt_event_clear_bits( const osal_event_group_bits_t uxBitsToClear );
/**
 * @brief Wait for all bits to be set in the underlying Event Group. Does not clear the bits.
 * @param[in] uxBitsToWaitFor The bits to wait for.
 * @param[in] timeout_ms Timeout in milliseconds.
 * @return true if the all waited bits were set within the timeout, false otherwise.
 */
bool osal_mqtt_event_wait_for_all_bits( const osal_event_group_bits_t uxBitsToWaitFor, uint32_t timeout_ms );
/**
 * @brief Get the status of the requested bits at the current time.
 * @param[in] uxBitsToGet The bits to read.
 * @return The requested bits, as currently set in the underlying event group.
 */
osal_event_group_bits_t osal_mqtt_event_get_bits( const osal_event_group_bits_t uxBitsToGet );

/* Shorthand macros **************************************************/

/**
 * @brief Wait for the client to be connected.
 * @param[in] timeout_ms Timeout in milliseconds.
 * @return true if the client is connected, false otherwise.
 */
#define osal_mqtt_event_wait_client_connected( timeout_ms ) osal_mqtt_event_wait_for_all_bits( OSAL_MQTT_CLIENT_CONNECTED_BIT, timeout_ms )

/**
 * @brief Wait for the client to have disconnected.
 * @param[in] timeout_ms Timeout in milliseconds.
 * @return true if the client has disconnected, false otherwise.
 */
#define osal_mqtt_event_wait_client_has_disconnected( timeout_ms ) osal_mqtt_event_wait_for_all_bits( OSAL_MQTT_CLIENT_DISCONNECTED_BIT, timeout_ms )

/**
 * @brief Notify the network connected event.
 */
#define osal_mqtt_event_notify_network_connected() do { \
    osal_mqtt_event_clear_bits( OSAL_MQTT_NETWORK_DISCONNECTED_BIT ); \
    osal_mqtt_event_set_bits( OSAL_MQTT_NETWORK_CONNECTED_BIT ); \
} while (0)

/**
 * @brief Notify the network disconnected event.
 */
#define osal_mqtt_event_notify_network_disconnected() do { \
    osal_mqtt_event_clear_bits( OSAL_MQTT_NETWORK_CONNECTED_BIT ); \
    osal_mqtt_event_set_bits( OSAL_MQTT_NETWORK_DISCONNECTED_BIT ); \
} while (0)

/**
 * @brief Notify the client connected event.
 */
#define osal_mqtt_event_notify_client_connected() do { \
    osal_mqtt_event_clear_bits( OSAL_MQTT_CLIENT_DISCONNECTED_BIT ); \
    osal_mqtt_event_set_bits( OSAL_MQTT_CLIENT_CONNECTED_BIT ); \
} while (0)

/**
 * @brief Notify the client disconnected event.
 */
#define osal_mqtt_event_notify_client_disconnected() do { \
    osal_mqtt_event_clear_bits( OSAL_MQTT_CLIENT_CONNECTED_BIT ); \
    osal_mqtt_event_set_bits( OSAL_MQTT_CLIENT_DISCONNECTED_BIT ); \
} while (0)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSAL_MQTT_EVENTS_H */
