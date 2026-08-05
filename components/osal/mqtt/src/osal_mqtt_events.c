/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mqtt_events.c
 * @brief common functions for MQTT events
 *
 */

/* Includes *******************************************************************/

/* Standard includes. */
#include <stdint.h>

/* MQTT common includes. */
#include "osal_mqtt_events.h"
#include "osal_mqtt_prototypes.h"

/* Platform common includes. */
#include "osal_event_group.h"

/* Global variables ***********************************************************/

/**
 * @brief The event group used to manage MQTT client events.
 */
static osal_event_group_handle_t event_group;

/** Public function definitions *******************************************/

osal_err_t osal_mqtt_event_init(void)
{
    osal_err_t ret = OSAL_ERR_OK;

    /* Initialize the MQTT client event group. */
    if (event_group == NULL) {
        event_group = osal_event_group_create();
        if (event_group == NULL) {
            return OSAL_ERR_NO_MEM;
        }
        osal_mqtt_event_set_bits( OSAL_MQTT_NETWORK_DISCONNECTED_BIT | OSAL_MQTT_CLIENT_DISCONNECTED_BIT );
    }

    return ret;
}

void osal_mqtt_event_deinit( void )
{
    if (event_group != NULL) {
        osal_event_group_delete(event_group);
        event_group = NULL;
    }
}

void osal_mqtt_event_set_bits( const osal_event_group_bits_t uxBitsToSet )
{
    if (osal_mqtt_event_init() != OSAL_ERR_OK) {
        return;
    }
    osal_event_group_set_bits( event_group, uxBitsToSet );
}

void osal_mqtt_event_clear_bits( const osal_event_group_bits_t uxBitsToClear )
{
    if (osal_mqtt_event_init() != OSAL_ERR_OK) {
        return;
    }
    osal_event_group_clear_bits( event_group, uxBitsToClear );
}

bool osal_mqtt_event_wait_for_all_bits( const osal_event_group_bits_t uxBitsToWaitFor, uint32_t timeout_ms )
{
    if (osal_mqtt_event_init() != OSAL_ERR_OK) {
        return false;
    }
    osal_event_group_bits_t bits = osal_event_group_wait_bits( event_group,
                                   uxBitsToWaitFor,
                                   false,
                                   true,
                                   osal_ticks_from_ms(timeout_ms) );
    return ((bits & uxBitsToWaitFor) == uxBitsToWaitFor);
}

osal_event_group_bits_t osal_mqtt_event_get_bits( const osal_event_group_bits_t uxBitsToGet )
{
    if (osal_mqtt_event_init() != OSAL_ERR_OK) {
        return 0;
    }
    return osal_event_group_get_bits(event_group) & uxBitsToGet;
}
