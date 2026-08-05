/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bridge_events.c
 * @brief Bridge control-plane events.
 *
 * Outbound: ``addChild`` / ``removeChild`` published on the bridge
 * ``to_cloud`` topic with a generated ``request_id`` correlator, after
 * registering a ::__pending_entry_t in the pending table.
 *
 * Inbound: ``bridgeAck`` dispatched here, correlated by ``request_id``,
 * with kind-specific success / failure handling. Failure paths fire the
 * public ::RMAKER_EVENT_BRIDGE_CHILD_ADD_FAILED /
 * ::RMAKER_EVENT_BRIDGE_CHILD_REMOVE_FAILED events.
 */

#include "esp_rmaker_bridge.h"
#include "esp_rmaker_event_loop.h"
#include "bridge/bridge_internal.h"

#include "constants/identity.h"
#include "constants/bridge.h"

#include "network/common.h"
#include "network/cloud/manager.h"
#include "network/cloud/events.h"
#include "network/mqtt_topics.h"
#include "network/mqtt_channels.h"
#include "esp_rmaker_mqtt_channels.h"

#include "esp_rmaker_credentials.h"

#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_event_loop.h"
#include "osal_random.h"
#include "osal_semaphore.h"
#include "osal_time.h"

#include "sdkconfig.h"

#include "json_generator.h"
#include "json_parser.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "rmng_br_events";

/* Cross-module helpers from bridge.c */
extern void bridge_internal_lock(void);
extern void bridge_internal_unlock(void);
extern void bridge_handle_ack_success_add(const char *child_thing_name);
extern void bridge_handle_ack_failure_add(const char *bridge_local_id, const char *child_suffix, const char *error);
extern void bridge_handle_ack_success_remove_by_local_id(const char *bridge_local_id);
extern void bridge_handle_ack_failure_remove_by_local_id(const char *bridge_local_id, const char *error);

/* Pending request table *****************************************************
 *
 * Each outbound addChild / removeChild registers an entry keyed by the
 * generated ``request_id`` before publish. The inbound bridgeAck
 * dispatcher takes the entry by request_id and routes by kind. This
 * removes the dependency on payload-shape heuristics (``child_thing_name``
 * present vs absent) and slot-state guessing that the previous design
 * used, and lets failure responses surface as
 * ::RMAKER_EVENT_BRIDGE_CHILD_ADD_FAILED /
 * ::RMAKER_EVENT_BRIDGE_CHILD_REMOVE_FAILED with structured payloads.
 *
 * Sized at compile time to ``1 + CONFIG_RMNG_BRIDGE_MAX_CHILDREN``:
 * worst case is one inflight op per slot.
 *
 * Entries older than ::__PEND_EXPIRY_S are swept on the next register
 * call to prevent permanent occupation when an ack never arrives
 * (cloud bug, network drop after publish-ok, etc.).
 */

typedef enum {
    __PEND_KIND_ADD,
    __PEND_KIND_REMOVE,
} __pend_kind_t;

typedef struct {
    bool used;
    __pend_kind_t kind;
    char request_id[RMAKER_BRIDGE_REQUEST_ID_BUF_SIZE];
    char bridge_local_id[RMAKER_BRIDGE_LOCAL_ID_BUF_SIZE];
    union {
        struct {
            char child_suffix[RMAKER_BRIDGE_CHILD_SUFFIX_BUF_SIZE];
        } add;
        struct {
            char child_thing_name[RMAKER_THING_NAME_BUFFER_SIZE];
        } remove;
    } u;
    time_t inserted_s;
} __pending_entry_t;

#ifdef CONFIG_RMNG_BRIDGE_MAX_CHILDREN
#define __PEND_MAX (1 + CONFIG_RMNG_BRIDGE_MAX_CHILDREN)
#else
#define __PEND_MAX 4
#endif

#define __PEND_EXPIRY_S 60

