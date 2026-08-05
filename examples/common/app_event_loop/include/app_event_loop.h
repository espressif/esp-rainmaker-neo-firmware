/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_event_loop.h
 * @brief App event loop header file.
 */

#ifndef __APP_EVENT_LOOP_H__
#define __APP_EVENT_LOOP_H__

/* Includes **********************************************************************/

/* RMNG includes */
#include "esp_rmaker_error_types.h"

/* Public function declarations ****************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register a default event handler for RainMaker Neo events.
 *
 * @return
 *  - ESP_RMAKER_OK: The event handler was registered successfully.
 *  - Others: An error occurred while registering the event handler.
 */
esp_rmaker_error_t app_event_loop_register_default_handler(void);

/**
 * @brief Unregister the default event handler for RainMaker Neo events.
 *
 * @return
 *  - ESP_RMAKER_OK: The event handler was unregistered successfully.
 *  - Others: An error occurred while unregistering the event handler.
 */
esp_rmaker_error_t app_event_loop_unregister_default_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_EVENT_LOOP_H__ */
