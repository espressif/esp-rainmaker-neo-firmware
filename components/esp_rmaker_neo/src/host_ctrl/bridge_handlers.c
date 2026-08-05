/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bridge_handlers.c
 * @brief Host-control bridge sub-command dispatch.
 */

#include "bridge_handlers.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

#include "osal_ext_io.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

#include "esp_rmaker_host_ctrl_constants.h"
#include "esp_rmaker_host_ctrl.h"
#include "esp_rmaker_bridge.h"
#include "esp_rmaker_data_model.h"
#include "bridge/bridge_internal.h"
#include "data_model/dm_param_helpers.h"
#include "host_ctrl_processing.h"

#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_semaphore.h"
#include "osal_task.h"
#include "osal_ticks.h"

static const char *TAG = "rmng_hc_bridge";

#define BRIDGE_HOST_CTRL_POLL_MS         50
#define BRIDGE_HOST_CTRL_WAIT_MAX_MS  120000

/* Handle table *************************************************************
 *
 * Public handles are 32-bit uints encoded as decimal ASCII on the wire:
 *     handle = (gen << 16) | slot
 * The slot is an index into our own ``__table`` (not the bridge's slot
 * pool). The generation counter is bumped on every free so a stale
 * handle from a buggy test resolves to ::NOT_FOUND instead of a wrong
 * child. As a second line of defence, ``__resolve`` re-validates that
 * the bridge slot still hosts the same ``bridge_local_id`` we recorded
 * at add time.
 *
 * Locking discipline: never acquire the bridge mutex while holding
 * ``__table_mutex``. ``__resolve`` reads from the table, drops the
 * table lock, then queries the bridge. Conversely, work that runs
 * inside a bridge-held visitor (``__list_visitor``) is free to acquire
 * the table mutex.
 */

/* Per-child observable flag bits. Mapped to/from the wire flag chars by
 * ::__flag_char_to_bit. The global ``state_reported`` flag is the right
 * tool for "any publish acked"; there is no per-child equivalent because
 * the state-pipeline publish-complete path does not carry ctx. */
#define BRIDGE_FLAG_BIT_ONLINE                  (1u << 0)
#define BRIDGE_FLAG_BIT_GROUP_INFO              (1u << 1)
#define BRIDGE_FLAG_BIT_STATE_STARTED_LISTENING (1u << 2)
#define BRIDGE_FLAG_BIT_NODE_CONFIG_SENT        (1u << 3)
#define BRIDGE_FLAG_BIT_TRIGGER_DETAILS         (1u << 4)
#define BRIDGE_FLAG_BIT_SCHED_DETAILS           (1u << 5)

typedef struct {
    bool used;
    uint16_t gen;
    uint16_t flags; /* Cumulative bits, cleared explicitly via 'x' subcommand. */
    esp_rmaker_bridge_child_handle_t child;
    char *local_id; /* strdup'd at add time. */
} __handle_entry_t;

/* Allocated once in __init_once (prefers SPIRAM) and never freed - the
 * bridge handle table for host control lives for the process lifetime. */
static __handle_entry_t *__table = NULL;
static osal_semaphore_handle_t __table_mutex = NULL;

/* Forward decl - observer body lives further down with the rest of the
 * flag-bitmap plumbing. */
static void __on_bridge_child_event(esp_rmaker_bridge_child_handle_t child,
                                    bridge_internal_child_event_kind_t kind,
                                    void *priv);

static void __init_once(void)
{
    if (__table_mutex == NULL) {
        /* Allocate the handle table first - keep the invariant that a live
         * mutex implies a live table. */
        __table = (__handle_entry_t *)OSAL_CALLOC_EXTRAM(
                      CONFIG_RMNG_BRIDGE_MAX_CHILDREN, sizeof(__handle_entry_t));
        if (__table == NULL) {
            OSAL_LOGE(TAG, "Failed to allocate bridge host control handle table (%d slots)",
                      CONFIG_RMNG_BRIDGE_MAX_CHILDREN);
            return;
        }
        __table_mutex = osal_semaphore_create_mutex();
        if (__table_mutex == NULL) {
            free(__table);
            __table = NULL;
            return;
        }
        /* Register the per-child event observer once. The bridge core
         * stores a single observer slot, so re-registering is safe but
         * unnecessary. */
        bridge_internal_register_event_observer(__on_bridge_child_event, NULL);
    }
}

