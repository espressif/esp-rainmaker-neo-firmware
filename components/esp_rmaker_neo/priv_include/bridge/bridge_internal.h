/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bridge_internal.h
 * @brief Internal contract between the bridge module and the rest of the
 *        RainMaker Neo core (cloud manager, state pipeline, data-model adapter).
 *
 * Only used when CONFIG_RMNG_BRIDGE_ENABLED. Other code paths must guard
 * any reference to symbols declared here.
 */

#ifndef __BRIDGE_INTERNAL_H__
#define __BRIDGE_INTERNAL_H__

#include "sdkconfig.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_rmaker_bridge.h"
#include "esp_rmaker_error_types.h"
#include "esp_rmaker_node.h"
#include "json_parser.h"
#include "network/mqtt_topics.h"
#include "node_internal.h"
#include "constants/nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Child handle internal layout *********************************************/

/**
 * @brief Lifecycle state of a child handle.
 */
typedef enum {
    RMNG_BRIDGE_CHILD_STATE_PENDING_ADD = 0, /**< add_child sent, awaiting bridgeAck. */
    RMNG_BRIDGE_CHILD_STATE_READY,           /**< Cloud confirmed; child usable. */
    RMNG_BRIDGE_CHILD_STATE_PENDING_REMOVE,  /**< remove_child sent, awaiting ack. */
} esp_rmaker_bridge_child_state_t;

/**
 * @brief Per-child in-memory version-handshake progress for the
 *        getSchedVer/getSchedDetails (and trigger equivalent) cycle.
 *
 * Mirrors the self-only version registry in events.c, but carried on
 * the child so that simultaneous handshakes for self and any child do
 * not collide.
 */
typedef struct {
    bool is_new_version;     /**< Received a new version, awaiting details. */
    bool is_new_details;     /**< Received fresh details, awaiting version. */
    int  pending_version;    /**< Version-in-flight pending commit. -1 if unset. */
} esp_rmaker_bridge_version_progress_t;

/**
 * @brief Internal representation of a bridged child Thing.
 *
 * Embedded inside a fixed-size slot in the bridge module's child pool
 * (see ``CONFIG_RMNG_BRIDGE_MAX_CHILDREN``); the slot's "in use" flag
 * doubles as the validity flag for the slot's topic ctx. The handle
 * address therefore lives for the program lifetime, even after the
 * child is removed - state and timeseries pipelines holding the ctx
 * pointer learn about teardown via the validity flag and drop their
 * own references lazily on the next publish cycle.
 *
 * All mutable fields are guarded by the bridge mutex.
 */
struct esp_rmaker_bridge_child {
    char *thing_name;                          /**< ``<parent>--<suffix>`` (NULL until ack). */
    char *bridge_local_id;                     /**< Caller-supplied protocol id. */
    char *child_suffix;                        /**< Suffix portion (for pending-add diagnostics). */
    char *group_info_str;                      /**< ``<primary>[-<sg>-...]``, "" until first getGroupInfo. */
    char nvs_key[RMAKER_NVS_KEY_LEN_MAX + 1];  /**< Derived NVS key (SHA-256(local_id) first 7 bytes, hex-encoded). NUL-terminated, exactly 14 chars + NUL. */
    esp_rmaker_bridge_child_state_t state;
    bool devices_committed;                                /**< Set by commit_devices; gates outbound cloud-info bundle. RAM-only. */
    bool group_setup_done;                                 /**< Session-fresh bit: full state report scheduled at least once on this child this boot. RAM-only; default false. */
    esp_rmaker_bridge_version_progress_t sched_progress;   /**< schedule version handshake state */
    esp_rmaker_bridge_version_progress_t trigger_progress; /**< trigger version handshake state */
};

/* Hooks called by the rest of the SDK **************************************/

/**
 * @brief Bridge subsystem one-time init.
 *        Subsequent calls are a no-op.
 */
esp_rmaker_error_t bridge_internal_init(void);

/**
 * @brief Bridge subsystem teardown. Frees all live child
 *        slots, unregisters the MQTT-connect handler, and releases the
 *        internal mutex. No-op if init never ran. Cloud-side state
 *        (already-acked children) is not affected - only the in-memory
 *        registry.
 */
