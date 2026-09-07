/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_bridge.c
 * @brief Matter bridge topology: the Aggregator, bridged endpoints, and their reachable values.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_rmaker_standard_types.h"

#include "rm_mirror_internal.h"

#include "osal_log.h"

static const char *TAG = "rm_mirror_bridge";

/* Unique ID: the device key in hex; Matter allows 32 characters */
#define RM_MIRROR_UNIQUE_ID_LEN (RM_MIRROR_DEVICE_KEY_LEN * 2)
_Static_assert(RM_MIRROR_UNIQUE_ID_LEN <= 32, "a UniqueID has 32 characters at most");

/* Devices stored as unreachable; a device not in the list is reachable */
static struct {
    uint16_t aggregator_id;
    uint8_t (*unreachable)[RM_MIRROR_DEVICE_KEY_LEN];
    size_t n_unreachable;
} __bridge = { .aggregator_id = RM_MIRROR_ENDPOINT_NONE };

esp_rmaker_error_t rm_mirror_bridge_init(const rm_mirror_port_ops_t *port)
{
    rm_mirror_bridge_deinit();
    if (!port->aggregator_create) {
        return ESP_RMAKER_OK;
    }
    uint16_t id = RM_MIRROR_ENDPOINT_NONE;
    esp_rmaker_error_t err = port->aggregator_create(&id);
    if (err == ESP_RMAKER_NOT_SUPPORTED) {
        return ESP_RMAKER_OK;
    }
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to create the Aggregator endpoint.");
        return err;
    }
    __bridge.aggregator_id = id;
    OSAL_LOGI(TAG, "Aggregator on endpoint %u.", (unsigned)id);
    return ESP_RMAKER_OK;
}

void rm_mirror_bridge_deinit(void)
{
    free(__bridge.unreachable);
    memset(&__bridge, 0, sizeof(__bridge));
    __bridge.aggregator_id = RM_MIRROR_ENDPOINT_NONE;
}

uint16_t rm_mirror_bridge_aggregator(void)
{
    return __bridge.aggregator_id;
}

static int __find_unreachable(const uint8_t key[RM_MIRROR_DEVICE_KEY_LEN])
{
    for (size_t i = 0; i < __bridge.n_unreachable; i++) {
        if (memcmp(__bridge.unreachable[i], key, RM_MIRROR_DEVICE_KEY_LEN) == 0) {
            return (int)i;
        }
    }
    return -1;
}

esp_rmaker_error_t rm_mirror_bridge_endpoint(const rm_mirror_port_ops_t *port, uint16_t endpoint_id,
        const char *node_id, const esp_rmaker_device_t *device)
{
    if (!port->endpoint_bridge) {
        return ESP_RMAKER_NOT_SUPPORTED;
    }
    const char *device_id = esp_rmaker_device_get_id(device);
    uint8_t key[RM_MIRROR_DEVICE_KEY_LEN];
    if (!rm_mirror_device_key(node_id, device_id, key)) {
        return ESP_RMAKER_NO_MEM;
    }
    char unique_id[RM_MIRROR_UNIQUE_ID_LEN + 1];
    for (size_t i = 0; i < RM_MIRROR_DEVICE_KEY_LEN; i++) {
        snprintf(&unique_id[i * 2], 3, "%02x", key[i]);
    }

    const char *label = device_id;
    const esp_rmaker_param_t *name = esp_rmaker_device_get_param_by_type(device, ESP_RMAKER_PARAM_NAME);
    const esp_rmaker_param_val_t *val = name ? esp_rmaker_param_get_val((esp_rmaker_param_t *)name) : NULL;
    if (val && val->type == RMAKER_VAL_TYPE_STRING && val->val.s && val->val.s[0]) {
        label = val->val.s;
    }
    return port->endpoint_bridge(endpoint_id, unique_id, label, __find_unreachable(key) < 0);
}

esp_rmaker_error_t rm_mirror_bridge_store_reachable(const char *node_id, const char *device_id, bool reachable)
{
    uint8_t key[RM_MIRROR_DEVICE_KEY_LEN];
    if (!rm_mirror_device_key(node_id, device_id, key)) {
        return ESP_RMAKER_NO_MEM;
    }
    const int idx = __find_unreachable(key);
    if (reachable && idx >= 0) {
        memmove(&__bridge.unreachable[idx], &__bridge.unreachable[idx + 1],
                (__bridge.n_unreachable - (size_t)idx - 1) * sizeof(__bridge.unreachable[0]));
        __bridge.n_unreachable--;
    } else if (!reachable && idx < 0) {
        void *grown = realloc(__bridge.unreachable, (__bridge.n_unreachable + 1) * sizeof(__bridge.unreachable[0]));
        if (!grown) {
            return ESP_RMAKER_NO_MEM;
        }
        __bridge.unreachable = grown;
        memcpy(__bridge.unreachable[__bridge.n_unreachable++], key, RM_MIRROR_DEVICE_KEY_LEN);
    }
    return ESP_RMAKER_OK;
}