static void __tlock(void)
{
    if (__table_mutex) {
        osal_semaphore_take(__table_mutex, OSAL_MAX_DELAY);
    }
}

static void __tunlock(void)
{
    if (__table_mutex) {
        osal_semaphore_give(__table_mutex);
    }
}

static uint32_t __encode_handle(uint16_t gen, uint16_t slot)
{
    return ((uint32_t)gen << 16) | (uint32_t)slot;
}

static bool __decode_handle(uint32_t handle, uint16_t *slot_out, uint16_t *gen_out)
{
    uint16_t slot = (uint16_t)(handle & 0xFFFF);
    if (slot >= CONFIG_RMNG_BRIDGE_MAX_CHILDREN) {
        return false;
    }
    *slot_out = slot;
    *gen_out  = (uint16_t)((handle >> 16) & 0xFFFF);
    return true;
}

/* Caller must NOT hold the table mutex. */
static esp_rmaker_bridge_child_handle_t __resolve(uint32_t handle)
{
    uint16_t slot, gen;
    if (!__table || !__decode_handle(handle, &slot, &gen)) {
        return NULL;
    }

    __tlock();
    __handle_entry_t *e = &__table[slot];
    bool ok = e->used && e->gen == gen && e->local_id != NULL;
    char *local_id_copy = ok ? OSAL_STRDUP_EXTRAM(e->local_id) : NULL;
    esp_rmaker_bridge_child_handle_t recorded = ok ? e->child : NULL;
    __tunlock();

    if (!ok || !local_id_copy) {
        free(local_id_copy);
        return NULL;
    }

    esp_rmaker_bridge_child_handle_t live = bridge_internal_find_by_local_id(local_id_copy);
    free(local_id_copy);
    return (live == recorded) ? live : NULL;
}

static int __alloc_slot_locked(void)
{
    if (!__table) {
        return -1;
    }
    for (int i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        if (!__table[i].used) {
            return i;
        }
    }
    return -1;
}

static void __free_slot_locked(int slot)
{
    if (slot < 0 || slot >= CONFIG_RMNG_BRIDGE_MAX_CHILDREN) {
        return;
    }
    if (!__table) {
        return;
    }
    __handle_entry_t *e = &__table[slot];
    if (!e->used) {
        return;
    }
    free(e->local_id);
    e->local_id = NULL;
    e->child = NULL;
    e->flags = 0;
    e->gen = (uint16_t)(e->gen + 1);
    e->used = false;
}

static int __find_slot_by_local_id_locked(const char *local_id)
{
    if (!__table) {
        return -1;
    }
    for (int i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        __handle_entry_t *e = &__table[i];
        if (e->used && e->local_id && strcmp(e->local_id, local_id) == 0) {
            return i;
        }
    }
    return -1;
}

static int __find_slot_for_child_locked(esp_rmaker_bridge_child_handle_t child)
{
    if (!__table) {
        return -1;
    }
    for (int i = 0; i < CONFIG_RMNG_BRIDGE_MAX_CHILDREN; i++) {
        if (__table[i].used && __table[i].child == child) {
            return i;
        }
    }
    return -1;
}

/* Translate a wire flag character to its bridge-side bit. Returns 0 for
 * chars that have no per-child semantic. */
