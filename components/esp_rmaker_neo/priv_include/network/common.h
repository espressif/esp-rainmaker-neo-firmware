/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file common.h
 * @brief Common network variables and functions.
 */

#ifndef __NETWORK_COMMON_H__
#define __NETWORK_COMMON_H__

/* Standard includes */
#include <stddef.h>
#include <stdint.h>

/* Platform common includes */
#include "osal_event_group.h"

/* Error types includes */
#include "esp_rmaker_error_types.h"

/* MQTT includes */
#include "esp_rmaker_mqtt_impl.h"

/* Identity constants */
#include "constants/identity.h"

/* Structure definitions **********************************************************/

typedef struct {
    uint8_t *payload; /* Payload received from the network. */
    size_t payload_len; /* Length of the payload. */
    /* Subgroup parsed off the inbound MQTT topic (group-control path only).
     * Empty string for broadcast and non-group payloads. The group-control
     * dispatcher filters node fan-out by this against each node's group
     * info via esp_rmaker_node_is_in_subgroup. */
    char subgroup[RMAKER_SUBGROUP_BUFFER_SIZE];
} esp_rmaker_network_payload_t;

/* Public function declarations ****************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the network event group.
 */
esp_rmaker_error_t esp_rmaker_network_init(void);

/**
 * @brief De-initialize the network event group.
 */
esp_rmaker_error_t esp_rmaker_network_deinit(void);

/**
 * @brief Wait for a combination of bits to be set in the network event group.
 * @note This does not clear the bits after waiting.
 * @param[in] bits_to_wait_for The bits to wait for.
 * @param[in] timeout_ms Timeout in milliseconds.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_network_wait_bits(osal_event_group_bits_t bits_to_wait_for, uint32_t timeout_ms);

/**
 * @brief Set bits in the network event group.
 *
 * @param[in] bits_to_set The bits to set.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_network_set_bits(osal_event_group_bits_t bits_to_set);

/**
 * @brief Clear bits in the network event group.
 *
 * @param[in] bits_to_clear The bits to clear.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_network_clear_bits(osal_event_group_bits_t bits_to_clear);

/**
 * @brief Get bits in the network event group.
 *
 * @return Bits in the network event group.
 */
osal_event_group_bits_t esp_rmaker_network_get_bits(void);

/**
 * @brief Make payload.
 *
 * @param[in] payload Payload received from the network.
 * @param[in] payload_len Length of the payload.
 *
 * @return Pointer to the payload, of type esp_rmaker_network_payload_t, on success.
 * @return NULL on failure.
 */
esp_rmaker_network_payload_t *esp_rmaker_network_make_payload(void *payload, size_t payload_len);

/**
 * @brief Free payload.
 *
 * @param[in] p_payload Pointer to the payload, of type esp_rmaker_network_payload_t.
 */
void esp_rmaker_network_free_payload(esp_rmaker_network_payload_t *p_payload);

#ifdef __cplusplus
}
#endif

#endif /* __NETWORK_COMMON_H__ */
