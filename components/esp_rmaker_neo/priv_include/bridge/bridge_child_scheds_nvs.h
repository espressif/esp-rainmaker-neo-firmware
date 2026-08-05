/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bridge_child_scheds_nvs.h
 * @brief Per-child schedule-details persistence.
 *
 * Stores each child's schedule details as the raw JSON array string the
 * cloud sent, under ``RMAKER_NVS_BRIDGE_SCHEDS_NAMESPACE``, keyed by the
 * child's SHA-256-derived NVS key (``bridge_internal_child_nvs_key``).
 * Separate from the packed per-child record (``bridge_child_nvs``) because
 * the payload is unbounded.
 *
 * The schedule service persists the details JSON itself rather than relying
 * on esp_schedule's NVS: esp_schedule stores only the trigger config, so the
 * action a fired schedule must apply would not survive a reboot. Replaying
 * the original JSON on boot reproduces both.
 *
 * Only present when ``CONFIG_RMNG_BRIDGE_ENABLED``.
 */

#ifndef __BRIDGE_CHILD_SCHEDS_NVS_H__
#define __BRIDGE_CHILD_SCHEDS_NVS_H__

#include "sdkconfig.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

#include "esp_rmaker_bridge.h"
#include "esp_rmaker_error_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Persist a child's schedule-details JSON.
 * @param[in] child    Child handle.
 * @param[in] details  NUL-terminated JSON array string.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t bridge_child_scheds_nvs_set(esp_rmaker_bridge_child_handle_t child,
        const char *details);

/**
 * @brief Load a child's schedule-details JSON.
 * @param[in] child  Child handle.
 * @return malloc'd NUL-terminated string (caller frees), or NULL if none
 *         stored / on error.
 */
char *bridge_child_scheds_nvs_get(esp_rmaker_bridge_child_handle_t child);

/**
 * @brief Erase a child's stored schedule details. Best-effort.
 * @param[in] child  Child handle.
 * @return ESP_RMAKER_OK on success (including absent), otherwise an error code.
 */
esp_rmaker_error_t bridge_child_scheds_nvs_erase(esp_rmaker_bridge_child_handle_t child);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

#endif /* __BRIDGE_CHILD_SCHEDS_NVS_H__ */
