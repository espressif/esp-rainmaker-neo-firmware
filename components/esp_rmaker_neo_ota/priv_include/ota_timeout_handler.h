/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_timeout_handler.h
 * @brief Private header for handling timeouts in the OTA process
 */

#ifndef __OTA_TIMEOUT_HANDLER_H__
#define __OTA_TIMEOUT_HANDLER_H__

/* Includes ******************************************************************/

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Standard includes */
#include <stdint.h>

/* Types ******************************************************************/

/**
 * @brief Callback function for the timeout handler
 *
 * @param[in] priv_data Private data passed to the timeout handler
 */
typedef void (*rmaker_ota_timeout_handler_callback_t)(void *priv_data);

/**
 * @brief Timeout handler config
 */
typedef struct {
    /** Timeout value in milliseconds */
    uint32_t timeout_ms;
    /** Callback function */
    rmaker_ota_timeout_handler_callback_t callback;
    /** Private data to be passed to the callback function */
    void *priv_data;
} rmaker_ota_timeout_handler_config_t;

/**
 * @brief Timeout handler handle
 */
typedef void *rmaker_ota_timeout_handler_handle_t;

/* Public function declarations ****************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the timeout handler
 *
 * @param[in] config Timeout handler config
 * @param[out] p_handle Pointer to the timeout handler handle
 * @return ESP_RMAKER_OK on success, error code otherwise
 */
esp_rmaker_error_t rmaker_ota_timeout_handler_init(const rmaker_ota_timeout_handler_config_t *config, rmaker_ota_timeout_handler_handle_t *p_handle);

/**
 * @brief Deinitialize the timeout handler.
 * The handle is invalid after deinitialization.
 *
 * @param[in] handle Timeout handler handle
 * @return ESP_RMAKER_OK on success, error code otherwise
 */
esp_rmaker_error_t rmaker_ota_timeout_handler_deinit(rmaker_ota_timeout_handler_handle_t handle);

/**
 * @brief [Re]start the timeout handler.
 * - If the timeout handler is not running, it will be started.
 * - If the timeout handler is running, the timer is reset to the timeout value.
 *
 * @param[in] handle Timeout handler handle
 * @return ESP_RMAKER_OK on success, error code otherwise
 */
esp_rmaker_error_t rmaker_ota_timeout_handler_restart(rmaker_ota_timeout_handler_handle_t handle);

/**
 * @brief Stop the timeout handler.
 * This will pause the timer, but the handle is still valid.
 * - Call restart to resume the timer.
 * - Call deinit to destroy the handle.
 *
 * @param[in] handle Timeout handler handle
 * @return ESP_RMAKER_OK on success, error code otherwise
 */
esp_rmaker_error_t rmaker_ota_timeout_handler_stop(rmaker_ota_timeout_handler_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_TIMEOUT_HANDLER_H__ */
