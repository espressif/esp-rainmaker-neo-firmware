/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file shadows.h
 * @brief Indexed and named shadow operations
 */

#ifndef __HOST_CTRL_NETWORK_SHADOWS_H__
#define __HOST_CTRL_NETWORK_SHADOWS_H__

#include <stdint.h>
#include "esp_rmaker_error_types.h"

/* Public function declarations *************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/* --- Init/deinit --- */

/**
 * @brief Initialize the variables used by the shadows management.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_shadows_init(void);

/**
 * @brief Deinitialize the variables used by the shadows management.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_shadows_deinit(void);

/* --- Subscription management --- */

/**
 * @brief Subscribe to the get/accepted topic of the indexed shadow.
 * @note This blocks until the subscription is complete or the timeout is reached.
 * @param[in] timeout_ms The timeout in milliseconds for all subscriptions.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_indexed_shadow_subscribe_get_accepted(uint32_t timeout_ms);

/**
 * @brief Unsubscribe from the get/accepted topic of the indexed shadow.
 * @note This blocks until the unsubscription is complete or the timeout is reached.
 * @param[in] timeout_ms The timeout in milliseconds for all unsubscriptions.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_indexed_shadow_unsubscribe_get_accepted(uint32_t timeout_ms);

/**
 * @brief Subscribe to the get/accepted topic of the named shadow.
 * @note This blocks until the subscription is complete or the timeout is reached.
 * @param[in] timeout_ms The timeout in milliseconds for all subscriptions.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_named_shadow_subscribe_get_accepted(uint32_t timeout_ms);

/**
 * @brief Unsubscribe from the get/accepted topic of the named shadow.
 * @note This blocks until the unsubscription is complete or the timeout is reached.
 * @param[in] timeout_ms The timeout in milliseconds for all unsubscriptions.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t esp_rmaker_named_shadow_unsubscribe_get_accepted(uint32_t timeout_ms);

/* --- Get reported documents --- */

/**
 * @brief Get the reported document of the indexed shadow.
 * @note This is a blocking operation.
 * @param[in] timeout_ms The timeout in milliseconds.
 * @return pointer to the reported document on success. NULL on failure.
 * @note The reported document is a JSON string. Must be freed by the caller.
 */
char *esp_rmaker_indexed_shadow_get_reported(uint32_t timeout_ms);

/**
 * @brief Get the reported document of the named shadow.
 * @note This is a blocking operation.
 * @param[in] timeout_ms The timeout in milliseconds.
 * @return pointer to the reported document on success. NULL on failure.
 * @note The reported document is a JSON string. Must be freed by the caller.
 */
char *esp_rmaker_named_shadow_get_reported(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __HOST_CTRL_NETWORK_SHADOWS_H__ */
