/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mqtt_impl.h
 * @brief Setup of the platform MQTT client vtable (::osal_mqtt_impl_t).
 *
 * Taken largely from ESP RainMaker Classic SDK's "esp_rmaker_mqtt_glue.h".
 */

#ifndef OSAL_MQTT_IMPL_H
#define OSAL_MQTT_IMPL_H

/* Common includes */
#include "osal_mqtt_prototypes.h"
#include "osal_mqtt_subscription_manager.h"
#include "osal_mqtt_events.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief This function initializes MQTT implementation layer with all the default functions.
 *
 * All future MQTT function calls should then be made with this impl, e.g., mqtt_impl.connect().
 *
 * @param[out] mqtt_impl Pointer to an allocated MQTT implementation structure.
 *
 * @return OSAL_ERR_OK on success.
 * @return An error code in case of any error.
 */
osal_err_t osal_mqtt_impl_setup( osal_mqtt_impl_t *mqtt_impl );

/* Common public function declarations ***********************/

/**
 * @brief common init sequence, at the start of init functions. This includes:
 *
 * 1. initializing the events
 * 2. initializing the subscription manager
 *
 * @return OSAL_ERR_OK on success.
 * @return An error code in case of any error.
 */
osal_err_t osal_mqtt_pre_init( void );

/**
 * @brief common deinit sequence, at the end of deinit functions. This includes:
 *
 * 1. clearing the subscription manager
 * 2. clearing the events
 *
 * @return OSAL_ERR_OK on success.
 * @return An error code in case of any error.
 */
osal_err_t osal_mqtt_post_deinit( void );

#ifdef __cplusplus
}
#endif

#endif /* OSAL_MQTT_IMPL_H */