esp_rmaker_error_t bridge_internal_deinit(void);

/**
 * @brief Dispatch hook for ``bridgeAck`` cloud events. Called from the
 *        cloud manager's payload handler when ``first_char == 'b'``.
 *
 * @param[in]  event_name  Full event name (NUL-terminated).
 * @param[in,out] p_jctx   JSON parser positioned inside the event's
 *                         payload object. Caller leaves the object
 *                         on return.
 * @return ESP_RMAKER_OK if the event was understood and consumed,
 *         ESP_RMAKER_NOT_FOUND if the event name is unknown.
 */
esp_rmaker_error_t bridge_internal_dispatch_from_cloud_event(
    const char *event_name, jparse_ctx_t *p_jctx);

/**
 * @brief Number of bridge-namespace MQTT filter subscriptions issued by
 *        bridge_internal_subscribe()/bridge_internal_unsubscribe()
 *        (bridge_filter_cloud + bridge_filter_params).
 *
 * The cloud manager seeds its subscribe/unsubscribe ack gate with this count,
 * then reconciles against the actual ``out_issued`` reported below. Keep this
 * in sync with the number of subscribes attempted in bridge_subscriber.c.
 */
#define BRIDGE_INTERNAL_FILTER_SUB_COUNT  2u

/**
 * @brief Subscribe to the bridge-namespace MQTT filters
 *        (bridge_filter_cloud + bridge_filter_params). Issues the
 *        subscribes synchronously but does NOT wait for ACK - the cloud
 *        manager's atomic ack counters co-gate completion alongside the
 *        self start_listening ack.
 *
 * @param[out] out_issued If non-NULL, set to the number of subscribes that
 *             were successfully issued (0..BRIDGE_INTERNAL_FILTER_SUB_COUNT)
 *             - i.e. the number of ACKs the gate should expect from this call.
 *             Always written, even on partial/total failure, so the caller can adjust its ack target.
 *
 * @return ESP_RMAKER_OK if all subscribes were issued, ESP_RMAKER_FAIL if any
 *         failed (the remaining ones are still attempted).
 *
 * Idempotent.
 */
esp_rmaker_error_t bridge_internal_subscribe(uint8_t *out_issued);

/**
 * @brief Unsubscribe from the bridge-namespace filters. Called on cloud
 *        disconnect; co-gated with the self unsubscribe ack.
 *
 * @param[out] out_issued If non-NULL, set to the number of unsubscribes that
 *             were successfully issued (0..BRIDGE_INTERNAL_FILTER_SUB_COUNT).
 *             Always written, even on partial/total failure, so the caller can adjust its ack target.
 *
 * @return ESP_RMAKER_OK if all unsubscribes were issued, ESP_RMAKER_FAIL otherwise.
 */
esp_rmaker_error_t bridge_internal_unsubscribe(uint8_t *out_issued);

/* Registry iteration (used by state pipeline) ******************************/

typedef esp_rmaker_error_t (*bridge_internal_child_visitor_t)(
    esp_rmaker_bridge_child_handle_t child, void *priv);

/**
 * @brief Iterate every READY child. Visitor return values are ignored
 *        (iteration always completes).
 */
void bridge_internal_for_each_ready_child(
    bridge_internal_child_visitor_t visitor, void *priv);

/**
 * @brief Look up a READY child by its cloud-assigned thing name.
 * @return Handle on hit, NULL if not found or not ready.
 */
esp_rmaker_bridge_child_handle_t bridge_internal_find_by_thing_name(
    const char *thing_name);

/**
 * @brief Look up a child by its bridge-protocol identifier (any state).
 * @return Handle on hit, NULL if absent.
 */
esp_rmaker_bridge_child_handle_t bridge_internal_find_by_local_id(
    const char *bridge_local_id);

/**
 * @brief Look up a child by its derived NVS key (14-char hex of
 *        ``SHA-256(bridge_local_id)[0:7]``; see
 *        ::bridge_internal_child_nvs_key). Any state.
 *
 * @return Handle on hit, NULL if absent or ``nvs_key`` NULL/empty.
 */
