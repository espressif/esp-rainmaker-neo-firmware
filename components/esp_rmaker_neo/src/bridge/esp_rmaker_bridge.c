/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_bridge.c
 * @brief Bridge subsystem entry points and child registry.
 */

/* Public + internal headers */
#include "esp_rmaker_bridge.h"
#include "bridge/bridge_internal.h"
#include "bridge/bridge_child_nvs.h"
#include "bridge/bridge_child_triggers_nvs.h"
#include "node_config_pending.h"
#include "core_internal.h"
#include "node_internal.h"
#include "services/automation.h"
#include "services/schedules.h"

#include "network/mqtt_topics.h"
#include "network/mqtt_channels.h"
#include "network/state_changes.h"
#include "network/cloud/events.h"
#include "network/cloud/manager.h"

#include "esp_rmaker_common_events.h"
#include "esp_rmaker_work_queue.h"
#include "osal_event_loop.h"

/* Crypto + hex utilities for NVS key derivation. */
#include "util/esp_rmaker_crypto.h"
#include "util/esp_rmaker_convert_hex.h"

/* Platform */
#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_semaphore.h"

#include "sdkconfig.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "rmng_br_core";

/* Slot pool ******************************************************************/

/**
 * @brief One slot in the bridge child pool.
 *
 * The pool is sized at compile time by ``CONFIG_RMNG_BRIDGE_MAX_CHILDREN``.
 * Each slot statically owns both the child handle (so its address is
 * stable forever) and the topic ctx that downstream subsystems hold
 * by pointer. The ``in_use`` flag is the slot's lifecycle bit and is
 * aliased into ``ctx.valid`` so consumers can detect teardown without
 * any explicit purge step.
 */
struct __bridge_child_slot {
    bool in_use;                          /**< Set while the slot is live. */
    struct esp_rmaker_bridge_child child; /**< Stable child handle address. */
    _esp_rmaker_node_t node;              /**< Per-child node (info/attrs/
                                               tags/devices/topic_ctx).
                                               ``node.topic_ctx.valid`` is
                                               aliased to ``in_use``. */
};

/* Allocated once at ``bridge_internal_init`` (prefers SPIRAM via
 * OSAL_CALLOC_EXTRAM) and freed at ``bridge_internal_deinit``. Held
 * as a heap pointer rather than a static array because at large
 * CONFIG_RMNG_BRIDGE_MAX_CHILDREN the per-slot embedded node makes this
 * the dominant static-DRAM consumer, overflowing internal RAM. The array
 * is allocated exactly once for the bridge's lifetime, so slot addresses
 * (``&slot.child`` / ``&node.topic_ctx``) stay stable for as long as
 * downstream holds them - same guarantee the static array gave. */
static struct __bridge_child_slot *__child_pool = NULL;

/* Topic-ctx ops *************************************************************/

static int __child_write_thing_name(void *priv, char *buf, size_t buf_size)
{
    struct esp_rmaker_bridge_child *c = (struct esp_rmaker_bridge_child *)priv;
    if (!c || !c->thing_name) {
        return -1;
    }
    int written = snprintf(buf, buf_size, "%s", c->thing_name);
    return (written > 0 && (size_t)written < buf_size) ? written : -1;
}

static int __child_write_group_info_str(void *priv, char *buf, size_t buf_size)
{
    struct esp_rmaker_bridge_child *c = (struct esp_rmaker_bridge_child *)priv;
    const char *gi = (c && c->group_info_str) ? c->group_info_str : "";
    int written = snprintf(buf, buf_size, "%s", gi);
    return (written >= 0 && (size_t)written < buf_size) ? written : -1;
}

const esp_rmaker_topic_ops_t bridge_child_topic_ops = {
    .write_thing_name = __child_write_thing_name,
    .write_group_info_str = __child_write_group_info_str,
};

/* Module state ***************************************************************/

static bool __initialized = false;

/** Tracks whether a disconnect has occurred since last connect. The
 *  MQTT-connected handler is a no-op on first connect (initial cloud-info
 *  bundles are driven by the commit-devices path); only an actual reconnect
 *  needs to re-fan-out per-child events. */
static bool __has_disconnected = false;

/** Guard for the child pool and per-child mutable fields. */
static osal_semaphore_handle_t __children_mutex = NULL;

#if CONFIG_RMNG_HOST_CTRL
/** Single registered per-child event observer + its private cookie.
 *  Today only the remote-layer bridge handler registers here. */
static bridge_internal_child_event_cb_t __child_event_cb = NULL;
static void *__child_event_cb_priv = NULL;
#endif

/** Resolve the slot containing the given child handle. */
static inline struct __bridge_child_slot *__slot_of(struct esp_rmaker_bridge_child *c)
{
    return (struct __bridge_child_slot *)((char *)c - offsetof(struct __bridge_child_slot, child));
}

static void __lock(void)
{
    if (__children_mutex) {
        osal_semaphore_take(__children_mutex, OSAL_MAX_DELAY);
    }
}

static void __unlock(void)
{
    if (__children_mutex) {
        osal_semaphore_give(__children_mutex);
    }
}

/* Validation helpers *********************************************************/

/* NVS key derivation: SHA-256(bridge_local_id), first 7 bytes,
 * hex-encoded -> 14 chars + NUL. Stable across reboots; collision risk
 * with SHA-256 truncation to 56 bits is negligible for the bounded
 * child pool. */
