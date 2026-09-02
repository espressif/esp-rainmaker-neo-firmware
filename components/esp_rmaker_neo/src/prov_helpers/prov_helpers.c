/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file prov_helpers.c
 * @brief Common functions for the provisioning registry.
 */

/* Includes *******************************************************/

/* Declarations */
#include "prov_helpers_internal.h"

/* Standard includes */
#include <stdbool.h>

/* Platform common includes */
#include "osal_log.h"

/* Types *******************************************************/

typedef struct {
    const char *name;
    const char *version;
} app_label_t;

/* Variables *******************************************************/

static const char *TAG = "rmng_prov";

/** The labels capabilities advertise under. Each reaches the backend at most once, carrying
 * every capability declared under it. */
static const app_label_t app_labels[PROV_APP_LABEL_MAX] = {
    [PROV_APP_LABEL_RMAKER]       = { "rmaker",       "1.0" },
    [PROV_APP_LABEL_RMAKER_EXTRA] = { "rmaker_extra", "1.0" },
};

__event_loop_info_t __event_loop_info;

/** Added endpoints, by slot; a free slot is NULL. Slot order is add order, which decides the
 * BLE characteristic UUIDs and is therefore visible on the wire. */
static const void *volatile endpoints[PROV_MAX_ENDPOINTS];

/** Added capabilities, by slot; a free slot is NULL. Slot order is add order, which is the
 * order the app info lists them in. */
static const void *volatile capabilities[PROV_MAX_CAPABILITIES];

/** Whether the registry holds the provisioning event registrations. */
static bool events_registered;

/* Private function declarations *******************************************************/

static void __prov_event_handler(void *arg, osal_event_base_t event_base, int32_t event_id, void *event_data);
static void __prov_app_info_apply(void);
static osal_err_t __prov_events_register(void);
static void __prov_events_unregister(void);
static osal_err_t __prov_events_acquire(void);
static void __prov_events_release_if_empty(void);
static osal_err_t __prov_slot_add(const void *volatile *slots, size_t total_slots, const void *entry);
static osal_err_t __prov_slot_del(const void *volatile *slots, size_t total_slots, const void *entry);
static bool __prov_slots_empty(const void *volatile *slots, size_t total_slots);

/* Private function definitions *******************************************************/

/**
 * @brief Set one app info entry per label that has capabilities.
 */
static void __prov_app_info_apply(void)
{
    for (prov_app_label_t label = PROV_APP_LABEL_INVALID + 1; label < PROV_APP_LABEL_MAX; label++) {
        const char *names[PROV_MAX_CAPABILITIES];
        size_t total = __prov_app_info_collect(label, names, PROV_MAX_CAPABILITIES);
        if (total == 0) {
            continue;
        }
        if (__prov_backend_app_info_set(app_labels[label].name, app_labels[label].version,
                                        names, total) != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to set the app info for %s.", app_labels[label].name);
        }
    }
}

static void __prov_event_handler(void *arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base != __event_loop_info.event_base) {
        return;
    }

    /* Taken before any callback runs, so every endpoint present when the walk started gets its
     * turn even if a callback goes on to remove one. */
    const prov_endpoint_t *snapshot[PROV_MAX_ENDPOINTS];
    for (size_t i = 0; i < PROV_MAX_ENDPOINTS; i++) {
        snapshot[i] = (const prov_endpoint_t *)endpoints[i];
    }

    if (event_id == __event_loop_info.event_ids.prov_init) {
        __prov_app_info_apply();
        for (size_t i = 0; i < PROV_MAX_ENDPOINTS; i++) {
            if (snapshot[i] == NULL) {
                continue;
            }
            if (__prov_backend_endpoint_create(snapshot[i]) != OSAL_ERR_OK) {
                OSAL_LOGE(TAG, "Failed to create the %s endpoint.", snapshot[i]->name);
            }
        }
    } else if (event_id == __event_loop_info.event_ids.prov_start) {
        for (size_t i = 0; i < PROV_MAX_ENDPOINTS; i++) {
            if (snapshot[i] == NULL) {
                continue;
            }
            if (__prov_backend_endpoint_register(snapshot[i]) != OSAL_ERR_OK) {
                OSAL_LOGE(TAG, "Failed to register the %s endpoint.", snapshot[i]->name);
            }
        }
    } else if (event_id == __event_loop_info.event_ids.prov_end) {
        for (size_t i = 0; i < PROV_MAX_ENDPOINTS; i++) {
            if (snapshot[i] != NULL && snapshot[i]->on_prov_end != NULL) {
                snapshot[i]->on_prov_end(snapshot[i]->priv_data);
            }
        }
    }
}

