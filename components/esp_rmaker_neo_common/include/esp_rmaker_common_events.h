/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_common_events.h
 * @brief Events shared by all RainMaker Neo components, posted on the ``RMAKER_COMMON_EVENT`` base.
 */

#ifndef __ESP_RMAKER_COMMON_EVENTS_H__
#define __ESP_RMAKER_COMMON_EVENTS_H__

/* Includes **********************************************************************/

/* Platform common includes */
#include "osal_event_loop.h"

/* Constants **********************************************************************/

/**
 * @brief Event loop base event.
 */
OSAL_EVENT_DECLARE_BASE(RMAKER_COMMON_EVENT);

/**
 * @brief Events posted on the ``RMAKER_COMMON_EVENT`` base.
 */
typedef enum {
    /** Wildcard: register a handler with this id to receive every RMAKER_COMMON_EVENT. */
    RMAKER_COMMON_EVENT_BASE_ANY = OSAL_EVENT_ID_ANY,

    /* System Events ***************************************************************/

    /** System service reboot.
     * Event data will contain the seconds after which the system will reboot.
     */
    RMAKER_EVENT_REBOOT,

    /** System service network reset.
     * No event data will be provided.
     */
    RMAKER_EVENT_NETWORK_RESET,

    /** System service factory reset.
     * No event data will be provided.
     */
    RMAKER_EVENT_FACTORY_RESET,

    /* MQTT Events *****************************************************************/

    /** MQTT connected.
     * Event data will be NULL.
     */
    RMAKER_MQTT_EVENT_CONNECTED,

    /** MQTT disconnected.
     * Event data will be NULL.
     */
    RMAKER_MQTT_EVENT_DISCONNECTED,

    /** MQTT message published successfully.
     * Event data is a pointer to a osal_mqtt_event_loop_data_on_complete_t.
     */
    RMAKER_MQTT_EVENT_PUBLISHED,

    /** MQTT message subscribed successfully.
     * Event data is a pointer to a osal_mqtt_event_loop_data_on_complete_t.
     */
    RMAKER_MQTT_EVENT_SUBSCRIBED,

    /** MQTT message unsubscribed successfully.
     * Event data is a pointer to a osal_mqtt_event_loop_data_on_complete_t.
     */
    RMAKER_MQTT_EVENT_UNSUBSCRIBED,

    /* Timezone Events *************************************************************/

    /** POSIX Timezone Changed.
     * Eg. "PST8PDT,M3.2.0,M11.1.0"
     * Event data will contain the NULL terminated POSIX timezone (string).
     */
    RMAKER_EVENT_TZ_POSIX_CHANGED,

    /** Timezone Changed.
     * Eg. "America/Los_Angeles"
     * Note that whenever this event is received, the RMAKER_EVENT_TZ_POSIX_CHANGED event
     * will also be received, but not necessarily vice versa.
     * Event data will contain the NULL terminated Timezone (string).
     */
    RMAKER_EVENT_TZ_CHANGED,
} esp_rmaker_common_event_t;

#endif /* __ESP_RMAKER_COMMON_EVENTS_H__ */