esp_rmaker_bridge_child_handle_t bridge_internal_find_by_nvs_key(
    const char *nvs_key);

/**
 * @brief Get the slot-embedded node for the given child handle.
 *
 * The node address is stable for the program lifetime (slot pool storage).
 * Returns NULL only if ``child`` is NULL.
 */
esp_rmaker_node_t *bridge_internal_child_node(
    esp_rmaker_bridge_child_handle_t child);

/**
 * @brief Recover a child handle from its slot-embedded node pointer.
 *
 * Iterates the slot pool. Returns NULL if ``node`` is not a slot-embedded
 * node, or the slot is not in use.
 */
esp_rmaker_bridge_child_handle_t bridge_internal_child_from_node(
    const esp_rmaker_node_t *node);

/**
 * @brief Iterate every READY child's node.
 */
void bridge_internal_for_each_ready_node(esp_rmaker_node_visitor_t visitor, void *priv);

/**
 * @brief Recover a child handle from its topic ctx (NULL for self).
 *
 * @return Child handle, or NULL if ``ctx`` is the self ctx (or NULL).
 */
esp_rmaker_bridge_child_handle_t bridge_internal_child_from_ctx(
    const esp_rmaker_topic_ctx_t *ctx);

/**
 * @brief Fetch the synthetic-entry source values for a child for a full
 *        state report.
 *
 * On unknown node (e.g. self node or a freed slot) returns
 * ::ESP_RMAKER_NOT_FOUND with outputs left unmodified.
 *
 * @param[in]  node      Node identifying the child.
 * @param[out] online    Last reachability marked via
 *                       ::esp_rmaker_bridge_child_mark_online; defaults
 *                       to false until the app marks the child.
 * @param[out] ncfg_hash Persisted per-child node-config SHA-256 checksum
 *                       (RMAKER_CHECKSUM_LEN / BRIDGE_CHILD_NCFG_CHECKSUM_LEN
 *                       bytes). Written only when ``*has_ncfg`` is set true.
 * @param[out] has_ncfg  True iff a stored checksum was found for the child.
 */
esp_rmaker_error_t bridge_internal_child_get_report_synthetics(
    const esp_rmaker_node_t *node, bool *online, uint8_t *ncfg_hash, bool *has_ncfg);

/**
 * @brief Get the caller-supplied ``bridge_local_id`` for a child.
 *        Stable for the lifetime of the slot.
 * @return Pointer to the NUL-terminated id, or NULL if ``child`` is NULL.
 */
const char *bridge_internal_child_local_id(
    esp_rmaker_bridge_child_handle_t child);

/**
 * @brief Get the child's current in-memory ``group_info_str``.
 * @return Pointer to the NUL-terminated string ("" until first
 *         getGroupInfo response), or NULL if ``child`` is NULL.
 */
const char *bridge_internal_child_group_info_str(
    esp_rmaker_bridge_child_handle_t child);

/**
 * @brief Replace the child's in-memory ``group_info_str``. Frees the
 *        existing string and duplicates ``new_str``. Caller must hold
 *        no other reference to the previous value. Not persisted to NVS:
 *        ``group_info_str`` is re-fetched via getGroupInfo on every
 *        add_child (including after reboot), so the in-memory copy is
 *        authoritative.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_NO_MEM on alloc failure.
 */
esp_rmaker_error_t bridge_internal_child_set_group_info_str(
    esp_rmaker_bridge_child_handle_t child, const char *new_str);

/**
 * @brief Get the child's NVS key.
 *
 * Derived once at slot allocation from ``SHA-256(bridge_local_id)`` -
 * first 7 bytes hex-encoded, 14 ASCII chars + NUL. Always within the
 * NVS key length cap (::RMAKER_NVS_KEY_LEN_MAX = 15) and stable across
 * reboots for the same ``bridge_local_id``.
 *
 * @return Pointer to the NUL-terminated key, or NULL if ``child`` is NULL.
 */
const char *bridge_internal_child_nvs_key(
    esp_rmaker_bridge_child_handle_t child);