static osal_err_t __prov_events_register(void)
{
    if (osal_event_handler_register(__event_loop_info.event_base, __event_loop_info.event_ids.prov_init,
                                    __prov_event_handler, NULL) != OSAL_ERR_OK ||
            osal_event_handler_register(__event_loop_info.event_base, __event_loop_info.event_ids.prov_start,
                                        __prov_event_handler, NULL) != OSAL_ERR_OK ||
            osal_event_handler_register(__event_loop_info.event_base, __event_loop_info.event_ids.prov_end,
                                        __prov_event_handler, NULL) != OSAL_ERR_OK) {
        __prov_events_unregister();
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}

static void __prov_events_unregister(void)
{
    osal_event_handler_unregister(__event_loop_info.event_base, __event_loop_info.event_ids.prov_init,
                                  __prov_event_handler);
    osal_event_handler_unregister(__event_loop_info.event_base, __event_loop_info.event_ids.prov_start,
                                  __prov_event_handler);
    osal_event_handler_unregister(__event_loop_info.event_base, __event_loop_info.event_ids.prov_end,
                                  __prov_event_handler);
}

/**
 * @brief Hold the provisioning event registrations. Idempotent.
 */
static osal_err_t __prov_events_acquire(void)
{
    if (__prov_event_loop_init() != OSAL_ERR_OK) {
        return OSAL_ERR_FAIL;
    }
    if (!events_registered) {
        if (__prov_events_register() != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to register for the provisioning events.");
            return OSAL_ERR_FAIL;
        }
        events_registered = true;
    }
    return OSAL_ERR_OK;
}

/**
 * @brief Drop the provisioning event registrations once nothing is left to serve.
 */
static void __prov_events_release_if_empty(void)
{
    if (events_registered &&
            __prov_slots_empty(endpoints, PROV_MAX_ENDPOINTS) &&
            __prov_slots_empty(capabilities, PROV_MAX_CAPABILITIES)) {
        __prov_events_unregister();
        events_registered = false;
    }
}

/**
 * @brief Put an entry in the first free slot. The same pointer keeps its slot.
 */
static osal_err_t __prov_slot_add(const void *volatile *slots, size_t total_slots, const void *entry)
{
    size_t free_slot = total_slots;
    for (size_t i = 0; i < total_slots; i++) {
        const void *slot = slots[i];
        if (slot == entry) {
            return OSAL_ERR_OK;
        }
        if (slot == NULL && free_slot == total_slots) {
            free_slot = i;
        }
    }
    if (free_slot == total_slots) {
        return OSAL_ERR_NO_MEM;
    }
    slots[free_slot] = entry;
    return OSAL_ERR_OK;
}

/**
 * @brief Free the slot that holds an entry. Other slots keep their positions.
 */
static osal_err_t __prov_slot_del(const void *volatile *slots, size_t total_slots, const void *entry)
{
    for (size_t i = 0; i < total_slots; i++) {
        if (slots[i] == entry) {
            slots[i] = NULL;
            return OSAL_ERR_OK;
        }
    }
    return OSAL_ERR_NOT_FOUND;
}

static bool __prov_slots_empty(const void *volatile *slots, size_t total_slots)
{
    for (size_t i = 0; i < total_slots; i++) {
        if (slots[i] != NULL) {
            return false;
        }
    }
    return true;
}

/* Public function definitions *******************************************************/

const prov_endpoint_t *__prov_endpoint_get(size_t slot)
{
    return slot < PROV_MAX_ENDPOINTS ? (const prov_endpoint_t *)endpoints[slot] : NULL;
}

size_t __prov_app_info_collect(prov_app_label_t label, const char **names, size_t max_names)
{
    if (names == NULL) {
        return 0;
    }

    size_t total = 0;
    for (size_t i = 0; i < PROV_MAX_CAPABILITIES && total < max_names; i++) {
        const prov_capability_t *capability = (const prov_capability_t *)capabilities[i];
        if (capability != NULL && capability->label == label) {
            names[total++] = capability->capability;
        }
    }
    return total;
}

osal_err_t prov_add_endpoint(const prov_endpoint_t *endpoint)
{
    if (endpoint == NULL || endpoint->name == NULL || endpoint->handler == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (__prov_events_acquire() != OSAL_ERR_OK) {
        return OSAL_ERR_FAIL;
    }

    osal_err_t err = __prov_slot_add(endpoints, PROV_MAX_ENDPOINTS, endpoint);
    if (err == OSAL_ERR_NO_MEM) {
        OSAL_LOGE(TAG, "The endpoint registry is full; raise PROV_MAX_ENDPOINTS.");
    }
    __prov_events_release_if_empty();
    return err;
}

osal_err_t prov_del_endpoint(const prov_endpoint_t *endpoint)
{
    if (endpoint == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    /* Already gone is the state the caller asks for. */
    __prov_slot_del(endpoints, PROV_MAX_ENDPOINTS, endpoint);
    __prov_events_release_if_empty();
    return OSAL_ERR_OK;
}

osal_err_t prov_add_capability(const prov_capability_t *capability)
{
    if (capability == NULL || capability->capability == NULL ||
            capability->label <= PROV_APP_LABEL_INVALID || capability->label >= PROV_APP_LABEL_MAX) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (__prov_events_acquire() != OSAL_ERR_OK) {
        return OSAL_ERR_FAIL;
    }

    osal_err_t err = __prov_slot_add(capabilities, PROV_MAX_CAPABILITIES, capability);
    if (err == OSAL_ERR_NO_MEM) {
        OSAL_LOGE(TAG, "The capability registry is full; raise PROV_MAX_CAPABILITIES.");
    }
    __prov_events_release_if_empty();
    return err;
}

osal_err_t prov_del_capability(const prov_capability_t *capability)
{
    if (capability == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    __prov_slot_del(capabilities, PROV_MAX_CAPABILITIES, capability);
    __prov_events_release_if_empty();
    return OSAL_ERR_OK;
}