/* Allocated once in bridge_to_cloud_init (prefers SPIRAM) and never freed -
 * lives for the process lifetime. At large CONFIG_RMNG_BRIDGE_MAX_CHILDREN
 * each ~340-byte entry x __PEND_MAX makes this a sizeable static-DRAM
 * consumer; heap-allocating keeps it out of internal RAM. Allocated
 * *before* the mutex, so ``__pending_mutex != NULL`` implies
 * ``__pending != NULL`` - and every accessor already bails when the mutex
 * is NULL, so no further guards are needed. */
static __pending_entry_t *__pending = NULL;
static osal_semaphore_handle_t __pending_mutex = NULL;

static inline void __plock(void)
{
    if (__pending_mutex) {
        osal_semaphore_take(__pending_mutex, OSAL_MAX_DELAY);
    }
}

static inline void __punlock(void)
{
    if (__pending_mutex) {
        osal_semaphore_give(__pending_mutex);
    }
}

static time_t __now_s(void)
{
    return osal_get_time(NULL);
}

static void __pend_sweep_expired_locked(time_t now)
{
    for (size_t i = 0; i < __PEND_MAX; i++) {
        if (__pending[i].used && (now - __pending[i].inserted_s) > __PEND_EXPIRY_S) {
            OSAL_LOGW(TAG, "Expiring pending bridge request request_id=%s (no ack within %ds)",
                      __pending[i].request_id, __PEND_EXPIRY_S);
            __pending[i].used = false;
        }
    }
}