#define __NVS_KEY_RAW_BYTES 7

esp_rmaker_error_t bridge_internal_compute_nvs_key(const char *bridge_local_id, char *out, size_t out_size)
{
    if (!bridge_local_id || bridge_local_id[0] == '\0' || !out || out_size < RMAKER_NVS_KEY_LEN_MAX + 1) {
        return ESP_RMAKER_INVALID_ARG;
    }
    uint8_t hash[RMAKER_CRYPTO_SHA256_HASH_LEN];
    esp_rmaker_error_t err = esp_rmaker_crypto_gen_sha256(
                                 (const uint8_t *)bridge_local_id, strlen(bridge_local_id), hash);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    /* 7 bytes -> 14 hex chars + NUL fits in 15-char NVS key cap. */
    return esp_rmaker_convert_bytes_to_hex(hash, __NVS_KEY_RAW_BYTES, out, out_size);
}

bool bridge_internal_valid_suffix(const char *s)
{
    if (s == NULL || s[0] == '\0') {
        return false;
    }
    size_t n = strlen(s);
    if (n > 32) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/* Registry ops (lock-held) ***************************************************/

static struct esp_rmaker_bridge_child *__find_by_local_id_locked(const char *bridge_local_id)
{
    for (size_t i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        struct __bridge_child_slot *s = &__child_pool[i];
        if (!s->in_use) {
            continue;
        }
        if (s->child.bridge_local_id && strcmp(s->child.bridge_local_id, bridge_local_id) == 0) {
            return &s->child;
        }
    }
    return NULL;
}

static struct esp_rmaker_bridge_child *__find_by_thing_name_locked(const char *thing_name)
{
    for (size_t i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        struct __bridge_child_slot *s = &__child_pool[i];
        if (!s->in_use) {
            continue;
        }
        if (s->child.thing_name && strcmp(s->child.thing_name, thing_name) == 0) {
            return &s->child;
        }
    }
    return NULL;
}

static struct esp_rmaker_bridge_child *__find_by_nvs_key_locked(const char *nvs_key)
{
    if (!__child_pool) {
        return NULL;
    }
    for (size_t i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        struct __bridge_child_slot *s = &__child_pool[i];
        if (!s->in_use) {
            continue;
        }
        if (strcmp(s->child.nvs_key, nvs_key) == 0) {
            return &s->child;
        }
    }
    return NULL;
}

static struct esp_rmaker_bridge_child *__alloc_child_locked(const char *child_suffix, const char *bridge_local_id)
{
    struct __bridge_child_slot *slot = NULL;
    for (size_t i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        if (!__child_pool[i].in_use) {
            slot = &__child_pool[i];
            break;
        }
    }
    if (!slot) {
        OSAL_LOGE(TAG, "Bridge child pool exhausted (%d slots)", CONFIG_RMNG_BRIDGE_MAX_CHILDREN);
        return NULL;
    }

    struct esp_rmaker_bridge_child *c = &slot->child;
    memset(c, 0, sizeof(*c));
    c->child_suffix = OSAL_STRDUP_EXTRAM(child_suffix);
    c->bridge_local_id = OSAL_STRDUP_EXTRAM(bridge_local_id);
    c->group_info_str = OSAL_STRDUP_EXTRAM("");
    if (!c->child_suffix || !c->bridge_local_id || !c->group_info_str) {
        free(c->child_suffix);
        free(c->bridge_local_id);
        free(c->group_info_str);
        memset(c, 0, sizeof(*c));
        return NULL;
    }
    c->state = RMNG_BRIDGE_CHILD_STATE_PENDING_ADD;
    c->sched_progress.pending_version = -1;
    c->trigger_progress.pending_version = -1;

    /* Derive the NVS key once now; same id -> same key across reboots. */
    if (bridge_internal_compute_nvs_key(bridge_local_id, c->nvs_key, sizeof(c->nvs_key)) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to compute NVS key for child '%s'", bridge_local_id);
        free(c->child_suffix);
        free(c->bridge_local_id);
        free(c->group_info_str);
        memset(c, 0, sizeof(*c));
        return NULL;
    }

    _esp_rmaker_node_init(&slot->node);
    slot->node.topic_ctx = (esp_rmaker_topic_ctx_t) {
        .ops = &bridge_child_topic_ops,
        .priv = c,
        .valid = &slot->in_use,
    };
    /* Publish the slot only after every field is populated; ``in_use``
     * is the validity flag downstream consumers read. */
    slot->in_use = true;
    return c;
}

static void __free_child_locked(struct esp_rmaker_bridge_child *target)
{
    if (!target) {
        return;
    }
    struct __bridge_child_slot *slot = __slot_of(target);
    if (!slot->in_use) {
        return;
    }
    /* Invalidate first so any concurrent state/timeseries publish sees
     * the ctx as gone before we tear the priv strings down. */
    slot->in_use = false;
    free(target->thing_name);
    free(target->bridge_local_id);
    free(target->child_suffix);
    free(target->group_info_str);
    memset(target, 0, sizeof(*target));
    /* Tear down per-child node contents. Leave node.topic_ctx populated;
     * validity flag (in_use, just cleared) governs use. */
    _esp_rmaker_node_reset(&slot->node);
}

/* bridge_to_cloud.c hooks (forward decls; defined in bridge_to_cloud.c) ******/

esp_rmaker_error_t bridge_to_cloud_publish_add_child(struct esp_rmaker_bridge_child *child);
esp_rmaker_error_t bridge_to_cloud_publish_remove_child(struct esp_rmaker_bridge_child *child);
esp_rmaker_error_t bridge_to_cloud_init(void);

/* bridge_subscriber.c hooks (forward decls) **********************************/

esp_rmaker_error_t bridge_subscriber_subscribe_all(uint8_t *out_issued);
esp_rmaker_error_t bridge_subscriber_unsubscribe_all(uint8_t *out_issued);

/* Internal contract impl ****************************************************/

esp_rmaker_error_t bridge_internal_subscribe(uint8_t *out_issued)
{
    return bridge_subscriber_subscribe_all(out_issued);
}

esp_rmaker_error_t bridge_internal_unsubscribe(uint8_t *out_issued)
{
    return bridge_subscriber_unsubscribe_all(out_issued);
}

void bridge_internal_for_each_ready_child(bridge_internal_child_visitor_t visitor, void *priv)
{
    if (!visitor) {
        return;
    }
    __lock();
    for (size_t i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        struct __bridge_child_slot *s = &__child_pool[i];
        if (!s->in_use) {
            continue;
        }
        if (s->child.state == RMNG_BRIDGE_CHILD_STATE_READY) {
            (void)visitor(&s->child, priv);
        }
    }
    __unlock();
}

esp_rmaker_bridge_child_handle_t bridge_internal_find_by_thing_name(const char *thing_name)
{
    if (!thing_name) {
        return NULL;
    }
    __lock();
    struct esp_rmaker_bridge_child *c = __find_by_thing_name_locked(thing_name);
    struct esp_rmaker_bridge_child *hit = (c && c->state == RMNG_BRIDGE_CHILD_STATE_READY) ? c : NULL;
    __unlock();
    return hit;
}

esp_rmaker_bridge_child_handle_t bridge_internal_find_by_local_id(const char *bridge_local_id)
{
    if (!bridge_local_id) {
        return NULL;
    }
    __lock();
    struct esp_rmaker_bridge_child *c = __find_by_local_id_locked(bridge_local_id);
    __unlock();
    return c;
}

esp_rmaker_bridge_child_handle_t bridge_internal_find_by_nvs_key(const char *nvs_key)
{
    if (!nvs_key || !nvs_key[0]) {
        return NULL;
    }
    __lock();
    struct esp_rmaker_bridge_child *c = __find_by_nvs_key_locked(nvs_key);
    __unlock();
    return c;
}

esp_rmaker_node_t *bridge_internal_child_node(esp_rmaker_bridge_child_handle_t child)
{
    if (!child) {
        return NULL;
    }
    return (esp_rmaker_node_t *)&__slot_of(child)->node;
}

esp_rmaker_bridge_child_handle_t bridge_internal_child_from_node(const esp_rmaker_node_t *node)
{
    if (!node || !__child_pool) {
        return NULL;
    }
    /* Slot-embedded nodes live inside __child_pool; reject anything outside. */
    for (size_t i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        if ((const _esp_rmaker_node_t *)node == &__child_pool[i].node) {
            return __child_pool[i].in_use ? &__child_pool[i].child : NULL;
        }
    }
    return NULL;
}

esp_rmaker_bridge_child_handle_t bridge_internal_child_from_ctx(const esp_rmaker_topic_ctx_t *ctx)
{
    if (!ctx || ctx == &esp_rmaker_topic_ctx_self || !__child_pool) {
        return NULL;
    }
    /* Slot ctx pointers now live inside each slot's embedded node. */
    for (size_t i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        if (&__child_pool[i].node.topic_ctx == ctx) {
            return __child_pool[i].in_use ? &__child_pool[i].child : NULL;
        }
    }
    return NULL;
}

void bridge_internal_for_each_ready_node(esp_rmaker_node_visitor_t visitor, void *priv)
{
    if (!visitor) {
        return;
    }
    __lock();
    for (size_t i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        struct __bridge_child_slot *s = &__child_pool[i];
        if (!s->in_use) {
            continue;
        }
        if (s->child.state == RMNG_BRIDGE_CHILD_STATE_READY) {
            (void)visitor((esp_rmaker_node_t *)&s->node, priv);
        }
    }
    __unlock();
}

const char *bridge_internal_child_local_id(esp_rmaker_bridge_child_handle_t child)
{
    return child ? child->bridge_local_id : NULL;
}

const char *bridge_internal_child_group_info_str(esp_rmaker_bridge_child_handle_t child)
{
    return child ? child->group_info_str : NULL;
}

esp_rmaker_error_t bridge_internal_child_set_group_info_str(esp_rmaker_bridge_child_handle_t child, const char *new_str)
{
    if (!child) {
        return ESP_RMAKER_INVALID_ARG;
    }
    char *dup = OSAL_STRDUP_EXTRAM(new_str ? new_str : "");
    if (!dup) {
        return ESP_RMAKER_NO_MEM;
    }
    __lock();
    free(child->group_info_str);
    child->group_info_str = dup;
    __unlock();
    return ESP_RMAKER_OK;
}

const char *bridge_internal_child_nvs_key(esp_rmaker_bridge_child_handle_t child)
{
    return child ? child->nvs_key : NULL;
}

esp_rmaker_bridge_version_progress_t *bridge_internal_child_version_progress(
    esp_rmaker_bridge_child_handle_t child, bridge_version_kind_t kind)
{
    if (!child) {
        return NULL;
    }
    return (kind == BRIDGE_VERSION_KIND_SCHED) ? &child->sched_progress : &child->trigger_progress;
}

bool bridge_internal_child_group_setup_done(esp_rmaker_bridge_child_handle_t child)
{
    if (!child) {
        return false;
    }
    __lock();
    bool v = child->group_setup_done;
    __unlock();
    return v;
}

bool bridge_internal_child_devices_committed(esp_rmaker_bridge_child_handle_t child)
{
    if (!child) {
        return false;
    }
    __lock();
    bool v = child->devices_committed;
    __unlock();
    return v;
}

void bridge_internal_child_set_group_setup_done(esp_rmaker_bridge_child_handle_t child, bool done)
{
    if (!child) {
        return;
    }
    __lock();
    child->group_setup_done = done;
    __unlock();
}

/* Test-only helpers ********************************************************/

esp_rmaker_bridge_child_handle_t bridge_internal_test_seed_child(
    const char *child_suffix, const char *bridge_local_id, const char *thing_name)
{
    if (!child_suffix || !bridge_local_id || !thing_name) {
        return NULL;
    }
    if (!bridge_internal_valid_suffix(child_suffix)) {
        return NULL;
    }
    if (!__initialized) {
        if (bridge_internal_init() != ESP_RMAKER_OK) {
            return NULL;
        }
    }
    __lock();
    if (__find_by_local_id_locked(bridge_local_id) != NULL) {
        __unlock();
        return NULL;
    }
    struct esp_rmaker_bridge_child *c = __alloc_child_locked(child_suffix, bridge_local_id);
    if (!c) {
        __unlock();
        return NULL;
    }
    /* Promote straight to READY; the public path waits for bridgeAck. */
    c->thing_name = OSAL_STRDUP_EXTRAM(thing_name);
    if (!c->thing_name) {
        __free_child_locked(c);
        __unlock();
        return NULL;
    }
    c->state = RMNG_BRIDGE_CHILD_STATE_READY;
    __unlock();
    return c;
}


/* Per-child ON-CONNECT pathway *********************************************/

/**
 * @brief Send the per-child cloud-event 3-bundle on the child's ctx.
 *
 * Mirrors self's ::esp_rmaker_get_cloud_info but child-scoped: only the
 * three events whose responses are per-Thing (getGroupInfo, getSchedVer,
 * getTriggerVer). getAlexaEn / getGVAEn are intentionally omitted -
 * those are group-global, and the bridge's own response applies to all
 * children in the group.
 */
static void bridge_internal_child_on_connect_task(void *child_arg)
{
    esp_rmaker_bridge_child_handle_t child = (esp_rmaker_bridge_child_handle_t)child_arg;
    if (!child) {
        return;
    }
    const esp_rmaker_topic_ctx_t *ctx = esp_rmaker_node_topic_ctx(bridge_internal_child_node(child));
    if (!ctx) {
        return;
    }

    esp_rmaker_cloud_event_t events[3] = {0};
    esp_rmaker_cloud_event_t *p = events;
    esp_rmaker_error_t err;

    err = esp_rmaker_cloud_event_getGroupInfo(p++);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "child on-connect: getGroupInfo build failed: %d", err);
        return;
    }
    err = esp_rmaker_cloud_event_getSchedVer(p++);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "child on-connect: getSchedVer build failed: %d", err);
        return;
    }
    err = esp_rmaker_cloud_event_getTriggerVer(p);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "child on-connect: getTriggerVer build failed: %d", err);
        return;
    }

    err = esp_rmaker_cloud_manager_send(ctx, events, sizeof(events) / sizeof(events[0]), MQTT_CHANNEL_SUB_CLOUD_MANAGER_BRIDGE_CHILD_CLOUD_INFO);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "child on-connect: cloud_manager_send failed: %d", err);
    }

    /* Rehydrate this child's automation triggers from NVS now that its
     * devices are committed (trigger paths reference child params). A
     * subsequent cloud getTriggerDetails will overwrite as needed. */
    (void)esp_rmaker_automation_service_reload_for_node(bridge_internal_child_node(child));
}