static uint16_t __flag_char_to_bit(char c)
{
    switch (c) {
    case RMAKER_HOST_CTRL_FLAG_CHAR_ONLINE:                  return BRIDGE_FLAG_BIT_ONLINE;
    case RMAKER_HOST_CTRL_FLAG_CHAR_GROUP_INFO:              return BRIDGE_FLAG_BIT_GROUP_INFO;
    case RMAKER_HOST_CTRL_FLAG_CHAR_STATE_STARTED_LISTENING: return BRIDGE_FLAG_BIT_STATE_STARTED_LISTENING;
    case RMAKER_HOST_CTRL_FLAG_CHAR_NODE_CONFIG_SENT:        return BRIDGE_FLAG_BIT_NODE_CONFIG_SENT;
    case RMAKER_HOST_CTRL_FLAG_CHAR_TRIGGER_DETAILS:         return BRIDGE_FLAG_BIT_TRIGGER_DETAILS;
    case RMAKER_HOST_CTRL_FLAG_CHAR_SCHED_DETAILS:           return BRIDGE_FLAG_BIT_SCHED_DETAILS;
    default:                                              return 0;
    }
}

/* Parse a contiguous run of flag characters into a bit mask. Unknown
 * chars cause a 0 return (caller treats as INVALID). */
static uint16_t __parse_flag_chars(const char *start, const char *end)
{
    uint16_t mask = 0;
    for (const char *p = start; p < end; p++) {
        uint16_t bit = __flag_char_to_bit(*p);
        if (!bit) {
            OSAL_LOGE(TAG, "wait/clear_flags: unknown flag char '%c'", *p);
            return 0;
        }
        mask |= bit;
    }
    return mask;
}

/* Observer: fired by the bridge core when a child-scoped operation
 * completes. Sets the matching bit in the slot's bitmap. */
static void __on_bridge_child_event(esp_rmaker_bridge_child_handle_t child,
                                    bridge_internal_child_event_kind_t kind,
                                    void *priv)
{
    (void)priv;
    uint16_t bit = 0;
    switch (kind) {
    case BRIDGE_CHILD_EVENT_ONLINE:                  bit = BRIDGE_FLAG_BIT_ONLINE; break;
    case BRIDGE_CHILD_EVENT_GROUP_INFO:              bit = BRIDGE_FLAG_BIT_GROUP_INFO; break;
    case BRIDGE_CHILD_EVENT_STATE_STARTED_LISTENING: bit = BRIDGE_FLAG_BIT_STATE_STARTED_LISTENING; break;
    case BRIDGE_CHILD_EVENT_NODE_CONFIG_SENT:        bit = BRIDGE_FLAG_BIT_NODE_CONFIG_SENT; break;
    case BRIDGE_CHILD_EVENT_TRIGGER_DETAILS_RECEIVED: bit = BRIDGE_FLAG_BIT_TRIGGER_DETAILS; break;
    case BRIDGE_CHILD_EVENT_SCHED_DETAILS_RECEIVED:   bit = BRIDGE_FLAG_BIT_SCHED_DETAILS; break;
    default:                                         return;
    }
    __tlock();
    int slot = __find_slot_for_child_locked(child);
    if (slot >= 0) {
        __table[slot].flags |= bit;
    }
    __tunlock();
}

/* Sub-command handlers *****************************************************/