static esp_rmaker_error_t __pend_register_add(const char *request_id, const char *bridge_local_id, const char *child_suffix)
{
    if (!__pending_mutex || !request_id || !bridge_local_id || !child_suffix) {
        return ESP_RMAKER_INVALID_ARG;
    }
    __plock();
    __pend_sweep_expired_locked(__now_s());
    int free_idx = -1;
    for (size_t i = 0; i < __PEND_MAX; i++) {
        if (!__pending[i].used) {
            free_idx = (int)i;
            break;
        }
    }
    if (free_idx < 0) {
        __punlock();
        OSAL_LOGE(TAG, "Pending bridge-request table full; cannot register request_id=%s", request_id);
        return ESP_RMAKER_NO_MEM;
    }
    __pending_entry_t *e = &__pending[free_idx];
    memset(e, 0, sizeof(*e));
    e->used = true;
    e->kind = __PEND_KIND_ADD;
    snprintf(e->request_id, sizeof(e->request_id), "%s", request_id);
    snprintf(e->bridge_local_id, sizeof(e->bridge_local_id), "%s", bridge_local_id);
    snprintf(e->u.add.child_suffix, sizeof(e->u.add.child_suffix), "%s", child_suffix);
    e->inserted_s = __now_s();
    __punlock();
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __pend_register_remove(const char *request_id, const char *bridge_local_id, const char *child_thing_name)
{
    if (!__pending_mutex || !request_id || !bridge_local_id || !child_thing_name) {
        return ESP_RMAKER_INVALID_ARG;
    }
    __plock();
    __pend_sweep_expired_locked(__now_s());
    int free_idx = -1;
    for (size_t i = 0; i < __PEND_MAX; i++) {
        if (!__pending[i].used) {
            free_idx = (int)i;
            break;
        }
    }
    if (free_idx < 0) {
        __punlock();
        OSAL_LOGE(TAG, "Pending bridge-request table full; cannot register request_id=%s", request_id);
        return ESP_RMAKER_NO_MEM;
    }
    __pending_entry_t *e = &__pending[free_idx];
    memset(e, 0, sizeof(*e));
    e->used = true;
    e->kind = __PEND_KIND_REMOVE;
    snprintf(e->request_id, sizeof(e->request_id), "%s", request_id);
    snprintf(e->bridge_local_id, sizeof(e->bridge_local_id), "%s", bridge_local_id);
    snprintf(e->u.remove.child_thing_name, sizeof(e->u.remove.child_thing_name), "%s", child_thing_name);
    e->inserted_s = __now_s();
    __punlock();
    return ESP_RMAKER_OK;
}

static bool __pend_take_by_request_id(const char *request_id, __pending_entry_t *out)
{
    if (!__pending_mutex || !request_id || !out) {
        return false;
    }
    __plock();
    for (size_t i = 0; i < __PEND_MAX; i++) {
        if (__pending[i].used && strcmp(__pending[i].request_id, request_id) == 0) {
            *out = __pending[i];
            __pending[i].used = false;
            __punlock();
            return true;
        }
    }
    __punlock();
    return false;
}

static void __pend_drop_by_request_id(const char *request_id)
{
    if (!__pending_mutex || !request_id) {
        return;
    }
    __plock();
    for (size_t i = 0; i < __PEND_MAX; i++) {
        if (__pending[i].used && strcmp(__pending[i].request_id, request_id) == 0) {
            __pending[i].used = false;
            break;
        }
    }
    __punlock();
}

/* Helpers *******************************************************************/

static void __make_short_request_id(char out[RMAKER_BRIDGE_REQUEST_ID_BUF_SIZE])
{
    uint8_t bytes[RMAKER_BRIDGE_REQUEST_ID_LEN / 2];
    osal_random_fill(bytes, sizeof(bytes));
    static const char *hex = "0123456789abcdef";
    for (size_t b = 0; b < sizeof(bytes); b++) {
        out[2 * b]     = hex[(bytes[b] >> 4) & 0xF];
        out[2 * b + 1] = hex[bytes[b] & 0xF];
    }
    out[RMAKER_BRIDGE_REQUEST_ID_LEN] = '\0';
}

static char *__compute_child_thing_name(const char *suffix)
{
    char *self = NULL;
    if (esp_rmaker_credentials_get_thing_name(&self) != ESP_RMAKER_OK || !self) {
        return NULL;
    }
    size_t need = strlen(self) + 2 /* "--" */ + strlen(suffix) + 1;
    char *out = (char *)OSAL_CALLOC_EXTRAM(need, sizeof(char));
    if (out) {
        snprintf(out, need, "%s--%s", self, suffix);
    }
    free(self);
    return out;
}

static char *__build_add_child_payload(const char *request_id, const char *suffix, const char *bridge_local_id)
{
    char buf[256];
    json_gen_str_t j;
    json_gen_str_start(&j, buf, sizeof(buf), NULL, NULL);
    json_gen_start_object(&j);
    json_gen_obj_set_string(&j, "request_id", (char *)request_id);
    json_gen_obj_set_string(&j, "child_suffix", (char *)suffix);
    json_gen_obj_set_string(&j, "child_local_id", (char *)bridge_local_id);
    json_gen_end_object(&j);
    (void)json_gen_str_end(&j);
    return OSAL_STRDUP_EXTRAM(buf);
}

static char *__build_remove_child_payload(const char *request_id, const char *child_thing_name)
{
    char buf[256];
    json_gen_str_t j;
    json_gen_str_start(&j, buf, sizeof(buf), NULL, NULL);
    json_gen_start_object(&j);
    json_gen_obj_set_string(&j, "request_id", (char *)request_id);
    json_gen_obj_set_string(&j, "child_node_id", (char *)child_thing_name);
    json_gen_end_object(&j);
    (void)json_gen_str_end(&j);
    return OSAL_STRDUP_EXTRAM(buf);
}

/* Public hooks to bridge.c **************************************************/

esp_rmaker_error_t bridge_to_cloud_init(void)
{
    if (!__pending_mutex) {
        /* Allocate the table before the mutex so a live mutex implies a
         * live table. */
        __pending = (__pending_entry_t *)OSAL_CALLOC_EXTRAM(
                        __PEND_MAX, sizeof(__pending_entry_t));
        if (!__pending) {
            OSAL_LOGE(TAG, "Failed to allocate pending-request table (%d entries)",
                      __PEND_MAX);
            return ESP_RMAKER_NO_MEM;
        }
        __pending_mutex = osal_semaphore_create_mutex();
        if (!__pending_mutex) {
            OSAL_LOGE(TAG, "Failed to create pending-request mutex");
            free(__pending);
            __pending = NULL;
            return ESP_RMAKER_FAIL;
        }
    }
    OSAL_LOGI(TAG, "bridge_events init OK");
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t bridge_to_cloud_publish_add_child(struct esp_rmaker_bridge_child *child)
{
    if (!child) {
        return ESP_RMAKER_INVALID_ARG;
    }

    bridge_internal_lock();
    char *suffix = child->child_suffix ? OSAL_STRDUP_EXTRAM(child->child_suffix) : NULL;
    char *local_id = child->bridge_local_id ? OSAL_STRDUP_EXTRAM(child->bridge_local_id) : NULL;
    bridge_internal_unlock();
    if (!suffix || !local_id) {
        free(suffix); free(local_id);
        return ESP_RMAKER_NO_MEM;
    }

    /* Pre-compute the child's thing name (<self>--<suffix>) so it is
     * already in the registry when the bridgeAck arrives keyed by the
     * same name. */
    char *child_thing_name = __compute_child_thing_name(suffix);
    if (!child_thing_name) {
        OSAL_LOGE(TAG, "Cannot compute child thing name (self credentials unavailable)");
        free(suffix); free(local_id);
        return ESP_RMAKER_INVALID_STATE;
    }
    bridge_internal_lock();
    free(child->thing_name);
    child->thing_name = child_thing_name;
    bridge_internal_unlock();

    char request_id[RMAKER_BRIDGE_REQUEST_ID_BUF_SIZE];
    __make_short_request_id(request_id);

    /* Register pending entry BEFORE publish so the inbound ack can never
     * arrive before the entry exists. */
    esp_rmaker_error_t reg_err = __pend_register_add(request_id, local_id, suffix);
    if (reg_err != ESP_RMAKER_OK) {
        free(suffix); free(local_id);
        return reg_err;
    }

    char *payload = __build_add_child_payload(request_id, suffix, local_id);
    free(suffix); free(local_id);
    if (!payload) {
        __pend_drop_by_request_id(request_id);
        return ESP_RMAKER_NO_MEM;
    }

    esp_rmaker_cloud_event_t event;
    esp_rmaker_cloud_event_addChild(&event, payload);
    esp_rmaker_error_t err = esp_rmaker_cloud_manager_send_bridge(&event, 1, MQTT_CHANNEL_SUB_CLOUD_MANAGER_BRIDGE_ADD_CHILD);

    if (err != ESP_RMAKER_OK) {
        __pend_drop_by_request_id(request_id);
    }

    free(payload);
    return err;
}

esp_rmaker_error_t bridge_to_cloud_publish_remove_child(struct esp_rmaker_bridge_child *child)
{
    if (!child) {
        return ESP_RMAKER_INVALID_ARG;
    }

    bridge_internal_lock();
    char *thing_name = child->thing_name ? OSAL_STRDUP_EXTRAM(child->thing_name) : NULL;
    char *local_id = child->bridge_local_id ? OSAL_STRDUP_EXTRAM(child->bridge_local_id) : NULL;
    bridge_internal_unlock();
    if (!thing_name || !local_id) {
        free(thing_name); free(local_id);
        return ESP_RMAKER_INVALID_STATE;
    }

    char request_id[RMAKER_BRIDGE_REQUEST_ID_BUF_SIZE];
    __make_short_request_id(request_id);

    esp_rmaker_error_t reg_err = __pend_register_remove(request_id, local_id, thing_name);
    if (reg_err != ESP_RMAKER_OK) {
        free(thing_name); free(local_id);
        return reg_err;
    }

    char *payload = __build_remove_child_payload(request_id, thing_name);
    if (!payload) {
        __pend_drop_by_request_id(request_id);
        free(thing_name); free(local_id);
        return ESP_RMAKER_NO_MEM;
    }

    esp_rmaker_cloud_event_t event;
    esp_rmaker_cloud_event_removeChild(&event, payload);
    esp_rmaker_error_t err = esp_rmaker_cloud_manager_send_bridge(&event, 1, MQTT_CHANNEL_SUB_CLOUD_MANAGER_BRIDGE_REMOVE_CHILD);
    free(payload);

    if (err == ESP_RMAKER_OK) {
        /* Optimistic: drop the registry entry and fire the REMOVED
         * event immediately. The cloud's bridgeAck is correlated
         * separately via the pending table by request_id; success is
         * informational, non-success surfaces as
         * ::RMAKER_EVENT_BRIDGE_CHILD_REMOVE_FAILED (the app must
         * reconcile because the local REMOVED was already fired). */
        bridge_handle_ack_success_remove_by_local_id(local_id);
    } else {
        __pend_drop_by_request_id(request_id);
    }
    free(thing_name); free(local_id);
    return err;
}

/* bridgeAck dispatch ********************************************************/

esp_rmaker_error_t bridge_internal_dispatch_from_cloud_event(const char *event_name, jparse_ctx_t *p_jctx)
{
    if (event_name == NULL || p_jctx == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (strcmp(event_name, "bridgeAck") != 0) {
        return ESP_RMAKER_NOT_FOUND;
    }

    char request_id[RMAKER_BRIDGE_REQUEST_ID_BUF_SIZE] = {0};
    if (json_obj_get_string(p_jctx, "request_id", request_id, sizeof(request_id)) != 0) {
        OSAL_LOGW(TAG, "bridgeAck missing request_id; cannot correlate");
        return ESP_RMAKER_OK;
    }

    __pending_entry_t entry;
    if (!__pend_take_by_request_id(request_id, &entry)) {
        /* Most common cause: cloud retry / duplicate ack, expired entry
         * swept already, or a stale ack after restart. Harmless. */
        OSAL_LOGD(TAG, "bridgeAck for unknown request_id=%s; ignoring", request_id);
        return ESP_RMAKER_OK;
    }

    char status[12] = {0};
    json_obj_get_string(p_jctx, "status", status, sizeof(status));
    bool ok = (strcmp(status, "success") == 0);

    char error_buf[64] = {0};
    if (!ok) {
        json_obj_get_string(p_jctx, "error", error_buf, sizeof(error_buf));
    }

    switch (entry.kind) {
    case __PEND_KIND_ADD: {
        if (!ok) {
            OSAL_LOGW(TAG, "addChild failed (request_id=%s local_id=%s suffix=%s error='%s')",
                      request_id, entry.bridge_local_id, entry.u.add.child_suffix, error_buf);
            bridge_handle_ack_failure_add(entry.bridge_local_id, entry.u.add.child_suffix, error_buf);
            return ESP_RMAKER_OK;
        }
        char child_thing_name[RMAKER_THING_NAME_BUFFER_SIZE] = {0};
        if (json_obj_get_string(p_jctx, "child_node_id", child_thing_name, sizeof(child_thing_name)) != 0) {
            OSAL_LOGW(TAG, "addChild bridgeAck success missing child_node_id (request_id=%s)", request_id);
            bridge_handle_ack_failure_add(entry.bridge_local_id, entry.u.add.child_suffix, "missing child_node_id");
            return ESP_RMAKER_OK;
        }
        bridge_handle_ack_success_add(child_thing_name);
        break;
    }
    case __PEND_KIND_REMOVE:
        if (!ok) {
            OSAL_LOGW(TAG, "removeChild failed (request_id=%s local_id=%s thing=%s error='%s'); local teardown already fired",
                      request_id, entry.bridge_local_id, entry.u.remove.child_thing_name, error_buf);
            bridge_handle_ack_failure_remove_by_local_id(entry.bridge_local_id, error_buf);
        } else {
            OSAL_LOGI(TAG, "removeChild ack (request_id=%s thing=%s)",
                      request_id, entry.u.remove.child_thing_name);
        }
        break;
    }
    return ESP_RMAKER_OK;
}