/**
 * @brief Compute the NVS key for a given ``bridge_local_id`` without a
 *        child handle. Exposed for unit-test fabrication of stack
 *        handles. Output buffer must be at least
 *        ``RMAKER_NVS_KEY_LEN_MAX + 1`` bytes.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_INVALID_ARG otherwise.
 */
esp_rmaker_error_t bridge_internal_compute_nvs_key(
    const char *bridge_local_id, char *out, size_t out_size);

/**
 * @brief Acquire / release the bridge module's child-pool mutex.
 *
 * Exposed for data-model-specific bridge integrations that touch
 * bridge-owned state outside ``bridge.c``. All bridge slot fields
 * (``state``, ``thing_name``, etc.) and per-DM bridge-binding tables
 * are guarded by this mutex.
 */
void bridge_internal_lock(void);
void bridge_internal_unlock(void);

/**
 * @brief Which version-handshake stream the caller is asking about.
 */
typedef enum {
    BRIDGE_VERSION_KIND_SCHED = 0,
    BRIDGE_VERSION_KIND_TRIGGER = 1,
} bridge_version_kind_t;

/**
 * @brief Get the per-child in-memory version-handshake progress slot.
 *
 * Mutable; caller is expected to hold the appropriate synchronization
 * (cloud-manager dispatch task is the sole writer today). Returns NULL
 * if ``child`` is NULL.
 */
esp_rmaker_bridge_version_progress_t *bridge_internal_child_version_progress(
    esp_rmaker_bridge_child_handle_t child, bridge_version_kind_t kind);

/**
 * @brief Read / write the session-fresh "group setup done" bit on a
 *        child slot.
 *
 * The bit is RAM-only and starts false on every boot (including reboots
 * that reload an existing child from NVS). It is set to true after the
 * first full state report has been scheduled for this child this boot,
 * so that subsequent in-session getGroupInfo responses (same group, no
 * migration) don't re-trigger the report. Migration / first-set still
 * trigger independently in the events handler.
 */
bool bridge_internal_child_group_setup_done(esp_rmaker_bridge_child_handle_t child);
void bridge_internal_child_set_group_setup_done(esp_rmaker_bridge_child_handle_t child, bool done);

/**
 * @brief Whether the child's devices have been committed (commit_devices called).
 * Used to defer the child's first full state report until the consumer has finished
 * building the node (e.g. a bridge-side capability table), so the report does not
 * race a still-incomplete local view.
 */
bool bridge_internal_child_devices_committed(esp_rmaker_bridge_child_handle_t child);

/* Per-child event observer *************************************************
 *
 * Lightweight in-process notification path used by the remote-control
 * bridge handler to drive per-child wait_flags / clear_flags. Sites that
 * complete a meaningful child-scoped operation call
 * ::bridge_internal_dispatch_child_event with the kind; the registered
 * observer (today: bridge_handlers.c) sets the matching bit in its
 * per-child bitmap.
 *
 * Single observer slot - the remote layer is the only consumer. Dispatch
 * is synchronous; callbacks must not block.
 *
 * Compiled out on non-remote builds - the kind enum stays visible so
 * emit-site callers compile uniformly, but ``register`` and ``dispatch``
 * collapse to no-ops.
 */

typedef enum {
    BRIDGE_CHILD_EVENT_ONLINE = 0,
    BRIDGE_CHILD_EVENT_GROUP_INFO,
    BRIDGE_CHILD_EVENT_STATE_STARTED_LISTENING,
    BRIDGE_CHILD_EVENT_NODE_CONFIG_SENT,
    BRIDGE_CHILD_EVENT_TRIGGER_DETAILS_RECEIVED,
    BRIDGE_CHILD_EVENT_SCHED_DETAILS_RECEIVED,
} bridge_internal_child_event_kind_t;

typedef void (*bridge_internal_child_event_cb_t)(
    esp_rmaker_bridge_child_handle_t child,
    bridge_internal_child_event_kind_t kind,
    void *priv);

#if CONFIG_RMNG_HOST_CTRL

/**
 * @brief Register a single observer for per-child operational events.
 *        Pass NULL to clear. Calling twice replaces the prior observer.
 */
void bridge_internal_register_event_observer(bridge_internal_child_event_cb_t cb, void *priv);

