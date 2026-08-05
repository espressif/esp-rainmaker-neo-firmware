/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file event_loop.h
 * @brief Usage of the event loop in the ESP RainMaker Neo SDK.
 */

#ifndef __EVENT_LOOP_H__
#define __EVENT_LOOP_H__

/* Standard C headers */
#include <stdint.h>
#include <stddef.h>

/* Public event loop includes */
#include "esp_rmaker_event_loop.h"

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Public function definitions ****************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the event loop, and register some default handlers.
 * @param[in] mqtt_connection_handler The handler for the MQTT connection event. Will be registered for the RMAKER_MQTT_EVENT_CONNECTED and RMAKER_MQTT_EVENT_DISCONNECTED events.
 */
esp_rmaker_error_t event_loop_init(osal_event_handler_t mqtt_connection_handler);

/**
 * @brief Deinitialize the event loop.
 */
esp_rmaker_error_t event_loop_deinit(void);

/* --- MQTT registration functions --- */

/**
 * @brief Register an event handler for the MQTT publish/subscribe/unsubscribe complete events.
 */
esp_rmaker_error_t event_loop_register_mqtt_on_complete_handler(osal_event_handler_t handler);

/**
 * @brief Unregister an event handler for the MQTT publish/subscribe/unsubscribe complete events.
 */
esp_rmaker_error_t event_loop_unregister_mqtt_on_complete_handler(osal_event_handler_t handler);

/* --- Timezone registration functions --- */

/**
 * @brief Register an event handler for the timezone change events.
 */
esp_rmaker_error_t event_loop_register_timezone_change_handler(osal_event_handler_t handler);

/**
 * @brief Unregister an event handler for the timezone change events.
 */
esp_rmaker_error_t event_loop_unregister_timezone_change_handler(osal_event_handler_t handler);

#ifdef __cplusplus
}
#endif

#endif /* __EVENT_LOOP_H__ */