static void __handle_add_child(uint8_t *payload, size_t payload_length)
{
    /* Format: <suffix>|<bridge_local_id>|<timeout_ms>| */
    char *d[3];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(payload, payload_length, d, 3)) {
        OSAL_LOGE(TAG, "add_child: invalid delimiter count");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    const char *suffix   = (const char *)payload;
    const char *local_id = d[0] + 1;
    int timeout_ms = atoi(d[1] + 1);
    if (timeout_ms <= 0 || timeout_ms > BRIDGE_HOST_CTRL_WAIT_MAX_MS) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    __init_once();

    esp_rmaker_error_t err = esp_rmaker_bridge_add_child(suffix, local_id);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "esp_rmaker_bridge_add_child failed: %d", (int)err);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    /* Poll for the child to reach READY (state transition driven by
     * the bridgeAck dispatcher). thing_name alone is not sufficient
     * because publish_add_child sets thing_name BEFORE publish - slot
     * is still PENDING_ADD at that point. The bridge is idempotent on
     * duplicate local_ids, so this also covers the already-known
     * no-op path. */
    int elapsed = 0;
    esp_rmaker_bridge_child_handle_t child = NULL;
    const char *thing_name = NULL;
    while (elapsed <= timeout_ms) {
        child = bridge_internal_find_by_local_id(local_id);
        if (child && child->state == RMNG_BRIDGE_CHILD_STATE_READY) {
            thing_name = esp_rmaker_bridge_child_thing_name(child);
            if (thing_name != NULL) {
                break;
            }
        }
        osal_task_delay(osal_ticks_from_ms(BRIDGE_HOST_CTRL_POLL_MS));
        elapsed += BRIDGE_HOST_CTRL_POLL_MS;
    }
    if (!child || !thing_name) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_TIMEOUT);
        return;
    }

    /* Allocate or reuse the table slot. */
    __tlock();
    int slot = __find_slot_by_local_id_locked(local_id);
    if (slot < 0) {
        slot = __alloc_slot_locked();
        if (slot >= 0) {
            __table[slot].used = true;
            free(__table[slot].local_id);
            __table[slot].local_id = OSAL_STRDUP_EXTRAM(local_id);
        }
    }
    uint32_t handle = 0;
    bool ok = false;
    if (slot >= 0 && __table[slot].local_id) {
        __table[slot].child = child;
        handle = __encode_handle(__table[slot].gen, (uint16_t)slot);
        ok = true;
    }
    __tunlock();

    if (!ok) {
        OSAL_LOGE(TAG, "Bridge host control handle table exhausted");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    /* Response payload: <handle>|<thing_name> */
    char out[16 + 1 + 128];
    int n = snprintf(out, sizeof(out), "%" PRIu32 "%c%s",
                     handle, RMAKER_HOST_CTRL_DELIMITER_CHAR, thing_name);
    if (n <= 0 || (size_t)n >= sizeof(out)) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, out, (size_t)n);
}

/* Fire-and-forget add_child. Returns OK immediately after the bridge
 * SDK accepts the add. Caller is expected to poll list_children to
 * confirm READY across the batch. Use when registering large pools
 * (e.g. the stress test) where serializing one bridgeAck round-trip
 * per child is the wall-time bottleneck. */