/**
 * @brief Fire the registered observer (if any) for @p child and @p kind.
 *        No-op when no observer is registered or @p child is NULL.
 */
void bridge_internal_dispatch_child_event(
    esp_rmaker_bridge_child_handle_t child,
    bridge_internal_child_event_kind_t kind);

#else /* !CONFIG_RMNG_HOST_CTRL */

#define bridge_internal_register_event_observer(cb, priv) do { (void)(cb); (void)(priv); } while (0)
#define bridge_internal_dispatch_child_event(child, kind) do { (void)(child); (void)(kind); } while (0)

#endif /* CONFIG_RMNG_HOST_CTRL */

/* Test-only helpers ********************************************************
 *
 * Seed a child slot directly without the cloud round-trip the public API
 * (::esp_rmaker_bridge_add_child) performs. Intended for unit tests that
 * exercise the accessor surface (ctx <-> child, version_progress, NVS) on
 * a real slot without requiring a live MQTT broker.
 *
 * Released into the same slot pool as production children, so
 * test-seeded handles are indistinguishable from real ones to downstream
 * consumers (state pipeline, cloud-manager dispatch).
 */

/**
 * @brief Seed a child slot for testing. Initialises bridge subsystem
 *        if necessary, allocates a slot, and promotes it directly to
 *        READY (skipping the bridgeAck wait).
 *
 * @param[in] child_suffix    Caller-supplied suffix (validated).
 * @param[in] bridge_local_id Caller-supplied protocol id.
 * @param[in] thing_name      Synthetic thing name (e.g. ``"test--A"``).
 * @return Stable child handle, or NULL on alloc / validation failure.
 */
esp_rmaker_bridge_child_handle_t bridge_internal_test_seed_child(
    const char *child_suffix, const char *bridge_local_id, const char *thing_name);

/* Pure helpers (exposed for unit testing) ***********************************/

/**
 * @brief Validate a child suffix per spec section 4.2.4: ``[A-Za-z0-9_]{1,32}``.
 *
 * @return ``true`` iff the suffix is non-NULL, non-empty, no more than 32
 *         characters long, and contains only ASCII alphanumerics or
 *         underscore. Single hyphens are deliberately rejected - they
 *         would create ambiguity around the bridge name's ``--``
 *         separator.
 */
bool bridge_internal_valid_suffix(const char *s);

/**
 * @brief Extract the ``<child>`` segment from a Rule-A-rewritten
 *        ``from_cloud`` topic of the form
 *        ``rainmaker/bridges/<self>/children/<child>/from_cloud``.
 *
 * @param[in]  topic       Topic string (not necessarily NUL-terminated).
 * @param[in]  topic_len   Length of ``topic`` in bytes.
 * @param[out] out         Buffer to receive the NUL-terminated child name.
 * @param[in]  out_size    Size of ``out`` (must be > length of child).
 *
 * @return Number of bytes written to ``out`` (excluding NUL), or -1 on
 *         malformed input.
 */
int bridge_internal_parse_child_from_from_cloud_topic(
    const char *topic, size_t topic_len, char *out, size_t out_size);

/**
 * @brief Extract ``<child>`` and ``<shadow_name>`` from a Rule-B-rewritten
 *        unicast params topic of the form
 *        ``rainmaker/bridges/<self>/children/<child>/user/<shadow_name>/params``.
 *
 * @param[in]  topic            Topic string.
 * @param[in]  topic_len        Length in bytes.
 * @param[out] child_out        Buffer for the child name.
 * @param[in]  child_out_size   Size of ``child_out``.
 * @param[out] shadow_out       Buffer for the shadow-name segment.
 * @param[in]  shadow_out_size  Size of ``shadow_out``.
 *
 * @return Number of bytes written to ``shadow_out`` (excluding NUL), or
 *         -1 on malformed input.
 */
int bridge_internal_parse_child_and_shadow_from_params_topic(
    const char *topic, size_t topic_len,
    char *child_out, size_t child_out_size,
    char *shadow_out, size_t shadow_out_size);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

#endif /* __BRIDGE_INTERNAL_H__ */
