/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_filetype_handler_internal.h
 * @brief Internal filetype handler header
 */

#ifndef __OTA_FILETYPE_HANDLER_INTERNAL_H__
#define __OTA_FILETYPE_HANDLER_INTERNAL_H__

#include <stdbool.h>
#include <stddef.h>
#include "esp_rmaker_ota_filetype_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the filetype handler context for the default filetype
 * @note DO NOT free the returned context. It is owned by the implementation.
 *
 * @return Filetype handler context for the default filetype
 */
const esp_rmaker_ota_ft_ctx_t *filetype_handler_get_default_ctx(void);

/**
 * @brief Check if the filetype handler context is valid
 * @param[in] ctx Filetype handler context to check
 * @return True if the context is valid, false otherwise
 */
bool filetype_handler_is_valid_ctx(const esp_rmaker_ota_ft_ctx_t *ctx);

/**
 * @brief Start the status timer
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t filetype_handler_status_timer_start(void);

/**
 * @brief Stop the status timer
 * @return ESP_RMAKER_OK on success, otherwise an error code
 */
esp_rmaker_error_t filetype_handler_status_timer_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_FILETYPE_HANDLER_INTERNAL_H__ */