static void __handle_add_child_no_ack(uint8_t *payload, size_t payload_length)
{
    /* Format: <suffix>|<bridge_local_id>| */
    char *d[2];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(payload, payload_length, d, 2)) {
        OSAL_LOGE(TAG, "add_child_no_ack: invalid delimiter count");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    const char *suffix   = (const char *)payload;
    const char *local_id = d[0] + 1;

    __init_once();

    esp_rmaker_error_t err = esp_rmaker_bridge_add_child(suffix, local_id);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "esp_rmaker_bridge_add_child failed: %d", (int)err);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    /* Child is in the bridge pool immediately after the SDK accepts
     * (state may still be PENDING_ADD). Allocate / find the table slot
     * now so list_children can later surface this child once it reaches
     * READY. */
    esp_rmaker_bridge_child_handle_t child = bridge_internal_find_by_local_id(local_id);
    if (!child) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    __tlock();
    int slot = __find_slot_by_local_id_locked(local_id);
    if (slot < 0) {
        slot = __alloc_slot_locked();
        if (slot >= 0) {
            __table[slot].used = true;
            free(__table[slot].local_id);
            __table[slot].local_id = OSAL_STRDUP_EXTRAM(local_id);
        }
    }
    bool ok = false;
    if (slot >= 0 && __table[slot].local_id) {
        __table[slot].child = child;
        ok = true;
    }
    __tunlock();

    if (!ok) {
        OSAL_LOGE(TAG, "Bridge host control handle table exhausted (no_ack)");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_remove_child(uint8_t *payload, size_t payload_length)
{
    /* Format: <handle>|<timeout_ms>| */
    char *d[2];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(payload, payload_length, d, 2)) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    uint32_t handle = (uint32_t)strtoul((char *)payload, NULL, 10);
    int timeout_ms = atoi(d[0] + 1);
    if (timeout_ms <= 0 || timeout_ms > BRIDGE_HOST_CTRL_WAIT_MAX_MS) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    __init_once();

    esp_rmaker_bridge_child_handle_t child = __resolve(handle);
    if (!child) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return;
    }

    /* Snapshot the local_id so we can poll for slot teardown after the
     * bridge frees the child. */
    char *local_id_copy = NULL;
    int slot = -1;
    __tlock();
    slot = __find_slot_for_child_locked(child);
    if (slot >= 0 && __table[slot].local_id) {
        local_id_copy = OSAL_STRDUP_EXTRAM(__table[slot].local_id);
    }
    __tunlock();

    esp_rmaker_error_t err = esp_rmaker_bridge_remove_child(child);
    if (err != ESP_RMAKER_OK) {
        free(local_id_copy);
        OSAL_LOGE(TAG, "esp_rmaker_bridge_remove_child failed: %d", (int)err);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    int elapsed = 0;
    bool gone = false;
    while (elapsed <= timeout_ms) {
        if (!local_id_copy || bridge_internal_find_by_local_id(local_id_copy) == NULL) {
            gone = true;
            break;
        }
        osal_task_delay(osal_ticks_from_ms(BRIDGE_HOST_CTRL_POLL_MS));
        elapsed += BRIDGE_HOST_CTRL_POLL_MS;
    }
    free(local_id_copy);

    if (!gone) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_TIMEOUT);
        return;
    }

    __tlock();
    __free_slot_locked(slot);
    __tunlock();
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_mark_online(uint8_t *payload, size_t payload_length)
{
    /* Format: <handle>|<0|1>| */
    char *d[2];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(payload, payload_length, d, 2)) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    uint32_t handle = (uint32_t)strtoul((char *)payload, NULL, 10);
    const char *online_str = d[0] + 1;
    if (online_str[0] != '0' && online_str[0] != '1') {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    bool online = (online_str[0] == '1');

    __init_once();

    esp_rmaker_bridge_child_handle_t child = __resolve(handle);
    if (!child) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return;
    }

    esp_rmaker_error_t err = esp_rmaker_bridge_child_mark_online(child, online);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "esp_rmaker_bridge_child_mark_online failed: %d", (int)err);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

typedef const char *(*__string_getter_t)(esp_rmaker_bridge_child_handle_t);

static void __handle_get_string_field(uint8_t *payload, size_t payload_length,
                                      __string_getter_t getter)
{
    /* Format: <handle>| */
    char *d[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(payload, payload_length, d, 1)) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    uint32_t handle = (uint32_t)strtoul((char *)payload, NULL, 10);

    __init_once();

    esp_rmaker_bridge_child_handle_t child = __resolve(handle);
    if (!child) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return;
    }
    const char *value = getter(child);
    if (value == NULL) {
        /* Empty payload still counts as OK. */
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
        return;
    }
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, value, strlen(value));
}

/* list_children *************************************************************/

/* Max entries returned in a single list_children page. Bounds the heap
 * payload buffer regardless of CONFIG_RMNG_BRIDGE_MAX_CHILDREN, so large
 * child pools (e.g. 400) don't blow up a single response. Defined in the
 * shared host-control constants header so the Python wrapper pages with the same
 * value. The Python wrapper pages with start/count until a short read. */
#define __LIST_PAGE_MAX RMAKER_HOST_CTRL_BRIDGE_LIST_PAGE_MAX

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
    /* Pagination window over the host-visible (slot >= 0) children, in
     * visitation order. ``seen`` is the running logical index; ``skip``
     * entries are passed over, then up to ``limit`` are emitted. */
    size_t skip;
    size_t limit;
    size_t seen;
    size_t emitted;
} __list_ctx_t;

