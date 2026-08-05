/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mqtt_impl.c
 * @brief common functions used across all MQTT implementations.
 *
 */

#include "osal_mqtt_impl.h"
#include "osal_event_loop.h"

osal_err_t osal_mqtt_pre_init( void )
{
    osal_err_t ret = OSAL_ERR_OK;

    // initialize events.
    ret = osal_mqtt_event_init();
    if (ret != OSAL_ERR_OK) {
        return ret;
    }

    // initialize subscription manager.
    osal_mqtt_subscription_init();

    // return result.
    return ret;
}

osal_err_t osal_mqtt_post_deinit( void )
{
    // clear the subscription manager.
    osal_mqtt_subscription_deinit();

    // clear the events.
    osal_mqtt_event_deinit();

    return OSAL_ERR_OK;
}
