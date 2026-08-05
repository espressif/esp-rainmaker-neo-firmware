/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bridge_child_triggers_nvs.h
 * @brief Per-child automation trigger-details persistence.
 *
 * Stores each child's trigger details as an opaque binary blob under
 * ``RMAKER_NVS_BRIDGE_TRIGGERS_NAMESPACE``, keyed by the child's
 * SHA-256-derived NVS key (``bridge_internal_child_nvs_key``). The blob
 * is the compact form produced by ``esp_rmaker_trigger_details_encode``;
 * this layer is format-agnostic and just stores/returns the bytes.
 * Separate from the packed per-child record (``bridge_child_nvs``)
 * because the payload is unbounded.
 *
 * Only present when ``CONFIG_RMNG_BRIDGE_ENABLED``.
 */

#ifndef __BRIDGE_CHILD_TRIGGERS_NVS_H__
#define __BRIDGE_CHILD_TRIGGERS_NVS_H__

#include "sdkconfig.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

#include <stddef.h>
#include <stdint.h>

#include "esp_rmaker_bridge.h"
#include "esp_rmaker_error_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Persist a child's trigger-details blob.
 * @param[in] child     Child handle.
 * @param[in] data      Bytes to store (may contain NULs).
 * @param[in] data_len  Number of bytes.
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
esp_rmaker_error_t bridge_child_triggers_nvs_set(esp_rmaker_bridge_child_handle_t child,
        const uint8_t *data, size_t data_len);

/**
 * @brief Load a child's trigger-details blob.
 * @param[in]  child    Child handle.
 * @param[out] out_len  On success, blob length in bytes.
 * @return malloc'd buffer (caller frees), or NULL if none stored / on error.
 */
uint8_t *bridge_child_triggers_nvs_get(esp_rmaker_bridge_child_handle_t child, size_t *out_len);

/**
 * @brief Erase a child's stored trigger details. Best-effort.
 * @param[in] child  Child handle.
 * @return ESP_RMAKER_OK on success (including absent), otherwise an error code.
 */
esp_rmaker_error_t bridge_child_triggers_nvs_erase(esp_rmaker_bridge_child_handle_t child);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

#endif /* __BRIDGE_CHILD_TRIGGERS_NVS_H__ */