static esp_rmaker_error_t __list_visitor(esp_rmaker_bridge_child_handle_t child, void *priv)
{
    __list_ctx_t *ctx = (__list_ctx_t *)priv;
    if (ctx->overflow) {
        return ESP_RMAKER_OK;
    }

    __tlock();
    int slot = __find_slot_for_child_locked(child);
    uint32_t handle = (slot >= 0) ? __encode_handle(__table[slot].gen, (uint16_t)slot) : 0;
    __tunlock();
    if (slot < 0) {
        /* Child added via SDK directly, not visible to the host control layer.
         * Not counted toward the pagination index so the window stays
         * stable across pages. */
        return ESP_RMAKER_OK;
    }

    /* Logical index among host-visible children (post-increment). */
    size_t idx = ctx->seen++;
    if (idx < ctx->skip) {
        return ESP_RMAKER_OK;  /* before the page window */
    }
    if (ctx->emitted >= ctx->limit) {
        return ESP_RMAKER_OK;  /* page full - rest belongs to later pages */
    }

    const char *thing_name = esp_rmaker_bridge_child_thing_name(child);
    if (!thing_name) {
        thing_name = "";
    }

    /* Entry format: <handle>:<thing_name>. The visitor only walks ready
     * children, so a state suffix would always be "ready" - omit it. */
    int written = snprintf(ctx->buf + ctx->len, ctx->cap - ctx->len,
                           "%s%" PRIu32 ":%s",
                           ctx->emitted == 0 ? "" : ",", handle, thing_name);
    if (written <= 0 || (size_t)written >= (ctx->cap - ctx->len)) {
        ctx->overflow = true;
        return ESP_RMAKER_OK;
    }
    ctx->len += (size_t)written;
    ctx->emitted++;
    return ESP_RMAKER_OK;
}

static void __handle_list_children(uint8_t *payload, size_t payload_length)
{
    /* Format: <start>|<count>| - paginated. ``start`` is the index of the
     * first host-visible child to emit; ``count`` caps how many (clamped
     * to __LIST_PAGE_MAX). The Python wrapper pages until a short read. */
    char *d[2];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(payload, payload_length, d, 2)) {
        OSAL_LOGE(TAG, "list_children: invalid delimiter count");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    long start = strtol((char *)payload, NULL, 10);
    long count = strtol(d[0] + 1, NULL, 10);
    if (start < 0 || count <= 0) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    if (count > __LIST_PAGE_MAX) {
        count = __LIST_PAGE_MAX;
    }

    __init_once();

    /* Worst case per entry: 10 (handle) + 1 (':') + ~64 (thing_name) + 1 (',') ~ 76.
     * Sized for a single page (__LIST_PAGE_MAX entries), not the whole pool,
     * so the buffer stays small regardless of CONFIG_RMNG_BRIDGE_MAX_CHILDREN.
     * Heap-allocated (prefers SPIRAM) to keep it out of static DRAM. */
    const size_t buf_cap = (size_t)__LIST_PAGE_MAX * 86 + 1;
    char *buf = (char *)OSAL_MALLOC_EXTRAM(buf_cap);
    if (!buf) {
        OSAL_LOGE(TAG, "list_children: failed to allocate %d-byte payload buffer", (int)buf_cap);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }
    __list_ctx_t ctx = {
        .buf = buf, .cap = buf_cap, .len = 0, .overflow = false,
        .skip = (size_t)start, .limit = (size_t)count, .seen = 0, .emitted = 0,
    };
    buf[0] = '\0';
    bridge_internal_for_each_ready_child(__list_visitor, &ctx);
    if (ctx.overflow) {
        OSAL_LOGE(TAG, "list_children: payload buffer overflow");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        free(buf);
        return;
    }
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, buf, ctx.len);
    free(buf);
}

/* Param / device management *************************************/

/* Split "<handle>|<rest>" into resolved child + pointer to <rest>.
 *
 * On success: returns the child handle, sets *p_rest to the byte after
 * the handle delimiter, and *p_rest_length to the remaining bytes
 * (including the trailing delimiter that the inner parser expects).
 * On failure: sends the appropriate response on the wire and returns NULL.
 */
