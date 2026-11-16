/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_endpoint_map.c
 * @brief Persistent map from a node ID and device ID to a Matter endpoint, so that a device that comes back keeps it.
 */

#include <stdlib.h>
#include <string.h>

#include "constants/esp_rmaker_nvs_common.h"
#include "util/esp_rmaker_crypto.h"
#include "util/esp_rmaker_nvs.h"

#include "rm_mirror_internal.h"

#include "osal_log.h"

static const char *TAG = "rm_mirror_epmap";

#ifndef CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_ENDPOINT_MAP_MAX
#define CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_ENDPOINT_MAP_MAX 32
#endif

#define RM_MIRROR_EP_MAP_KEY       "ep_map"
#define RM_MIRROR_EP_MAP_MAX       CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_ENDPOINT_MAP_MAX
#define RM_MIRROR_EP_MAP_KEY_LEN   RM_MIRROR_DEVICE_KEY_LEN

/* Stored as is */
typedef struct {
    uint16_t endpoint_id;
    uint8_t lowered;  /* the port's store holds this device's state on the endpoint */
    uint8_t reserved;
    uint8_t key[RM_MIRROR_EP_MAP_KEY_LEN];  /* first bytes of SHA-256(node ID, NUL, device ID) */
} __entry_t;

_Static_assert(sizeof(__entry_t) == 18, "the stored map entry layout changed");
_Static_assert(RM_MIRROR_EP_MAP_KEY_LEN <= RMAKER_CRYPTO_SHA256_HASH_LEN, "a map key is a SHA-256 prefix");

/* Oldest first. Used at init and under the engine's membership lock only. */
static struct {
    __entry_t entries[RM_MIRROR_EP_MAP_MAX];
    bool in_use[RM_MIRROR_EP_MAP_MAX];  /* assigned since load */
    size_t n;
    bool dirty;
} __map;

void rm_mirror_endpoint_map_load(void)
{
    memset(&__map, 0, sizeof(__map));
    size_t len = 0;
    uint8_t *blob = esp_rmaker_nvs_get_binary(RMAKER_NVS_PART_NAME, RM_MIRROR_NVS_NAMESPACE, RM_MIRROR_EP_MAP_KEY,
                    &len);
    if (!blob) {
        return;
    }
    if (len % sizeof(__entry_t) == 0) {
        /* A smaller Kconfig max keeps the newest entries */
        const size_t stored = len / sizeof(__entry_t);
        __map.n = stored < RM_MIRROR_EP_MAP_MAX ? stored : RM_MIRROR_EP_MAP_MAX;
        memcpy(__map.entries, blob + (stored - __map.n) * sizeof(__entry_t), __map.n * sizeof(__entry_t));
        __map.dirty = __map.n < stored;
    } else {
        OSAL_LOGW(TAG, "Endpoint map of %u bytes is not valid; starting empty.", (unsigned)len);
    }
    free(blob);
}

void rm_mirror_endpoint_map_unload(void)
{
    memset(&__map, 0, sizeof(__map));
}

bool rm_mirror_device_key(const char *node_id, const char *device_id, uint8_t key[RM_MIRROR_DEVICE_KEY_LEN])
{
    if (!device_id) {
        return false;
    }
    const char *node = node_id ? node_id : "";
    const size_t node_len = strlen(node);
    const size_t device_len = strlen(device_id);
    /* The NUL keeps ("ab", "c") apart from ("a", "bc") */
    uint8_t *input = malloc(node_len + 1 + device_len);
    if (!input) {
        return false;
    }
    memcpy(input, node, node_len + 1);
    memcpy(input + node_len + 1, device_id, device_len);
    uint8_t hash[RMAKER_CRYPTO_SHA256_HASH_LEN];
    const bool ok = esp_rmaker_crypto_gen_sha256(input, node_len + 1 + device_len, hash) == ESP_RMAKER_OK;
    free(input);
    memcpy(key, hash, RM_MIRROR_EP_MAP_KEY_LEN);
    return ok;
}

