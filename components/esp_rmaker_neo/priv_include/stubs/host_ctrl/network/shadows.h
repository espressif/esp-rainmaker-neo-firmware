/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file shadows.h
 * @brief This is a stub file for the indexed shadow. If remote control is enabled,
 *        this file will be replaced with the actual implementation.
 */

#ifndef __ESP_RMAKER_SHADOWS_H__
#define __ESP_RMAKER_SHADOWS_H__

#include "esp_rmaker_error_types.h"

/**
 * @brief Stub. Returns success.
 */
#define esp_rmaker_shadows_init()  ( ESP_RMAKER_OK )

/**
 * @brief Stub. Returns success.
 */
#define esp_rmaker_shadows_deinit()  ( ESP_RMAKER_OK )

/**
 * @brief Stub. Returns success.
 */
#define esp_rmaker_indexed_shadow_subscribe_get_accepted(timeout_ms)  ( ESP_RMAKER_OK )

/**
 * @brief Stub. Returns success.
 */
#define esp_rmaker_indexed_shadow_unsubscribe_get_accepted(timeout_ms)  ( ESP_RMAKER_OK )

/**
 * @brief Stub. Returns success.
 */
#define esp_rmaker_named_shadow_subscribe_get_accepted(timeout_ms)  ( ESP_RMAKER_OK )

/**
 * @brief Stub. Returns success.
 */
#define esp_rmaker_named_shadow_unsubscribe_get_accepted(timeout_ms)  ( ESP_RMAKER_OK )

#endif /* __ESP_RMAKER_SHADOWS_H__ */