static esp_rmaker_bridge_child_handle_t __split_handle(uint8_t *payload, size_t payload_length,
        uint8_t **p_rest, size_t *p_rest_length)
{
    if (payload == NULL || payload_length < 2) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return NULL;
    }
    /* Locate the first '|'. */
    uint8_t *delim = NULL;
    for (size_t i = 0; i < payload_length; i++) {
        if (payload[i] == RMAKER_HOST_CTRL_DELIMITER_CHAR) {
            delim = &payload[i];
            break;
        }
    }
    if (!delim) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return NULL;
    }
    *delim = '\0';
    uint32_t handle = (uint32_t)strtoul((char *)payload, NULL, 10);
    *p_rest        = delim + 1;
    *p_rest_length = payload_length - (size_t)(*p_rest - payload);

    __init_once();

    esp_rmaker_bridge_child_handle_t child = __resolve(handle);
    if (!child) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return NULL;
    }
    return child;
}

static void __handle_child_fill_info(uint8_t *payload, size_t payload_length)
{
    /* Format: <handle>|<name>|<type>|<fw_version>|<model>| */
    uint8_t *rest;
    size_t rest_length;
    esp_rmaker_bridge_child_handle_t child = __split_handle(payload, payload_length, &rest, &rest_length);
    if (!child) {
        return;
    }
    char *d[4];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(rest, rest_length, d, 4)) {
        OSAL_LOGE(TAG, "child_fill_info: invalid delimiter count");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    const char *name       = (const char *)rest;
    const char *type       = d[0] + 1;
    const char *fw_version = d[1] + 1;
    const char *model      = d[2] + 1;
    if (!*name || !*type || !*fw_version || !*model) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    esp_rmaker_node_t *node = esp_rmaker_bridge_child_node(child);
    if (!node) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return;
    }
    esp_rmaker_node_info_t info = {
        .name       = (char *)name,
        .type       = (char *)type,
        .fw_version = (char *)fw_version,
        .model      = (char *)model,
    };
    esp_rmaker_error_t err = esp_rmaker_node_fill_with_info(node, &info);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "esp_rmaker_node_fill_with_info failed: %d", (int)err);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_commit_devices(uint8_t *payload, size_t payload_length)
{
    /* Format: <handle>| */
    uint8_t *rest;
    size_t rest_length;
    esp_rmaker_bridge_child_handle_t child = __split_handle(payload, payload_length, &rest, &rest_length);
    if (!child) {
        return;
    }
    esp_rmaker_error_t err = esp_rmaker_bridge_child_commit_devices(child);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "commit_devices failed: %d", (int)err);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

/* Bridge-side resolve-node hook: route add_param onto the child's node. */
static esp_rmaker_node_t *__bridge_resolve_child_node(void *priv)
{
    return esp_rmaker_bridge_child_node((esp_rmaker_bridge_child_handle_t)priv);
}

static void __handle_child_add_param(uint8_t *payload, size_t payload_length)
{
    uint8_t *rest;
    size_t rest_length;
    esp_rmaker_bridge_child_handle_t child = __split_handle(payload, payload_length, &rest, &rest_length);
    if (!child) {
        return;
    }
    esp_rmaker_host_ctrl_handle_add_param_with_hook(rest, rest_length,
            __bridge_resolve_child_node,
            (void *)child);
}

static void __handle_child_update_param(uint8_t *payload, size_t payload_length)
{
    uint8_t *rest;
    size_t rest_length;
    esp_rmaker_bridge_child_handle_t child = __split_handle(payload, payload_length, &rest, &rest_length);
    if (!child) {
        return;
    }
    esp_rmaker_host_ctrl_handle_update_param_with_hook(rest, rest_length,
            __bridge_resolve_child_node,
            (void *)child);
}

static void __handle_child_get_param(uint8_t *payload, size_t payload_length)
{
    uint8_t *rest;
    size_t rest_length;
    esp_rmaker_bridge_child_handle_t child = __split_handle(payload, payload_length, &rest, &rest_length);
    if (!child) {
        return;
    }
    esp_rmaker_host_ctrl_handle_get_param_with_hook(rest, rest_length,
            __bridge_resolve_child_node,
            (void *)child);
}