static int __find_key(const uint8_t key[RM_MIRROR_EP_MAP_KEY_LEN])
{
    for (size_t i = 0; i < __map.n; i++) {
        if (memcmp(__map.entries[i].key, key, RM_MIRROR_EP_MAP_KEY_LEN) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int __find_endpoint(uint16_t endpoint_id)
{
    for (size_t i = 0; i < __map.n; i++) {
        if (__map.entries[i].endpoint_id == endpoint_id) {
            return (int)i;
        }
    }
    return -1;
}

static void __erase(size_t idx)
{
    memmove(&__map.entries[idx], &__map.entries[idx + 1], (__map.n - idx - 1) * sizeof(__map.entries[0]));
    memmove(&__map.in_use[idx], &__map.in_use[idx + 1], (__map.n - idx - 1) * sizeof(__map.in_use[0]));
    __map.n--;
    __map.dirty = true;
}

uint16_t rm_mirror_endpoint_map_find(const char *node_id, const char *device_id)
{
    uint8_t key[RM_MIRROR_EP_MAP_KEY_LEN];
    if (!rm_mirror_device_key(node_id, device_id, key)) {
        return RM_MIRROR_ENDPOINT_NONE;
    }
    const int idx = __find_key(key);
    return (idx < 0 || __map.in_use[idx]) ? RM_MIRROR_ENDPOINT_NONE : __map.entries[idx].endpoint_id;
}

bool rm_mirror_endpoint_map_assign(const char *node_id, const char *device_id, uint16_t endpoint_id)
{
    uint8_t key[RM_MIRROR_EP_MAP_KEY_LEN];
    if (!rm_mirror_device_key(node_id, device_id, key)) {
        return false;
    }
    int idx = __find_key(key);
    const bool lowered = idx >= 0 && __map.entries[idx].endpoint_id == endpoint_id && __map.entries[idx].lowered;
    if (idx >= 0) {
        __erase((size_t)idx);
    }
    /* The endpoint now holds this device, whatever the map said before */
    idx = __find_endpoint(endpoint_id);
    if (idx >= 0) {
        __erase((size_t)idx);
    }
    if (__map.n == RM_MIRROR_EP_MAP_MAX) {
        size_t victim = 0;
        while (victim < __map.n && __map.in_use[victim]) {
            victim++;
        }
        if (victim == __map.n) {
            OSAL_LOGW(TAG, "Endpoint map full; device '%s' of node '%s' gets no stable endpoint.", device_id,
                      node_id ? node_id : "");
            return false;
        }
        __erase(victim);
    }
    __entry_t *entry = &__map.entries[__map.n];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->key, key, sizeof(entry->key));
    entry->endpoint_id = endpoint_id;
    entry->lowered = lowered;
    __map.in_use[__map.n++] = true;
    __map.dirty = true;
    return lowered;
}

void rm_mirror_endpoint_map_set_lowered(uint16_t endpoint_id)
{
    const int idx = __find_endpoint(endpoint_id);
    if (idx >= 0 && !__map.entries[idx].lowered) {
        __map.entries[idx].lowered = 1;
        __map.dirty = true;
    }
}

void rm_mirror_endpoint_map_release(uint16_t endpoint_id)
{
    const int idx = __find_endpoint(endpoint_id);
    if (idx < 0) {
        return;
    }
    __map.in_use[idx] = false;
    if (__map.entries[idx].lowered) {
        __map.entries[idx].lowered = 0;
        __map.dirty = true;
    }
}

esp_rmaker_error_t rm_mirror_endpoint_map_commit(void)
{
    if (!__map.dirty) {
        return ESP_RMAKER_OK;
    }
    esp_rmaker_error_t err = esp_rmaker_nvs_update_binary(RMAKER_NVS_PART_NAME, RM_MIRROR_NVS_NAMESPACE,
                             RM_MIRROR_EP_MAP_KEY, __map.entries, __map.n * sizeof(__entry_t));
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Could not store the endpoint map; the next boot seeds the changed endpoints from the params.");
        return err;
    }
    __map.dirty = false;
    return ESP_RMAKER_OK;
}
