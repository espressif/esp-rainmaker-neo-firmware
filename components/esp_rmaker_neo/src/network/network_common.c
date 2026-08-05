/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file network_common.c
 * @brief Common network variables and functions.
 */

#include "network/common.h"

/* Standard includes */
#include <string.h>
#include <inttypes.h>

/* Platform common includes */
#include "osal_event_group.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Error types includes */
#include "esp_rmaker_error_types.h"

/* Global variables **************************************************************/

/**
 * @brief Tag for the network common functions.
 */
static const char *TAG = "rmng_net_common";

/**
 * @brief Network event group.
 */
static osal_event_group_handle_t network_event_group = NULL;

/* Public function definitions ****************************************************/

esp_rmaker_error_t esp_rmaker_network_init(void)
{
    network_event_group = osal_event_group_create();
    if (!network_event_group) {
        OSAL_LOGE(TAG, "Failed to create network event group");
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_network_deinit(void)
{
    if (network_event_group) {
        osal_event_group_delete(network_event_group);
        network_event_group = NULL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_network_wait_bits(osal_event_group_bits_t bits_to_wait_for, uint32_t timeout_ms)
{
    osal_event_group_bits_t bits = osal_event_group_wait_bits(network_event_group, bits_to_wait_for, false, true, osal_ticks_from_ms(timeout_ms));
    if ((bits & bits_to_wait_for) != bits_to_wait_for) {
        return ESP_RMAKER_TIMEOUT;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_network_set_bits(osal_event_group_bits_t bits_to_set)
{
    osal_event_group_set_bits(network_event_group, bits_to_set);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_network_clear_bits(osal_event_group_bits_t bits_to_clear)
{
    osal_event_group_clear_bits(network_event_group, bits_to_clear);
    return ESP_RMAKER_OK;
}

osal_event_group_bits_t esp_rmaker_network_get_bits(void)
{
    return osal_event_group_get_bits(network_event_group);
}

esp_rmaker_network_payload_t *esp_rmaker_network_make_payload(void *payload, size_t payload_len)
{
    if (payload == NULL || payload_len == 0) {
        OSAL_LOGE(TAG, "Passed NULL pointers to esp_rmaker_network_make_payload: payload=%p, payload_len=%" PRIu32, payload, (uint32_t)payload_len);
        return NULL;
    }
    esp_rmaker_network_payload_t *p_payload = OSAL_CALLOC_EXTRAM(1, sizeof(esp_rmaker_network_payload_t));
    if (!p_payload) {
        OSAL_LOGE(TAG, "Failed to allocate memory for payload");
        return NULL;
    }
    p_payload->payload = OSAL_CALLOC_EXTRAM(payload_len, sizeof(uint8_t));
    if (!p_payload->payload) {
        OSAL_LOGE(TAG, "Failed to allocate memory for payload");
        free(p_payload);
        return NULL;
    }
    memcpy(p_payload->payload, payload, payload_len);
    p_payload->payload_len = payload_len;
    return p_payload;
}

void esp_rmaker_network_free_payload(esp_rmaker_network_payload_t *p_payload)
{
    if (!p_payload) {
        return;
    }

    if (p_payload->payload) {
        free(p_payload->payload);
    }

    free(p_payload);
}