/* wait_flags / clear_flags ****************************************/

static void __handle_wait_flags(uint8_t *payload, size_t payload_length)
{
    /* Format: <handle>|<flag_chars>|<timeout_ms>| */
    char *d[3];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(payload, payload_length, d, 3)) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    uint32_t handle = (uint32_t)strtoul((char *)payload, NULL, 10);
    char *flags_start = d[0] + 1, *flags_end = d[1];
    int timeout_ms = atoi(d[1] + 1);
    if (flags_end - flags_start < 1 || timeout_ms <= 0 || timeout_ms > BRIDGE_HOST_CTRL_WAIT_MAX_MS) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    uint16_t mask = __parse_flag_chars(flags_start, flags_end);
    if (!mask) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    __init_once();

    esp_rmaker_bridge_child_handle_t child = __resolve(handle);
    if (!child) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return;
    }

    int elapsed = 0;
    while (elapsed <= timeout_ms) {
        __tlock();
        int slot = __find_slot_for_child_locked(child);
        bool satisfied = (slot >= 0) && ((__table[slot].flags & mask) == mask);
        __tunlock();
        if (satisfied) {
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
            return;
        }
        osal_task_delay(osal_ticks_from_ms(BRIDGE_HOST_CTRL_POLL_MS));
        elapsed += BRIDGE_HOST_CTRL_POLL_MS;
    }
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_TIMEOUT);
}

static void __handle_clear_flags(uint8_t *payload, size_t payload_length)
{
    /* Format: <handle>|<flag_chars>| */
    char *d[2];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(payload, payload_length, d, 2)) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    uint32_t handle = (uint32_t)strtoul((char *)payload, NULL, 10);
    char *flags_start = d[0] + 1, *flags_end = d[1];
    if (flags_end - flags_start < 1) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    uint16_t mask = __parse_flag_chars(flags_start, flags_end);
    if (!mask) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    __init_once();

    esp_rmaker_bridge_child_handle_t child = __resolve(handle);
    if (!child) {
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return;
    }

    __tlock();
    int slot = __find_slot_for_child_locked(child);
    if (slot >= 0) {
        __table[slot].flags &= (uint16_t)~mask;
    }
    __tunlock();
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

/* Dispatch *****************************************************************/

void esp_rmaker_host_ctrl_bridge_handle(char sub_char, uint8_t *payload, size_t payload_length)
{
    switch (sub_char) {
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_ADD_CHILD:
        __handle_add_child(payload, payload_length);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_ADD_CHILD_NO_ACK:
        __handle_add_child_no_ack(payload, payload_length);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_REMOVE_CHILD:
        __handle_remove_child(payload, payload_length);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_MARK_ONLINE:
        __handle_mark_online(payload, payload_length);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_THING_NAME:
        __handle_get_string_field(payload, payload_length, esp_rmaker_bridge_child_thing_name);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_LOCAL_ID:
        __handle_get_string_field(payload, payload_length, esp_rmaker_bridge_child_bridge_local_id);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_GROUP_INFO:
        __handle_get_string_field(payload, payload_length, bridge_internal_child_group_info_str);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_LIST_CHILDREN:
        __handle_list_children(payload, payload_length);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_COMMIT_DEVICES:
        __handle_commit_devices(payload, payload_length);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_FILL_INFO:
        __handle_child_fill_info(payload, payload_length);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_ADD_PARAM:
        __handle_child_add_param(payload, payload_length);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_UPDATE_PARAM:
        __handle_child_update_param(payload, payload_length);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_GET_PARAM:
        __handle_child_get_param(payload, payload_length);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_WAIT_FLAGS:
        __handle_wait_flags(payload, payload_length);
        return;
    case RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_CLEAR_FLAGS:
        __handle_clear_flags(payload, payload_length);
        return;
    default:
        OSAL_LOGE(TAG, "Unknown bridge sub-command: %c", sub_char);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
}

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