/* Visitor for the MQTT-connect fan-out. Skips children whose devices
 * haven't been committed yet - until commit_devices, the local view is
 * incomplete and a cloud-info round-trip (which the cloud uses to
 * confirm group / sched / trigger state) would race against the
 * incoming params. */
static esp_rmaker_error_t __on_connect_visitor(esp_rmaker_bridge_child_handle_t child, void *unused)
{
    (void)unused;
    if (!child || !child->devices_committed) {
        return ESP_RMAKER_OK;
    }
    /* Republish online - parent reconnect must re-affirm child reachability,
     * otherwise cloud (which marks the child offline on parent disconnect) stays
     * out-of-sync. Online is the per-node flag (single source of truth). Mirrors
     * self's path in esp_rmaker_check_and_report_online_status. */
    const esp_rmaker_node_t *cnode = bridge_internal_child_node(child);
    if (esp_rmaker_state_mark_for_update_online_for_node(cnode, esp_rmaker_node_is_online(cnode)) == ESP_RMAKER_OK) {
        bridge_internal_dispatch_child_event(child, BRIDGE_CHILD_EVENT_ONLINE);
    }
    osal_err_t qerr = esp_rmaker_work_queue_add_task(bridge_internal_child_on_connect_task, (void *)child);
    return (qerr == OSAL_ERR_OK) ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

/**
 * @brief Bridge's handler for ``RMAKER_MQTT_EVENT_CONNECTED`` /
 *        ``RMAKER_MQTT_EVENT_DISCONNECTED``.
 *
 * Recovery-only: the cloud-info bundle for first-connect is driven by
 * ``commit_devices``. This handler only fires the fan-out when a real
 * disconnect has occurred since the last connect - preventing redundant
 * publishes on the initial connect.
 */
static void __bridge_on_mqtt_state(void *unused_arg, osal_event_base_t base, int32_t id, void *unused_data)
{
    (void)unused_arg;
    (void)base;
    (void)unused_data;
    if (!__initialized) {
        return;
    }
    if (id == RMAKER_MQTT_EVENT_DISCONNECTED) {
        __has_disconnected = true;
        return;
    }
    if (id != RMAKER_MQTT_EVENT_CONNECTED) {
        return;
    }
    if (!__has_disconnected) {
        /* First connect - nothing to recover. commit_devices drives the
         * initial cloud-info publish for each child. */
        return;
    }
    __has_disconnected = false;
    bridge_internal_for_each_ready_child(__on_connect_visitor, NULL);
}

/* Bridge response handlers (called from bridge_events.c) ********************/

#include "esp_rmaker_event_loop.h"
#include "osal_event_loop.h"

static void __post_event(int32_t event_id, const void *data, size_t data_len)
{
    osal_event_post(RMAKER_EVENT, event_id, (void *)data, data_len, OSAL_MAX_DELAY);
}

/* Called from bridge_events.c when a successful bridgeAck arrives with a
 * child_thing_name. Promotes the matching PENDING_ADD entry to READY and
 * fires RMAKER_EVENT_BRIDGE_CHILD_ADDED. */
void bridge_handle_ack_success_add(const char *child_thing_name);
void bridge_handle_ack_success_add(const char *child_thing_name)
{
    if (!child_thing_name) {
        return;
    }
    __lock();
    struct esp_rmaker_bridge_child *c = __find_by_thing_name_locked(child_thing_name);
    if (!c || c->state != RMNG_BRIDGE_CHILD_STATE_PENDING_ADD) {
        /* With request_id correlation the dispatcher only calls this on
         * a known-add ack, so reaching here means the slot vanished or
         * changed state between publish and ack (true race). Not warn-
         * worthy. */
        OSAL_LOGD(TAG, "bridgeAck add: no pending child for thing_name=%s", child_thing_name);
        __unlock();
        return;
    }
    c->state = RMNG_BRIDGE_CHILD_STATE_READY;
    OSAL_LOGI(TAG, "success_add: promoted to READY thing=%s", child_thing_name);

    esp_rmaker_event_bridge_child_added_t event = {
        .child = c,
        .child_thing_name = c->thing_name,
        .bridge_local_id = c->bridge_local_id,
    };
    __unlock();

    __post_event(RMAKER_EVENT_BRIDGE_CHILD_ADDED, &event, sizeof(event));

    /* The bridge filter (rule-A from_cloud + rule-B params) is a
     * wildcard subscribed once at bridge init; once a child reaches
     * READY its inbound delivery is already armed. Notify any
     * registered observer so wait_on_state_started_listening unblocks. */
    bridge_internal_dispatch_child_event(c, BRIDGE_CHILD_EVENT_STATE_STARTED_LISTENING);
}

/* Called from bridge_events.c when a non-success bridgeAck is correlated
 * to an add request. Rolls back the optimistically-staged slot and fires
 * RMAKER_EVENT_BRIDGE_CHILD_ADD_FAILED. Mirrors the
 * bridge_handle_ack_success_add -> RMAKER_EVENT_BRIDGE_CHILD_ADDED
 * pattern so all bridge-child outcome events post from one place. */
void bridge_handle_ack_failure_add(const char *bridge_local_id, const char *child_suffix, const char *error);
void bridge_handle_ack_failure_add(const char *bridge_local_id, const char *child_suffix, const char *error)
{
    if (!bridge_local_id) {
        return;
    }
    __lock();
    struct esp_rmaker_bridge_child *c = __find_by_local_id_locked(bridge_local_id);
    if (c && c->state == RMNG_BRIDGE_CHILD_STATE_PENDING_ADD) {
        (void)bridge_child_triggers_nvs_erase(c);
        esp_rmaker_schedule_service_erase_node(bridge_internal_child_node(c));
        __free_child_locked(c);
    } else if (c) {
        OSAL_LOGD(TAG, "addChild failure ack for child not in PENDING_ADD (state=%d local_id=%s); slot kept",
                  (int)c->state, bridge_local_id);
    }
    __unlock();

    esp_rmaker_event_bridge_child_failed_t event = {
        .child_suffix    = child_suffix,
        .bridge_local_id = bridge_local_id,
        .error           = error ? error : "",
    };
    __post_event(RMAKER_EVENT_BRIDGE_CHILD_ADD_FAILED, &event, sizeof(event));
}

/* Called from bridge_events.c when a non-success bridgeAck is correlated
 * to a remove request. The local teardown already fired ``REMOVED`` at
 * publish-time, so this only emits the ``REMOVE_FAILED`` event for the
 * app to reconcile (see esp_rmaker_bridge.h on remove_child). */
void bridge_handle_ack_failure_remove_by_local_id(const char *bridge_local_id, const char *error);
void bridge_handle_ack_failure_remove_by_local_id(const char *bridge_local_id, const char *error)
{
    if (!bridge_local_id) {
        return;
    }
    esp_rmaker_event_bridge_child_failed_t event = {
        .child_suffix    = NULL,
        .bridge_local_id = bridge_local_id,
        .error           = error ? error : "",
    };
    __post_event(RMAKER_EVENT_BRIDGE_CHILD_REMOVE_FAILED, &event, sizeof(event));
}

/* Optimistic remove (called immediately after publish success). The
 * cloud's bridgeAck is correlated separately by request_id; success is
 * informational, failure surfaces as RMAKER_EVENT_BRIDGE_CHILD_REMOVE_FAILED. */
void bridge_handle_ack_success_remove_by_local_id(const char *bridge_local_id);
void bridge_handle_ack_success_remove_by_local_id(const char *bridge_local_id)
{
    if (!bridge_local_id) {
        return;
    }
    __lock();
    struct esp_rmaker_bridge_child *c = __find_by_local_id_locked(bridge_local_id);
    if (!c) {
        __unlock();
        return;
    }

    char *thing_name_copy = c->thing_name ? OSAL_STRDUP_EXTRAM(c->thing_name) : NULL;
    char *local_id_copy = c->bridge_local_id ? OSAL_STRDUP_EXTRAM(c->bridge_local_id) : NULL;
    /* Drop the node from the node-config pending list before
     * invalidating - the pending list holds the same node pointer, and
     * the drain otherwise would race against ``valid`` flipping
     * false. Safe to call outside the bridge mutex; the pending
     * module has its own. */
    const esp_rmaker_node_t *gone_node = bridge_internal_child_node(c);
    /* Erase the child's persisted trigger details while the handle is
     * still valid. The in-RAM trigger list is freed by __free_child_locked
     * -> _esp_rmaker_node_reset -> esp_rmaker_automation_drop_node. */
    (void)bridge_child_triggers_nvs_erase(c);
    /* Same for the child's persisted schedules - a real cloud-confirmed
     * remove must erase the NVS rows too, otherwise they'd orphan-park
     * forever on the next boot with no child to bind to.
     */
    esp_rmaker_schedule_service_erase_node(bridge_internal_child_node(c));
    /* Invalidate the slot. Downstream state ctx + timeseries entries
     * still hold a pointer to this slot's topic ctx but will reap
     * themselves on the next publish cycle when they see ``valid`` go
     * false - no explicit purge step required here. */
    __free_child_locked(c);
    __unlock();

    node_config_pending_remove(gone_node);

    esp_rmaker_event_bridge_child_removed_t event = {
        .child_thing_name = thing_name_copy,
        .bridge_local_id = local_id_copy,
    };
    __post_event(RMAKER_EVENT_BRIDGE_CHILD_REMOVED, &event, sizeof(event));
    free(thing_name_copy);
    free(local_id_copy);
}

/* Public API *****************************************************************/

esp_rmaker_error_t bridge_internal_init(void)
{
    if (__initialized) {
        return ESP_RMAKER_OK;
    }

    __children_mutex = osal_semaphore_create_mutex();
    if (!__children_mutex) {
        OSAL_LOGE(TAG, "Failed to create bridge children mutex");
        return ESP_RMAKER_FAIL;
    }

    /* Slot pool - prefer SPIRAM. calloc gives the same zero-init the old
     * static array had (in_use=false, node.lock=NULL, etc). Allocated once
     * and kept for the process lifetime: deinit clears the slots but does
     * NOT free the array, so subsystems that iterate the pool during the
     * reset/teardown sequence (which runs *after* bridge deinit) never see
     * a dangling NULL. A re-init reuses the existing allocation. */
    if (!__child_pool) {
        __child_pool = (struct __bridge_child_slot *)OSAL_CALLOC_EXTRAM(
                           CONFIG_RMNG_BRIDGE_MAX_CHILDREN, sizeof(struct __bridge_child_slot));
        if (!__child_pool) {
            OSAL_LOGE(TAG, "Failed to allocate bridge child pool (%d slots)",
                      CONFIG_RMNG_BRIDGE_MAX_CHILDREN);
            osal_semaphore_delete(__children_mutex);
            __children_mutex = NULL;
            return ESP_RMAKER_NO_MEM;
        }
    }

    /* Arms the per-child NVS record lock; without it the load-modify-store in every
     * bridge_child_nvs_set_* runs unsynchronised (its __lock() is NULL-guarded). */
    esp_rmaker_error_t err = bridge_child_nvs_init();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "bridge_child_nvs_init failed: %d", err);
        free(__child_pool);
        __child_pool = NULL;
        osal_semaphore_delete(__children_mutex);
        __children_mutex = NULL;
        return err;
    }

    err = bridge_to_cloud_init();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "bridge_to_cloud_init failed: %d", err);
        free(__child_pool);
        __child_pool = NULL;
        osal_semaphore_delete(__children_mutex);
        __children_mutex = NULL;
        return err;
    }

    /* Subscribe to MQTT connection events so we re-issue the per-child cloud-info
     * bundle on every reconnect. */
    osal_err_t reg_err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, __bridge_on_mqtt_state, NULL);
    if (reg_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register bridge MQTT connect handler: %d", reg_err);
        free(__child_pool);
        __child_pool = NULL;
        osal_semaphore_delete(__children_mutex);
        __children_mutex = NULL;
        return ESP_RMAKER_FAIL;
    }
    reg_err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_DISCONNECTED, __bridge_on_mqtt_state, NULL);
    if (reg_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register bridge MQTT disconnect handler: %d", reg_err);
        (void)osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, __bridge_on_mqtt_state);
        free(__child_pool);
        __child_pool = NULL;
        osal_semaphore_delete(__children_mutex);
        __children_mutex = NULL;
        return ESP_RMAKER_FAIL;
    }
    __has_disconnected = false;

    OSAL_LOGI(TAG, "Bridge subsystem init OK");
    __initialized = true;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t bridge_internal_deinit(void)
{
    if (!__initialized) {
        return ESP_RMAKER_OK;
    }
    /* Free all live child slots first. The mutex is still alive at this
     * point so the existing free path is safe. */
    __lock();
    for (size_t i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        if (__child_pool[i].in_use) {
            __free_child_locked(&__child_pool[i].child);
        }
    }
    /* Destroy each slot's per-node lock. Created lazily on first add by
     * _esp_rmaker_node_init and preserved across reuse; this is the only
     * place they are torn down. NULL for slots that were never used. */
    for (size_t i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        if (__child_pool[i].node.lock) {
            osal_semaphore_delete(__child_pool[i].node.lock);
            __child_pool[i].node.lock = NULL;
        }
    }
    /* Deliberately NOT freeing __child_pool here. The slots are now cleared
     * (in_use=false, node.lock=NULL), but the array stays allocated for the
     * process lifetime: the reset/teardown sequence continues to iterate the
     * pool *after* this deinit returns, and the locked iterators don't NULL-
     * check. A subsequent bridge_internal_init reuses the allocation. */
    __unlock();

    (void)osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, __bridge_on_mqtt_state);
    (void)osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_DISCONNECTED, __bridge_on_mqtt_state);

    if (__children_mutex) {
        osal_semaphore_delete(__children_mutex);
        __children_mutex = NULL;
    }
    __has_disconnected = false;
    __initialized = false;
    OSAL_LOGI(TAG, "Bridge subsystem deinit OK");
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_bridge_add_child(const char *child_suffix, const char *bridge_local_id)
{
    if (!__initialized) {
        return ESP_RMAKER_INVALID_STATE;
    }
    if (!bridge_internal_valid_suffix(child_suffix) || bridge_local_id == NULL || bridge_local_id[0] == '\0') {
        return ESP_RMAKER_INVALID_ARG;
    }

    __lock();
    if (__find_by_local_id_locked(bridge_local_id) != NULL) {
        /* Already known locally - caller should subscribe to the event
         * loop to learn about the existing child rather than calling
         * again. Treat as a no-op success. */
        __unlock();
        OSAL_LOGI(TAG, "add_child: bridge_local_id already known; ignoring");
        return ESP_RMAKER_OK;
    }
    struct esp_rmaker_bridge_child *c = __alloc_child_locked(child_suffix, bridge_local_id);
    if (!c) {
        __unlock();
        return ESP_RMAKER_NO_MEM;
    }
    __unlock();

    esp_rmaker_error_t err = bridge_to_cloud_publish_add_child(c);
    if (err != ESP_RMAKER_OK) {
        __lock();
        __free_child_locked(c);
        __unlock();
        return err;
    }

    /* Drain any schedule handles parked by the schedule service's on_start
     * for this child's local_id. Mandatory for restart-without-reset to
     * actually replay child schedules from NVS. */
    esp_rmaker_schedule_service_on_child_added(c);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_bridge_remove_child(esp_rmaker_bridge_child_handle_t child)
{
    if (!__initialized) {
        return ESP_RMAKER_INVALID_STATE;
    }
    if (!child) {
        return ESP_RMAKER_INVALID_ARG;
    }

    __lock();
    if (child->state != RMNG_BRIDGE_CHILD_STATE_READY) {
        __unlock();
        return ESP_RMAKER_INVALID_STATE;
    }
    child->state = RMNG_BRIDGE_CHILD_STATE_PENDING_REMOVE;
    __unlock();

    return bridge_to_cloud_publish_remove_child(child);
}

esp_rmaker_error_t esp_rmaker_bridge_child_mark_online(esp_rmaker_bridge_child_handle_t child, bool online)
{
    if (!__initialized) {
        return ESP_RMAKER_INVALID_STATE;
    }
    if (!child) {
        return ESP_RMAKER_INVALID_ARG;
    }

    __lock();
    if (child->state != RMNG_BRIDGE_CHILD_STATE_READY) {
        __unlock();
        return ESP_RMAKER_INVALID_STATE;
    }
    /* Pass the slot's stable node pointer; downstream layers
     * key off this pointer (and its validity flag) for the slot's
     * entire lifetime. */
    const esp_rmaker_node_t *node = (const esp_rmaker_node_t *)&__slot_of(child)->node;
    __unlock();

    /* Reachability is the per-node online flag. */
    esp_rmaker_node_set_online_for_node(node, online);

    /* Routes through the standard state-report pipeline; emitted as a
     * top-level "online": <bool> on both the named and indexed shadows
     * of the child's Thing. */
    esp_rmaker_error_t err = esp_rmaker_state_mark_for_update_online_for_node(node, online);
    if (err == ESP_RMAKER_OK) {
        bridge_internal_dispatch_child_event(child, BRIDGE_CHILD_EVENT_ONLINE);
    }
    return err;
}

esp_rmaker_error_t bridge_internal_child_get_report_synthetics(const esp_rmaker_node_t *node, bool *online, uint8_t *ncfg_hash, bool *has_ncfg)
{
    if (!node || !online || !ncfg_hash || !has_ncfg) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
    if (!child) {
        return ESP_RMAKER_NOT_FOUND;
    }
    /* Reachability comes from the per-node online flag. */
    *online = esp_rmaker_node_is_online(node);

    /* ncfg_ver is the persisted node-config checksum (change-token). */
    *has_ncfg = false;
    bridge_child_nvs_record_t r;
    if (bridge_child_nvs_load(child, &r) == ESP_RMAKER_OK && r.ncfg_checksum_set) {
        memcpy(ncfg_hash, r.ncfg_checksum, BRIDGE_CHILD_NCFG_CHECKSUM_LEN);
        *has_ncfg = true;
    }
    return ESP_RMAKER_OK;
}

const char *esp_rmaker_bridge_child_thing_name(esp_rmaker_bridge_child_handle_t child)
{
    return child ? child->thing_name : NULL;
}

const char *esp_rmaker_bridge_child_bridge_local_id(esp_rmaker_bridge_child_handle_t child)
{
    return child ? child->bridge_local_id : NULL;
}

esp_rmaker_node_t *esp_rmaker_bridge_child_node(esp_rmaker_bridge_child_handle_t child)
{
    return bridge_internal_child_node(child);
}

esp_rmaker_error_t esp_rmaker_bridge_child_commit_devices(esp_rmaker_bridge_child_handle_t child)
{
    if (!__initialized) {
        return ESP_RMAKER_INVALID_STATE;
    }
    if (!child) {
        return ESP_RMAKER_INVALID_ARG;
    }
    __lock();
    if (child->state != RMNG_BRIDGE_CHILD_STATE_READY) {
        __unlock();
        return ESP_RMAKER_INVALID_STATE;
    }
    /* Mark devices committed - gates the cloud-info fan-out and the
     * MQTT-reconnect visitor. From this point on, the local view is
     * complete enough that a cloud-info round-trip is meaningful. */
    child->devices_committed = true;
    /* Reset the session-fresh "group setup done" bit so that a follow-up
     * commit (e.g. after attaching more devices/params) re-triggers a
     * full state report once the cloud-info bundle's getGroupInfo
     * response comes back. Without this reset, the second commit only
     * publishes deltas and any param added against a never-reported
     * device stays invisible to the cloud. */
    child->group_setup_done = false;
    const esp_rmaker_node_t *node = bridge_internal_child_node(child);
    __unlock();
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    /* (Re-)insert into the pending list. If the node is already pending,
     * the add clears the inflight flag, so a republish on the next
     * retry tick picks up freshly attached devices/endpoints even when
     * a prior publish is mid-flight. */
    esp_rmaker_error_t err = node_config_pending_add(node);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    /* Kick the shared retry context so the publish goes out promptly
     * instead of waiting for the next scheduled tick. */
    esp_rmaker_core_kick_node_config_retry();

    /* Fire the cloud-info bundle. On fresh add this is the initial
     * publish; on re-commit it picks up new params from newly-attached
     * devices. */
    (void)__on_connect_visitor(child, NULL);
    return ESP_RMAKER_OK;
}

/* Per-child event observer **************************************************
 *
 * Real implementation only when the remote layer is built. Non-remote
 * builds use the header's macro stubs (mirrors the ``event_flags.h``
 * stub strategy), so callers compile uniformly with zero overhead. */

#if CONFIG_RMNG_HOST_CTRL

void bridge_internal_register_event_observer(bridge_internal_child_event_cb_t cb, void *priv)
{
    __child_event_cb = cb;
    __child_event_cb_priv = priv;
}

void bridge_internal_dispatch_child_event(esp_rmaker_bridge_child_handle_t child,
        bridge_internal_child_event_kind_t kind)
{
    if (!child) {
        return;
    }
    bridge_internal_child_event_cb_t cb = __child_event_cb;
    void *priv = __child_event_cb_priv;
    if (cb) {
        cb(child, kind, priv);
    }
}

#endif /* CONFIG_RMNG_HOST_CTRL */

/* Internal accessors for bridge_to_cloud.c **********************************/

/* Lock/unlock exposure for sibling translation units. */
void bridge_internal_lock(void);
void bridge_internal_unlock(void);
void bridge_internal_lock(void)
{
    __lock();
}
void bridge_internal_unlock(void)
{
    __unlock();
}
