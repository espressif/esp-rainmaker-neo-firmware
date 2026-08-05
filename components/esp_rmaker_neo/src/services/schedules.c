/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file schedules.c
 * @brief Per-node schedule service backed by the esp_schedule component.
 *
 * Schedules are per-node: each node owns its esp_schedule handle list in
 * ``node->schedule`` (see node_internal.h). A getSchedDetails for a node
 * replaces only that node's schedules; others are untouched. Per-node
 * handle lists are guarded by the per-node lock (::esp_rmaker_node_lock).
 * The service itself keeps no global handle list; it only owns the
 * timezone-change event registration.
 *
 * Persistence is owned by this service, not by esp_schedule: we run
 * esp_schedule with NVS disabled and store the node's schedule details
 * ourselves. esp_schedule's own NVS persists only the trigger config, so a
 * fired schedule's action payload would not survive a reboot; replaying our
 * stored JSON reproduces both trigger and action through the same build path
 * a live getSchedDetails takes.
 *
 * Storage per node:
 *  - self node  -> ``local_config`` key ``sched_det``
 *  - bridge child -> ``bridge_scheds`` namespace, keyed by the child's
 *    fixed NVS key (see ::bridge_child_scheds_nvs_set)
 *
 * What is stored is *re-serialized from the live handles*
 * (::__serialize_node_schedules_locked), not the cloud's payload verbatim.
 * That normalizes the trigger arms we chose, omits entries we refused, and
 * records each trigger's computed next-fire timestamp -- without which a
 * relative one-shot would recompute ``now + rsec`` and fire again on every
 * boot.
 *
 * Reload order -- important: ``esp_rmaker_schedule_service_on_start`` runs from
 * ``esp_rmaker_start_task`` *before* MQTT comes up, and bridge children only
 * enter the pool once the remote bridge handler processes a re-announce after
 * MQTT is up. So on_start reloads the self node only; each child is reloaded
 * from its own NVS entry by ``esp_rmaker_schedule_service_on_child_added``
 * once ``esp_rmaker_bridge_add_child`` registers the slot. Because nothing is
 * materialized until its owner exists, no orphan parking is needed.
 *
 * If a stored payload no longer builds, the node's persisted schedule version
 * is voided so the cloud's version handshake re-pushes the details (mirrors
 * the automation service).
 *
 * Spent schedules are removed, by two rules that each fire at a moment we
 * control, so neither needs polling:
 *
 *  - *Fire time.* Whether a trigger can fire more than once is a property of
 *    its config, settled when it is parsed (``priv_data.one_shot``). So when a
 *    one-shot fires, it is spent -- no need to read esp_schedule's state back,
 *    and no dependence on it having re-armed yet. The fire task removes it and
 *    rewrites the node's details without it. It finds the schedule by cloud id,
 *    never by handle pointer, because a concurrent getSchedDetails may have
 *    replaced the set (a stale pointer would be a use-after-free; a stale id
 *    just misses).
 *
 *    Deleting the handle here crosses tasks -- the fire runs on the timer task
 *    while the work queue does the teardown -- so it relies on the port's timer
 *    ``cancel`` barriering against a running callback (see
 *    ``overrides/esp_schedule/port/esp_schedule_port_osal.c``). Without that barrier
 *    ``esp_schedule_delete`` frees the ``esp_schedule_t`` from under the
 *    callback still using it.
 *
 *  - *Arm time.* esp_schedule reports a successful arm by invoking the
 *    timestamp callback -- for every trigger type, and only when it found a real
 *    future occurrence. So after ::__arm_or_defer says it called
 *    ``esp_schedule_enable``, a still-zero ``priv_data.next_fire_ts`` means the
 *    schedule can never fire: expired while powered off, a year bound in the
 *    past, a validity window already closed. (The callback is used rather than
 *    reading the config back because ``esp_schedule_get`` fills
 *    ``next_scheduled_time_utc`` for RELATIVE triggers only, so a read-back
 *    cannot tell an armed date or weekday schedule from an unarmable one.)
 *    Applies on build/replay and on the timezone re-arm.
 *
 *    Note the *timing* for a repeating schedule that expires (e.g. a month mask
 *    bounded by ``yy``): esp_schedule re-arms itself internally after each fire,
 *    without going through ::__arm_or_defer, so the failed re-arm that marks its
 *    end is not observed at that moment. It is pruned at the next arm we drive
 *    -- the next boot's replay, a timezone change, or arm_all -- and until then
 *    sits armed-but-dead, holding a slot and staying in the stored payload. It
 *    cannot fire, so this is untidiness rather than misbehaviour. Detecting it
 *    promptly would need esp_schedule to report the exhausted re-arm (its
 *    ``diff == 0`` path currently skips the timestamp callback entirely);
 *    inferring it from our own fire path would race that re-arm and risk
 *    deleting a live schedule.
 *
 *  - *Replay.* A one-shot's armed instant is persisted as ``ts``, so a stored
 *    entry whose ``ts`` is already behind us is skipped without even building a
 *    handle. That covers a one-shot that came due while the device was off, and
 *    one that fired but whose removal write was lost. ``ts`` is deliberately not
 *    persisted for repeating triggers: theirs moves on every arm, which would
 *    make the payload differ on every boot and cost a flash write each time.
 *
 * A prune deliberately leaves the persisted schedule *version* alone: voiding
 * it would make the cloud re-push the expired schedule, which would be pruned
 * again. The device's details intentionally diverge from the cloud's copy for
 * spent one-shots.
 */

/* Includes *******************************************************/

/* Declarations */
#include "services/schedules.h"
/* Standard includes */
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

/* Timesync common includes */
#include "osal_timesync.h"

/* Time-sync flow selector (TIME_SYNC_DECOUPLED_FLOW) */
#include "time_sync_flow.h"

/* esp_schedule includes */
#include "esp_schedule_port_osal.h"

/* JSON emission for the persisted details */
#include "json_generator.h"

/* Platform common includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* RMNG includes */
#include "sdkconfig.h"
#include "data_model_internal.h"
#include "network/state_changes.h"
#include "esp_rmaker_work_queue.h"
#include "esp_rmaker_runtime_gate.h"
#include "event_flags.h"
#include "event_loop.h"
#include "node_internal.h"
#include "esp_rmaker_node.h"
#include "local_config.h"

/* Crypto + hex utils for schedule name derivation */
#include "util/esp_rmaker_crypto.h"
#include "util/esp_rmaker_convert_hex.h"

/* Constants includes */
#include "constants/nvs.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "bridge/bridge_internal.h"
#include "bridge/bridge_child_nvs.h"
#include "bridge/bridge_child_scheds_nvs.h"
#endif

/* Preprocessor definitions *******************************************************/

/** Per-node schedule cap. Bounds the heap of a single node's handle array;
 *  parse loop truncates excess entries with a warning. */
#define MAX_SCHEDULES_PER_NODE CONFIG_RMAKER_SCHEDULING_MAX_SCHEDULES

/** Schedule name length actually used (14 hex chars + NUL = 15, the NVS key
 *  cap on the platform). esp_schedule's own ``MAX_SCHEDULE_NAME_LEN`` is 16
 *  but the underlying NVS impl caps at 15 - keep ours at 14 to leave a clear
 *  margin. */
#define SCHEDULE_NAME_HEX_CHARS 14
#define SCHEDULE_NAME_BUF_SIZE  (SCHEDULE_NAME_HEX_CHARS + 1)

/* Types *******************************************************/

/**
 * @brief Schedule action struct (raw action JSON + length).
 */
typedef struct {
    char *data;       /**< Raw action JSON string. */
    size_t data_len;  /**< Length of ``data`` (incl. NUL). */
} __schedule_action_t;

/**
 * @brief Per-schedule priv_data wrapper - what we hand esp_schedule.
 *
 * ``node_key`` is the owning child's fixed 14-char NVS key
 * (``SHA-256(bridge_local_id)[0:7]`` hex; see
 * ::bridge_internal_child_nvs_key), or "" for the self node.
 *
 * ``cloud_id`` is kept because the schedule's esp_schedule *name* is a one-way
 * hash of it (see ::__derive_schedule_name): it is the only way to name a
 * schedule back to the cloud when re-serializing the node's details, and it is
 * the identity the fire path uses to find a spent schedule again (a handle
 * pointer could be freed by a concurrent update in the meantime).
 *
 * ``one_shot`` records whether the trigger can fire more than once. It is a
 * property of the parsed config, so it is known at build time and needs no
 * runtime observation - which is what lets the fire path decide a schedule is
 * spent without reading esp_schedule's state back.
 *
 * ``next_fire_ts`` is the wall-clock instant esp_schedule armed the schedule
 * for, delivered by ::__schedule_timestamp_callback. We keep our own copy
 * because ``esp_schedule_get`` populates ``next_scheduled_time_utc`` for
 * RELATIVE triggers only - for the day-of-week, date and solar types it leaves
 * the field untouched, so reading the config back cannot tell an armed schedule
 * from an unarmable one. The callback is invoked for every type and only on a
 * successful arm, which makes its arrival the signal instead.
 *
 * @note ``next_fire_ts`` is also written from the timer thread when a repeating
 *       schedule re-arms itself inside its own fired callback, concurrently with
 *       readers holding the node lock. It is a single aligned word and purely
 *       advisory (a stale read costs at most one redundant persist), so it is
 *       deliberately not locked.
 */
typedef struct {
    __schedule_action_t action;              /**< Action to fire. */
    char node_key[RMAKER_NVS_KEY_LEN_MAX + 1]; /**< Owning child NVS key, or "" for self. */
    char cloud_id[MAX_SCHEDULE_NAME_LEN + 1];  /**< Cloud-side schedule id. */
    bool one_shot;                             /**< Trigger fires at most once. */
    time_t next_fire_ts;                       /**< Instant esp_schedule armed for; 0 if unarmed. */
} __schedule_priv_data_t;

/* Variables *******************************************************/

/**
 * @brief Tag for the schedules service.
 */
static const char *TAG = "rmng_svc_schedules";

/** Whether the service has been initialized. The service owns no per-handle
 *  state of its own (per-node lists live in ``node->schedule``), so a flat
 *  flag is enough. */
static bool __initialized = false;

#if TIME_SYNC_DECOUPLED_FLOW
/** Whether wall-clock time is ready for schedule arming (decoupled flow).
 *
 *  ``esp_schedule_enable`` computes a trigger's next-fire timestamp off
 *  the wall clock, so arming a schedule before the clock has synced would
 *  compute a next-fire off a ~1970 clock. Until time is ready, arm
 *  requests are deferred: the handle stays in the node's in-RAM list and
 *  the node's details JSON stays in NVS (so it survives reboot, can still
 *  be edited/removed by a later cloud getSchedDetails, and gets armed once
 *  time syncs) but ``esp_schedule_enable`` is not called - it just does not
 *  fire yet.
 *  ::esp_rmaker_schedule_service_arm_all sets this true and re-arms every
 *  handle once the core time-sync poll observes a synced clock.
 *
 *  A handle created/rehydrated after time is ready (or on a warm boot with
 *  an already-valid clock) arms inline via the ``osal_timesync_is_synced()``
 *  fast path in ::__arm_or_defer. */
static bool __time_ready = false;

/** Arm ``h`` now if time is ready, otherwise defer. Clears
 *  ``priv->next_fire_ts`` first so the timestamp callback's arrival during
 *  ``esp_schedule_enable`` reports whether the arm found an occurrence.
 *  @return true if ``esp_schedule_enable`` was actually called. Only then is
 *          ::__schedule_is_spent meaningful. */
static bool __arm_or_defer(esp_schedule_handle_t h, __schedule_priv_data_t *priv)
{
    if (!h) {
        return false;
    }
    if (!__time_ready && !osal_timesync_is_synced()) {
        OSAL_LOGD(TAG, "Time not synced; deferring arm of schedule handle %p", h);
        return false;
    }
    /* Cleared so that the timestamp callback landing (or not) during enable is
     * what reports whether this arm found an occurrence. */
    if (priv) {
        priv->next_fire_ts = 0;
    }
    esp_schedule_enable(h);
    return true;
}
#else
/** Synchronous flow (MBEDTLS_HAVE_TIME_DATE): start_task blocks until the
 *  clock is synced before any schedule work runs, so arm inline
 *  unconditionally - no deferral state, no arm-all poll. */
static bool __arm_or_defer(esp_schedule_handle_t h, __schedule_priv_data_t *priv)
{
    if (!h) {
        return false;
    }
    /* Cleared so that the timestamp callback landing (or not) during enable is
     * what reports whether this arm found an occurrence. */
    if (priv) {
        priv->next_fire_ts = 0;
    }
    esp_schedule_enable(h);
    return true;
}
#endif /* TIME_SYNC_DECOUPLED_FLOW */

/* Private function declarations *******************************************************/

/* --- Node helpers --- */

/**
 * @brief Get a node's embedded schedule substruct.
 * @param[in] node The node.
 * @return Pointer to ``node->schedule``, or NULL if ``node`` is NULL.
 */
static node_schedule_state_t *__node_sched(const esp_rmaker_node_t *node);

/**
 * @brief Resolve an owning-node key to its node. NULL/empty resolves to
 *        the self node; otherwise matched against child NVS keys via
 *        ::bridge_internal_find_by_nvs_key.
 * @param[in] node_key  Owning child NVS key, or NULL/"" for self.
 * @return Owning node, or NULL if no matching child is registered.
 */
static const esp_rmaker_node_t *__resolve_node(const char *node_key);

/* --- Schedule name derivation --- */

/**
 * @brief Derive a NVS-safe schedule name from (local_id, cloud_id).
 *
 * Name is hex(SHA-256(local_id ":" cloud_id))[0:14]. The 56-bit truncation
 * is safe for the realistic per-device schedule count (birthday collision
 * at ~2.7e8 entries vs. tens-to-hundreds in practice). NULL/empty local_id
 * is treated as self-node so self schedules cannot collide with child
 * schedules sharing the same cloud id.
 *
 * @param[in]  local_id  Bridge local id, or NULL for self.
 * @param[in]  cloud_id  Cloud-side schedule id (any length).
 * @param[out] out       Buffer of at least ::SCHEDULE_NAME_BUF_SIZE bytes.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __derive_schedule_name(const char *local_id, const char *cloud_id, char *out);

/* --- Action / priv_data lifecycle --- */

/**
 * @brief Free the schedule action.
 * @param[in] action The schedule action to free.
 */
static void __schedule_action_free(__schedule_action_t *action);

/**
 * @brief Allocate a new ``__schedule_priv_data_t``.
 * @param[in] action_json     NUL-terminated action JSON to deep-copy.
 * @param[in] action_json_len Length of ``action_json`` including NUL.
 * @param[in] node_key        Owning child NVS key (NULL/"" for self).
 */
static __schedule_priv_data_t *__schedule_priv_data_new(const char *action_json, size_t action_json_len,
        const char *node_key, const char *cloud_id, bool one_shot);

/**
 * @brief Free a ``__schedule_priv_data_t``.
 */
static void __schedule_priv_data_free(__schedule_priv_data_t *priv);

/* --- Details-JSON persistence (this service owns it; esp_schedule NVS is off) --- */

/**
 * @brief Serialize a node's live schedules into a details JSON array.
 *
 * The inverse of ::__build_schedule_details_for_node_locked, and the form we
 * persist. It has to read from the handles rather than from the cloud payload
 * for two reasons: the schedule's esp_schedule *name* is a one-way hash so
 * ``id`` is only recoverable from priv_data, and re-emitting the trigger picks
 * up esp_schedule's *computed* next-fire timestamp, which is what keeps a
 * relative one-shot from firing again after a reboot.
 *
 * What is emitted must round-trip through ::__parse_trigger to the same
 * config. ``name`` is dropped (the parse ignores it).
 *
 * Two-pass, mirroring ``node_config.c``: size with a NULL buffer, then emit.
 *
 * @note Caller holds the node lock.
 * @param[in] node The node.
 * @return malloc'd NUL-terminated JSON array (caller frees), or NULL on error.
 *         A node with no schedules yields ``"[]"``.
 */
static char *__serialize_node_schedules_locked(const esp_rmaker_node_t *node);

/**
 * @brief Re-serialize ``node``'s live schedules and persist them, replacing
 *        whatever was stored. Used after a build and after a prune.
 * @note Caller must NOT hold the node lock (this takes it).
 */
static esp_rmaker_error_t __reserialize_and_persist(const esp_rmaker_node_t *node);

/**
 * @brief Persist ``node``'s schedule-details JSON.
 *
 * Self node goes to ``local_config``; a bridge child goes to the
 * ``bridge_scheds`` namespace under the child's fixed NVS key.
 *
 * @param[in] node The owning node.
 * @param[in] data NUL-terminated details JSON array string.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __persist_details(const esp_rmaker_node_t *node, const char *data);

/**
 * @brief Read back ``node``'s persisted schedule-details JSON.
 * @return malloc'd NUL-terminated string (caller frees), or NULL if nothing
 *         is stored.
 */
static char *__load_details(const esp_rmaker_node_t *node);

/**
 * @brief Erase ``node``'s persisted schedule-details JSON. Best-effort.
 */
static void __erase_details(const esp_rmaker_node_t *node);

/**
 * @brief Void ``node``'s persisted schedule version so the cloud's version
 *        handshake re-pushes the details.
 *
 * Used when a stored payload no longer builds - it may be corrupt, or have
 * been written by a release whose schema we no longer accept. Mirrors the
 * automation service's ``__invalidate_trigger_version``.
 */
static void __invalidate_sched_version(const esp_rmaker_node_t *node);

/**
 * @brief Whether a schedule we just armed has no future occurrence left.
 *
 * ``esp_schedule`` reports a successful arm by invoking the timestamp callback;
 * both of its early-return paths (invalid clock, no next occurrence) skip it. So
 * after ::__arm_or_defer reports it called ``esp_schedule_enable``, a still-zero
 * ``next_fire_ts`` means the schedule can never fire - expired while powered
 * off, a year bound in the past, a validity window already closed.
 *
 * The field must be cleared before arming (::__arm_or_defer does it) or a value
 * left over from an earlier arm would mask a failed one. And it must only be
 * consulted when we actually armed: zero is equally the state of the deferred
 * pre-timesync path.
 *
 * As a second guard this refuses to answer without a synced clock - deleting a
 * user's schedules off a bad clock is the failure mode worth being paranoid
 * about.
 *
 * @note This depends on the timestamp callback being invoked *synchronously*,
 *       inside our own ``esp_schedule_enable`` call. That holds for 1.5.0,
 *       whose ``start_timer`` calls it inline. Were a later esp_schedule to
 *       defer it - to a task, or to the first tick - every schedule would look
 *       spent the moment it was armed and be dropped from persistence, silently
 *       and for every user. Worth re-checking on each component bump; the
 *       arm-time tests in test_schedules.c are what would catch it.
 *
 * @param[in] priv priv_data of the handle that was just armed.
 * @return true if the schedule can never fire again.
 */
static bool __schedule_is_spent(const __schedule_priv_data_t *priv);

/* --- Per-node handle list ops (caller holds the node lock) --- */

/**
 * @brief Release every schedule handle in a node's slice and free the array.
 *
 * Handles are always deleted outright: esp_schedule runs with NVS disabled,
 * so a handle holds no persistent state and there is nothing to preserve.
 * Whether the node's *details JSON* survives is a separate decision made by
 * the caller (::esp_rmaker_schedule_service_unload_node keeps it,
 * ::esp_rmaker_schedule_service_erase_node erases it).
 *
 * @note Caller holds the node lock.
 * @param[in] node The node.
 */
static void __node_release_locked(const esp_rmaker_node_t *node);

/**
 * @brief Tear down a single esp_schedule handle: snapshot the config to free
 *        the priv_data we attached on create, then delete the handle. Safe on
 *        NULL.
 */
static void __release_handle(esp_schedule_handle_t h);

/**
 * @brief Append a single handle to a node's slice. Reallocates ``handles``.
 *        On allocation failure, the handle is deleted and the node's slice is
 *        left unchanged.
 * @note Caller holds the owning node's lock.
 */
static esp_rmaker_error_t __node_append_handle_locked(const esp_rmaker_node_t *node, esp_schedule_handle_t handle);

/**
 * @brief Remove the schedule with ``cloud_id`` from ``node``'s slice.
 *
 * Looks the schedule up by cloud id rather than by handle pointer: the fire
 * path runs asynchronously, and a concurrent getSchedDetails may have replaced
 * the whole set in the meantime. A stale pointer would be a use-after-free,
 * whereas a stale id simply misses and correctly does nothing.
 *
 * @note Caller holds the node lock.
 * @param[in] node     The node.
 * @param[in] cloud_id Cloud-side schedule id to drop.
 * @return true if a schedule was found and removed.
 */
static bool __node_remove_by_cloud_id_locked(const esp_rmaker_node_t *node, const char *cloud_id);


/* --- Trigger dispatch --- */

/**
 * @brief Work-queue task that runs a fired schedule's action against its
 *        owning node. Owns the ``__schedule_priv_data_t *`` copy handed
 *        to it by the trigger callback and frees it on completion.
 */
static void __schedule_fire_task(void *arg);

/**
 * @brief esp_schedule trigger callback. Deep-copies the live priv_data via
 *        ``__schedule_priv_data_new`` and queues the copy onto the work
 *        queue. The copy decouples the fire from the live priv_data so a
 *        concurrent update/delete of the schedule cannot free the bytes the
 *        work task is about to read.
 */
static void __schedule_trigger_callback(esp_schedule_handle_t handle, void *priv_data);

/**
 * @brief esp_schedule timestamp callback. Records the instant the schedule was
 *        armed for into ``priv_data.next_fire_ts``.
 *
 * esp_schedule invokes this from ``start_timer`` only once it has a real future
 * occurrence - both early-return paths (invalid clock, no next occurrence) skip
 * it. So for the initial arm, which runs synchronously inside our own
 * ``esp_schedule_enable`` call, the callback having landed *is* the answer to
 * "did this schedule arm?" for every trigger type. See ::__schedule_is_spent.
 *
 * @note The callback truncates the timestamp to ``uint32_t``. Harmless until
 *       2106.
 */
static void __schedule_timestamp_callback(esp_schedule_handle_t handle, uint32_t next_timestamp,
        void *priv_data);

/* --- JSON parsing --- */

/**
 * @brief Parse the action.
 * @param[in] jctx The JSON context.
 * @param[out] action The action to parse into.
 * @return ESP_RMAKER_OK on success, otherwise error code
 */
static esp_rmaker_error_t __parse_action(jparse_ctx_t *jctx, __schedule_action_t *action);

/**
 * @brief Parse the schedule's trigger.
 *
 * esp_schedule carries exactly one trigger per schedule
 * (``esp_schedule_config_t::trigger``). The cloud payload still sends a
 * ``"triggers"`` array, so only index 0 is used; any further entries are
 * dropped with a warning.
 *
 * @param[in]  jctx    The JSON context.
 * @param[out] trigger  The trigger to parse into.
 * @param[out] out_ts   The persisted ``ts`` (the instant a previous run armed
 *                      this schedule for), or 0 if absent. Only meaningful for
 *                      one-shots, where the caller uses it to skip a schedule
 *                      that has already come due. Reported for every type
 *                      because only RELATIVE seeds it into the trigger itself.
 * @return ESP_RMAKER_OK if a usable trigger was parsed, otherwise error code.
 */
static esp_rmaker_error_t __parse_trigger(jparse_ctx_t *jctx, esp_schedule_trigger_t *trigger,
        time_t *out_ts);

/**
 * @brief Whether ``trigger`` can fire at most once.
 *
 * A property of the config alone (see esp_schedule's docs/trigger_rules.md
 * section 3), so it is settled at build time and never has to be observed at
 * runtime. Evaluated against the trigger ::__parse_trigger produced, i.e. after
 * our own arm normalisation, so it reflects what esp_schedule was actually
 * handed.
 */
static bool __trigger_is_one_shot(const esp_schedule_trigger_t *trigger);

/**
 * @brief Parse the optional "validity" object.
 */
static esp_rmaker_error_t __parse_validity(jparse_ctx_t *jctx, esp_schedule_validity_t *validity);

/**
 * @brief Parse a schedule-details JSON array and install it on ``node``,
 *        replacing the node's existing handles. Caller holds the node lock.
 *
 * @param[in] node     Owning node.
 * @param[in] local_id Bridge local id (NULL for self) for name derivation
 *                     and priv_data.
 * @param[in] data     JSON array string.
 * @param[in] data_len Length of ``data``.
 */
static esp_rmaker_error_t __build_schedule_details_for_node_locked(
    const esp_rmaker_node_t *node, const char *local_id, const char *data, size_t data_len);

/**
 * @brief Resolve ``node``'s bridge local id, or NULL for the self node.
 *        Used for schedule-name derivation and priv_data owner keys.
 */
static const char *__node_local_id(const esp_rmaker_node_t *node);

/**
 * @brief Signal observers that ``node``'s schedule details are live.
 *
 * Self node latches the global event flag; a child dispatches
 * ``BRIDGE_CHILD_EVENT_SCHED_DETAILS_RECEIVED`` so child-side waiters
 * (``wait_on_sched_details``) can gate on rehydrated schedules.
 */
static void __signal_details_received(const esp_rmaker_node_t *node);

/* --- Update work-queue task --- */

typedef struct {
    const esp_rmaker_node_t *node;
    char *data;
    char *bridge_local_id; /* heap copy, NULL = self */
} __update_details_arg_t;

static void __update_details_work_queue_task(void *arg);

/* --- Timezone change event handling --- */

/**
 * @brief Per-node visitor for rescheduling all schedules in a node.
 * @param[in] node The node to reschedule.
 * @param[in] priv The private data.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __reschedule_node_visitor(const esp_rmaker_node_t *node, void *priv);

/**
 * @brief Reschedule all schedules in the event of a timezone change.
 * @param[in] arg unused
 */
static void __reschedule_all(void *arg);

/**
 * @brief Event handler for timezone change.
 * @param[in] event_handler_arg The event handler argument.
 * @param[in] event_base The event base.
 * @param[in] event_id The event id.
 * @param[in] event_data The event data.
 */
static void __on_timezone_change_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data);

/**
 * @brief Visitor that releases each node's handles from esp_schedule,
 *        leaving the persisted details JSON in place. Used by deinit and by
 *        on_start, so a re-init rebuilds from the stored JSON.
 */
static esp_rmaker_error_t __unload_visitor(const esp_rmaker_node_t *node, void *priv);

/* Private function definitions *******************************************************/

/* --- Node helpers --- */

static node_schedule_state_t *__node_sched(const esp_rmaker_node_t *node)
{
    return node ? &((_esp_rmaker_node_t *)node)->schedule : NULL;
}

static const esp_rmaker_node_t *__resolve_node(const char *node_key)
{
    if (!node_key || node_key[0] == '\0') {
        return esp_rmaker_get_node();
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_find_by_nvs_key(node_key);
    if (child) {
        return bridge_internal_child_node(child);
    }
#endif
    return NULL;
}

/* --- Schedule name derivation --- */

static esp_rmaker_error_t __derive_schedule_name(const char *local_id, const char *cloud_id, char *out)
{
    if (!cloud_id || cloud_id[0] == '\0' || !out) {
        return ESP_RMAKER_INVALID_ARG;
    }
    /* Concatenate ``local_id ":" cloud_id`` into a small stack buffer. */
    char buf[256];
    size_t lid_len = (local_id && local_id[0]) ? strlen(local_id) : 0;
    size_t cid_len = strlen(cloud_id);
    if (lid_len + 1 + cid_len + 1 > sizeof(buf)) {
        OSAL_LOGE(TAG, "Identifiers too long for name derivation");
        return ESP_RMAKER_INVALID_ARG;
    }
    size_t off = 0;
    if (lid_len > 0) {
        memcpy(buf + off, local_id, lid_len);
        off += lid_len;
    }
    buf[off++] = ':';
    memcpy(buf + off, cloud_id, cid_len);
    off += cid_len;

    uint8_t hash[RMAKER_CRYPTO_SHA256_HASH_LEN];
    esp_rmaker_error_t err = esp_rmaker_crypto_gen_sha256((const uint8_t *)buf, off, hash);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    /* 7 bytes -> 14 hex chars + NUL. */
    return esp_rmaker_convert_bytes_to_hex(hash, SCHEDULE_NAME_HEX_CHARS / 2, out, SCHEDULE_NAME_BUF_SIZE);
}

/* --- Action / priv_data lifecycle --- */

static void __schedule_action_free(__schedule_action_t *action)
{
    if (!action) {
        return;
    }
    if (action->data) {
        free(action->data);
        action->data = NULL;
    }
    action->data_len = 0;
}

static __schedule_priv_data_t *__schedule_priv_data_new(const char *action_json, size_t action_json_len,
        const char *node_key, const char *cloud_id, bool one_shot)
{
    __schedule_priv_data_t *p = OSAL_CALLOC_EXTRAM(1, sizeof(__schedule_priv_data_t));
    if (!p) {
        return NULL;
    }
    if (action_json && action_json_len > 0) {
        p->action.data = OSAL_MALLOC_EXTRAM(action_json_len);
        if (!p->action.data) {
            free(p);
            return NULL;
        }
        memcpy(p->action.data, action_json, action_json_len);
        p->action.data_len = action_json_len;
    } else {
        p->action.data = NULL;
        p->action.data_len = 0;
    }
    /* node_key is a fixed-size token; copy inline. "" / NULL <=> self node. */
    if (node_key && node_key[0]) {
        strncpy(p->node_key, node_key, sizeof(p->node_key) - 1);
        p->node_key[sizeof(p->node_key) - 1] = '\0';
    }
    if (cloud_id && cloud_id[0]) {
        strncpy(p->cloud_id, cloud_id, sizeof(p->cloud_id) - 1);
        p->cloud_id[sizeof(p->cloud_id) - 1] = '\0';
    }
    p->one_shot = one_shot;
    return p;
}

static void __schedule_priv_data_free(__schedule_priv_data_t *priv)
{
    if (!priv) {
        return;
    }
    __schedule_action_free(&priv->action);
    free(priv);
}

/* --- Details-JSON serialization --- */

/* Emit one schedule object. Mirrors ::__parse_trigger field for field so the
 * result parses back to an identical config. */
static void __emit_schedule(json_gen_str_t *jptr, const esp_schedule_config_t *cfg,
                            const __schedule_priv_data_t *priv)
{
    const esp_schedule_trigger_t *t = &cfg->trigger;

    json_gen_start_object(jptr);
    json_gen_obj_set_string(jptr, "id", (char *)priv->cloud_id);
    json_gen_obj_set_bool(jptr, "enabled", true);

    json_gen_push_array(jptr, "triggers");
    json_gen_start_object(jptr);
    switch (t->type) {
    case ESP_SCHEDULE_TYPE_RELATIVE:
        json_gen_obj_set_int(jptr, "rsec", t->relative_seconds);
        break;
    case ESP_SCHEDULE_TYPE_DAYS_OF_WEEK:
        json_gen_obj_set_int(jptr, "d", t->day.repeat_days);
        json_gen_obj_set_int(jptr, "m", t->hours * 60 + t->minutes);
        break;
    case ESP_SCHEDULE_TYPE_DATE:
        json_gen_obj_set_int(jptr, "dd", t->date.day);
        json_gen_obj_set_int(jptr, "mm", t->date.repeat_months);
        /* Only a real year bound is emitted: repeat_every_year is spelled by
         * the absence of "yy", and the two are mutually exclusive. */
        if (t->date.year != 0) {
            json_gen_obj_set_int(jptr, "yy", t->date.year);
        }
        json_gen_obj_set_int(jptr, "m", t->hours * 60 + t->minutes);
        break;
#if CONFIG_ESP_SCHEDULE_ENABLE_DAYLIGHT
    case ESP_SCHEDULE_TYPE_SUNRISE:
    /* fall-through */
    case ESP_SCHEDULE_TYPE_SUNSET:
        json_gen_obj_set_float(jptr, "lat", (float)t->solar.latitude);
        json_gen_obj_set_float(jptr, "lon", (float)t->solar.longitude);
        json_gen_obj_set_int(jptr, t->type == ESP_SCHEDULE_TYPE_SUNRISE ? "sr" : "ss",
                             t->solar.offset_minutes);
        /* Solar reads exactly one day arm; emit whichever is populated. */
        if (t->day.repeat_days != 0) {
            json_gen_obj_set_int(jptr, "d", t->day.repeat_days);
        } else if (t->date.day != 0) {
            json_gen_obj_set_int(jptr, "dd", t->date.day);
            json_gen_obj_set_int(jptr, "mm", t->date.repeat_months);
            if (t->date.year != 0) {
                json_gen_obj_set_int(jptr, "yy", t->date.year);
            }
        }
        break;
#endif
    default:
        break;
    }
    /* A one-shot persists the instant it was armed for, so a replay can tell a
     * schedule that already came due from one still pending -- without it a
     * relative trigger would recompute ``now + rsec`` and fire again on every
     * boot. Read from priv_data because ``esp_schedule_get`` only fills
     * ``next_scheduled_time_utc`` for RELATIVE triggers, whereas the timestamp
     * callback supplies it for every type.
     *
     * Deliberately not emitted for repeating triggers: their next-fire moves on
     * every arm, so emitting it would make the serialized form differ on every
     * boot and cost a flash write each time (see ::__reload_for_node). */
    if (priv->one_shot && priv->next_fire_ts != 0) {
        json_gen_obj_set_int(jptr, "ts", (int)priv->next_fire_ts);
    }
    json_gen_end_object(jptr);
    json_gen_pop_array(jptr);

    if (cfg->validity.start_time != 0 || cfg->validity.end_time != 0) {
        json_gen_push_object(jptr, "validity");
        json_gen_obj_set_int(jptr, "start", (int)cfg->validity.start_time);
        json_gen_obj_set_int(jptr, "end", (int)cfg->validity.end_time);
        json_gen_pop_object(jptr);
    }

    /* The action is stored verbatim, so splice it in as raw JSON. */
    if (priv->action.data) {
        json_gen_push_object_str(jptr, "action", priv->action.data);
    }

    json_gen_end_object(jptr);
}

/* One serialization pass. Returns the required buffer size, or -1 on error. */
static int __serialize_pass(const esp_rmaker_node_t *node, char *buf, size_t buf_size)
{
    node_schedule_state_t *state = __node_sched(node);
    json_gen_str_t jstr;

    json_gen_str_start(&jstr, buf, buf_size, NULL, NULL);
    json_gen_start_array(&jstr);
    if (state && state->handles) {
        for (uint8_t i = 0; i < state->count; i++) {
            esp_schedule_config_t cfg = {0};
            if (!state->handles[i] || esp_schedule_get(state->handles[i], &cfg) != ESP_OK) {
                continue;
            }
            const __schedule_priv_data_t *priv = (const __schedule_priv_data_t *)cfg.priv_data;
            if (!priv || priv->cloud_id[0] == '\0') {
                /* Without the cloud id the entry cannot be named back to the
                 * cloud, so it cannot be persisted meaningfully. */
                OSAL_LOGW(TAG, "Skipping schedule with no cloud id during serialization");
                continue;
            }
            __emit_schedule(&jstr, &cfg, priv);
        }
    }
    json_gen_end_array(&jstr);
    return json_gen_str_end(&jstr);
}

static char *__serialize_node_schedules_locked(const esp_rmaker_node_t *node)
{
    if (!node) {
        return NULL;
    }
    /* Two-pass: NULL/0 to size, then alloc and emit. */
    int req_size = __serialize_pass(node, NULL, 0);
    if (req_size < 0) {
        OSAL_LOGE(TAG, "Failed to size the schedule details JSON");
        return NULL;
    }
    char *out = OSAL_CALLOC_EXTRAM(1, (size_t)req_size);
    if (!out) {
        OSAL_LOGE(TAG, "Failed to allocate %d bytes for schedule details", req_size);
        return NULL;
    }
    if (__serialize_pass(node, out, (size_t)req_size) < 0) {
        OSAL_LOGE(TAG, "Failed to emit the schedule details JSON");
        free(out);
        return NULL;
    }
    return out;
}

static esp_rmaker_error_t __reserialize_and_persist(const esp_rmaker_node_t *node)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_node_lock(node);
    char *json = __serialize_node_schedules_locked(node);
    esp_rmaker_node_unlock(node);
    if (!json) {
        return ESP_RMAKER_FAIL;
    }
    esp_rmaker_error_t err = __persist_details(node, json);
    free(json);
    return err;
}

/* --- Details-JSON persistence --- */

static esp_rmaker_error_t __persist_details(const esp_rmaker_node_t *node, const char *data)
{
    if (!node || !data) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (esp_rmaker_node_is_self(node)) {
        return esp_rmaker_local_config_set_sched_details(data);
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
    return child ? bridge_child_scheds_nvs_set(child, data) : ESP_RMAKER_NOT_FOUND;
#else
    return ESP_RMAKER_NOT_FOUND;
#endif
}

static char *__load_details(const esp_rmaker_node_t *node)
{
    if (!node) {
        return NULL;
    }
    if (esp_rmaker_node_is_self(node)) {
        return esp_rmaker_local_config_get_sched_details();
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
    return child ? bridge_child_scheds_nvs_get(child) : NULL;
#else
    return NULL;
#endif
}

static void __erase_details(const esp_rmaker_node_t *node)
{
    if (!node) {
        return;
    }
    if (esp_rmaker_node_is_self(node)) {
        /* An empty array is the "no schedules" payload; storing it keeps the
         * key present and consistent with what the cloud would push. */
        (void)esp_rmaker_local_config_set_sched_details("[]");
        return;
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
    if (child) {
        (void)bridge_child_scheds_nvs_erase(child);
    }
#endif
}

static void __invalidate_sched_version(const esp_rmaker_node_t *node)
{
    if (!node) {
        return;
    }
    if (esp_rmaker_node_is_self(node)) {
        (void)esp_rmaker_local_config_set_sched_ver(-1);
        return;
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
    if (child) {
        (void)bridge_child_nvs_set_sched_ver(child, -1);
    }
#endif
}

/* --- Per-node handle list ops --- */

static bool __schedule_is_spent(const __schedule_priv_data_t *priv)
{
    if (!priv || !osal_timesync_is_synced()) {
        return false;
    }
    return priv->next_fire_ts == 0;
}

static void __node_release_locked(const esp_rmaker_node_t *node)
{
    node_schedule_state_t *s = __node_sched(node);
    if (!s || !s->handles) {
        return;
    }
    for (uint8_t i = 0; i < s->count; i++) {
        __release_handle(s->handles[i]);
    }
    free(s->handles);
    s->handles = NULL;
    s->count = 0;
}

static void __release_handle(esp_schedule_handle_t h)
{
    if (!h) {
        return;
    }
    /* esp_schedule_get hands back the live priv_data pointer we attached on
     * create. It is copied out by value, so the pointer stays ours to free
     * after the handle is gone. The config is zero-initialized and
     * esp_schedule_get writes nothing on failure, so a NULL priv_data covers
     * both "read failed" and "none attached" without depending on the
     * component's error enum. */
    esp_schedule_config_t cfg = {0};
    (void)esp_schedule_get(h, &cfg);

    /* Delete first, free second. ``esp_schedule_delete`` tears the timer down
     * through the port's ``cancel``, which barriers against a callback already
     * running on another task -- and this priv_data is exactly what that
     * callback was handed. Freeing before the barrier leaves
     * ::__schedule_trigger_callback reading ``action.data`` out of freed
     * memory, which is the same cloud-push-replaces-schedules race the barrier
     * exists to close for the ``esp_schedule_t`` itself. */
    esp_schedule_delete(h);
    if (cfg.priv_data) {
        __schedule_priv_data_free((__schedule_priv_data_t *)cfg.priv_data);
    }
}

static esp_rmaker_error_t __node_append_handle_locked(const esp_rmaker_node_t *node, esp_schedule_handle_t handle)
{
    node_schedule_state_t *s = __node_sched(node);
    if (!s || !handle) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_schedule_handle_t *grown = OSAL_REALLOC_EXTRAM(s->handles, sizeof(esp_schedule_handle_t) * (s->count + 1));
    if (!grown) {
        OSAL_LOGE(TAG, "Failed to grow schedule handle array");
        return ESP_RMAKER_NO_MEM;
    }
    s->handles = grown;
    s->handles[s->count] = handle;
    s->count++;
    return ESP_RMAKER_OK;
}

static bool __node_remove_by_cloud_id_locked(const esp_rmaker_node_t *node, const char *cloud_id)
{
    node_schedule_state_t *s = __node_sched(node);
    if (!s || !s->handles || !cloud_id || !cloud_id[0]) {
        return false;
    }
    for (uint8_t i = 0; i < s->count; i++) {
        esp_schedule_config_t cfg = {0};
        if (!s->handles[i] || esp_schedule_get(s->handles[i], &cfg) != ESP_OK) {
            continue;
        }
        const __schedule_priv_data_t *priv = (const __schedule_priv_data_t *)cfg.priv_data;
        if (!priv || strcmp(priv->cloud_id, cloud_id) != 0) {
            continue;
        }
        /* Safe here, unlike on the fire path: the caller reached this from an
         * arm we drove ourselves, not from inside the schedule's callback. */
        __release_handle(s->handles[i]);
        /* Compact in place; order carries no meaning. */
        for (uint8_t j = i; j + 1 < s->count; j++) {
            s->handles[j] = s->handles[j + 1];
        }
        s->count--;
        if (s->count == 0) {
            free(s->handles);
            s->handles = NULL;
        }
        return true;
    }
    return false;
}

/* --- Trigger dispatch --- */

static void __schedule_fire_task(void *arg)
{
    __schedule_priv_data_t *priv = (__schedule_priv_data_t *)arg;
    if (!priv) {
        return;
    }
    /* Runtime gate: don't apply a fired schedule against node memory while
     * stopping/stopped/resetting. */
    if (!esp_rmaker_should_do_work()) {
        __schedule_priv_data_free(priv);
        return;
    }
    const esp_rmaker_node_t *node = __resolve_node(priv->node_key);
    if (node && priv->action.data) {
        data_model_state_handle_update_payload_json(
            node, priv->action.data, priv->action.data_len, ESP_RMAKER_REQ_SRC_SCHEDULE);
    } else if (!node) {
        OSAL_LOGW(TAG, "Schedule fired for unresolved node_key='%s'; dropping",
                  priv->node_key);
    }

    /* A one-shot that has fired is spent. That is known from the config alone,
     * so there is no need to read esp_schedule's state back (and no dependence
     * on it having re-armed yet): drop the schedule and rewrite the node's
     * persisted details without it, so a reboot does not resurrect it.
     *
     * The persisted schedule *version* is deliberately left alone: voiding it
     * would make the cloud re-push the expired schedule, which we would prune
     * again. The device's details intentionally diverge from the cloud's copy
     * for spent one-shots. */
    if (node && priv->one_shot && priv->cloud_id[0]) {
        esp_rmaker_node_lock(node);
        bool removed = __node_remove_by_cloud_id_locked(node, priv->cloud_id);
        esp_rmaker_node_unlock(node);
        if (removed) {
            OSAL_LOGI(TAG, "One-shot schedule '%s' fired; removed it from persistence", priv->cloud_id);
            (void)__reserialize_and_persist(node);
        }
    }

    __schedule_priv_data_free(priv);
}

static void __schedule_timestamp_callback(esp_schedule_handle_t handle, uint32_t next_timestamp,
        void *priv_data)
{
    (void)handle;
    __schedule_priv_data_t *priv = (__schedule_priv_data_t *)priv_data;
    if (priv) {
        priv->next_fire_ts = (time_t)next_timestamp;
    }
}

static void __schedule_trigger_callback(esp_schedule_handle_t handle, void *priv_data)
{
    (void)handle;
    __schedule_priv_data_t *src = (__schedule_priv_data_t *)priv_data;
    if (!src || !src->action.data) {
        OSAL_LOGE(TAG, "Trigger callback got empty priv_data");
        return;
    }
    /* Deep-copy the live priv_data so an edit/delete on the source schedule
     * doesn't free bytes the work task is about to read. */
    __schedule_priv_data_t *copy = __schedule_priv_data_new(
                                       src->action.data,
                                       src->action.data_len,
                                       src->node_key,
                                       src->cloud_id,
                                       src->one_shot);
    if (!copy) {
        OSAL_LOGE(TAG, "Failed to copy priv_data for fire");
        return;
    }
    if (esp_rmaker_work_queue_add_task(__schedule_fire_task, copy) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to enqueue schedule fire");
        __schedule_priv_data_free(copy);
    }
}

/* --- JSON parsing --- */

static esp_rmaker_error_t __parse_action(jparse_ctx_t *jctx, __schedule_action_t *action)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    int data_len = 0;
    json_obj_get_object_strlen(jctx, "action", &data_len);
    if (data_len <= 0) {
        OSAL_LOGD(TAG, "Action not found in JSON");
        err = ESP_RMAKER_INVALID_ARG;
        goto __parse_action_fail;
    }
    action->data_len = (size_t)data_len + 1;
    action->data = (char *)OSAL_CALLOC_EXTRAM(1, action->data_len);
    if (!action->data) {
        OSAL_LOGE(TAG, "Could not allocate schedule action data");
        err = ESP_RMAKER_NO_MEM;
        goto __parse_action_fail;
    }
    if (json_obj_get_object_str(jctx, "action", action->data, action->data_len) != 0) {
        err = ESP_RMAKER_INVALID_ARG;
        goto __parse_action_fail;
    }
    return ESP_RMAKER_OK;

__parse_action_fail:
    __schedule_action_free(action);
    return err;
}

static esp_rmaker_error_t __parse_trigger(jparse_ctx_t *jctx, esp_schedule_trigger_t *trigger,
        time_t *out_ts)
{
    int total_triggers = 0;
    if (out_ts) {
        *out_ts = 0;
    }
    if (json_obj_get_array(jctx, "triggers", &total_triggers) != 0) {
        OSAL_LOGD(TAG, "Triggers not found in JSON");
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = ESP_RMAKER_INVALID_ARG;
    memset(trigger, 0, sizeof(esp_schedule_trigger_t));
    trigger->type = ESP_SCHEDULE_TYPE_INVALID;

    if (total_triggers <= 0) {
        OSAL_LOGD(TAG, "No triggers found in trigger array");
        goto __parse_trigger_end;
    }
    if (total_triggers > 1) {
        /* esp_schedule holds one trigger per schedule; the cloud array can
         * carry more. Honour index 0 and drop the rest. */
        OSAL_LOGW(TAG, "Got %d triggers but only one per schedule is supported; using index 0 and dropping %d",
                  total_triggers, total_triggers - 1);
    }
    if (json_arr_get_object(jctx, 0) != 0) {
        OSAL_LOGD(TAG, "Trigger 0 is not an object");
        goto __parse_trigger_end;
    }

    int repeat_days = 0, day = 0, repeat_months = 0, year = 0;

    /* A "ts" we wrote on a previous run: the instant that run armed this
     * schedule for. Reported to the caller for every trigger type, but only fed
     * back into the trigger for RELATIVE, which is the one type esp_schedule
     * consults it for (what it would make of a pre-set timestamp on the others
     * is its business, not something to lean on). */
    int64_t persisted_ts = 0;
    bool have_ts = (json_obj_get_int64(jctx, "ts", &persisted_ts) == 0);
    if (have_ts && persisted_ts > 0 && out_ts) {
        *out_ts = (time_t)persisted_ts;
    }

    /* Relative trigger */
    if (json_obj_get_int(jctx, "rsec", &trigger->relative_seconds) == 0) {
        trigger->type = ESP_SCHEDULE_TYPE_RELATIVE;
        if (have_ts) {
            trigger->next_scheduled_time_utc = (time_t)persisted_ts;
        }
        err = ESP_RMAKER_OK;
        goto __parse_trigger_leave_object;
    }

    /* Check for days of week */
    bool have_d = (json_obj_get_int(jctx, "d", &repeat_days) == 0);
    if (have_d) {
        trigger->type = ESP_SCHEDULE_TYPE_DAYS_OF_WEEK;
        trigger->day.repeat_days = (uint8_t)repeat_days;
    }

    /* Check for date (which overrides days of week).
     *
     * esp_schedule keeps the weekday and date arms strictly exclusive: a
     * trigger carrying both is rejected outright, and the "(weekdays OR the
     * Nth) of month-set" union the cloud payload can express is not
     * representable. The date arm is the more specific of the two, so it
     * wins and the weekday arm is cleared.
     *
     * ``dd`` (day of month) is what anchors the arm: ``mm`` without it means
     * "every day of those months", which esp_schedule rejects, and ``yy``
     * without it has no pattern to bound. Both are ignored in that case.
     *
     * Recurrence mapping (see esp_schedule docs/trigger_rules.md 3.1):
     * ``mm`` absent means "every month" for the cloud, which is a full month
     * mask here, not an absent one -- an absent mask makes the arm one-shot.
     * ``yy`` absent means "every year" (repeat_every_year), which esp_schedule
     * requires a month mask to recur over; ``yy`` present bounds the arm to
     * that year instead and is mutually exclusive with repeat_every_year. */
    bool have_dd = (json_obj_get_int(jctx, "dd", &day) == 0);
    bool have_mm = (json_obj_get_int(jctx, "mm", &repeat_months) == 0);
    bool have_yy = (json_obj_get_int(jctx, "yy", &year) == 0);
    if (have_dd) {
        if (have_d) {
            OSAL_LOGW(TAG, "Trigger carries both 'd' and 'dd'; the weekday and date arms are exclusive, using the date arm and dropping 'd'");
        }
        trigger->type = ESP_SCHEDULE_TYPE_DATE;
        trigger->day.repeat_days = 0;
        trigger->date.day = (uint8_t)day;
        /* No month mask from the cloud means every month, not "no recurrence". */
        trigger->date.repeat_months = have_mm ? (uint16_t)repeat_months
                                      : (uint16_t)ESP_SCHEDULE_MONTH_ALL;
        /* A year of 0 does not bound anything, so treat it as absent. */
        trigger->date.year = (have_yy && year > 0) ? (uint16_t)year : 0;
        /* Unbounded recurrence needs a month mask to recur over. */
        trigger->date.repeat_every_year = (trigger->date.year == 0) &&
                                          (trigger->date.repeat_months != 0);
    } else if (have_mm || have_yy) {
        OSAL_LOGW(TAG, "Trigger has 'mm'/'yy' without 'dd'; ignoring them as there is no date pattern to apply them to");
    }

#if CONFIG_ESP_SCHEDULE_ENABLE_DAYLIGHT
    /* Check for solar trigger via mandatory 'lat' and 'lon' fields */
    float lat, lon;
    if ((json_obj_get_float(jctx, "lat", &lat) == 0) &&
            (json_obj_get_float(jctx, "lon", &lon) == 0)) {
        trigger->solar.latitude = lat;
        trigger->solar.longitude = lon;
        int offset_minutes = 0;
        if (json_obj_get_int(jctx, "sr", &offset_minutes) == 0) {
            trigger->type = ESP_SCHEDULE_TYPE_SUNRISE;
            trigger->solar.offset_minutes = offset_minutes;
        } else if (json_obj_get_int(jctx, "ss", &offset_minutes) == 0) {
            trigger->type = ESP_SCHEDULE_TYPE_SUNSET;
            trigger->solar.offset_minutes = offset_minutes;
        }
        if ((trigger->type == ESP_SCHEDULE_TYPE_SUNRISE || trigger->type == ESP_SCHEDULE_TYPE_SUNSET) &&
                !have_d && !have_dd) {
            /* A solar trigger with no date constraints at all fires on every
             * valid day within the schedule's validity window. esp_schedule
             * reads an all-zero day arm as a wildcard that fires *once*, so
             * ask for every day explicitly. An explicit 'd' is left alone --
             * including ``"d": 0``, which is a deliberate one-shot. */
            trigger->day.repeat_days = ESP_SCHEDULE_DAY_EVERYDAY;
        }
    }
#endif

    /* DAYS_OF_WEEK and DATE triggers require trigger time to be set, so translate to hours and minutes */
    if (trigger->type == ESP_SCHEDULE_TYPE_DAYS_OF_WEEK ||
            trigger->type == ESP_SCHEDULE_TYPE_DATE) {
        int minutes_since_midnight = 0;
        if (json_obj_get_int(jctx, "m", &minutes_since_midnight) != 0) {
            OSAL_LOGD(TAG, "Dropping trigger due to missing minutes since midnight for days of week or date trigger");
            memset(trigger, 0, sizeof(esp_schedule_trigger_t));
            goto __parse_trigger_leave_object;
        }
        if (minutes_since_midnight < 0 || minutes_since_midnight >= 24 * 60) {
            OSAL_LOGE(TAG, "Dropping trigger with out-of-range minutes since midnight %d (expected 0-1439)",
                      minutes_since_midnight);
            memset(trigger, 0, sizeof(esp_schedule_trigger_t));
            goto __parse_trigger_leave_object;
        }

        // translate to hours and minutes
        trigger->hours = (uint8_t)(minutes_since_midnight / 60);
        trigger->minutes = (uint8_t)(minutes_since_midnight % 60);

        err = ESP_RMAKER_OK;
        goto __parse_trigger_leave_object;
    }

    /* If the trigger type is invalid, log an error and drop the trigger */
    if (trigger->type == ESP_SCHEDULE_TYPE_INVALID) {
        OSAL_LOGD(TAG, "Dropping trigger due to invalid trigger type");
        memset(trigger, 0, sizeof(esp_schedule_trigger_t));
        goto __parse_trigger_leave_object;
    }

    /* Solar trigger: no minutes-since-midnight requirement. */
    err = ESP_RMAKER_OK;

__parse_trigger_leave_object:
    json_arr_leave_object(jctx);
__parse_trigger_end:
    json_obj_leave_array(jctx);
    return err;
}

static bool __trigger_is_one_shot(const esp_schedule_trigger_t *trigger)
{
    switch (trigger->type) {
    case ESP_SCHEDULE_TYPE_RELATIVE:
        return true; /* relative triggers fire exactly once */
    case ESP_SCHEDULE_TYPE_DAYS_OF_WEEK:
        return trigger->day.repeat_days == ESP_SCHEDULE_DAY_ONCE;
    case ESP_SCHEDULE_TYPE_DATE:
        /* The month mask is what a date arm recurs over; with no mask it fires
         * once, whatever the year says. */
        return trigger->date.repeat_months == 0;
#if CONFIG_ESP_SCHEDULE_ENABLE_DAYLIGHT
    case ESP_SCHEDULE_TYPE_SUNRISE:
    /* fall-through */
    case ESP_SCHEDULE_TYPE_SUNSET:
        /* Solar picks one day arm: a weekday mask repeats forever, otherwise
         * the date arm's rule applies. */
        if (trigger->day.repeat_days != 0) {
            return false;
        }
        return trigger->date.repeat_months == 0;
#endif
    default:
        /* An unusable trigger never fires; treat it as spent rather than
         * something to keep around. */
        return true;
    }
}

static esp_rmaker_error_t __parse_validity(jparse_ctx_t *jctx, esp_schedule_validity_t *validity)
{
    if (json_obj_get_object(jctx, "validity") == 0) {
        int64_t start = 0;
        int64_t end = 0;
        if (json_obj_get_int64(jctx, "start", &start) != 0) {
            start = 0;
        }
        if (json_obj_get_int64(jctx, "end", &end) != 0) {
            end = 0;
        }
        validity->start_time = (time_t)start;
        validity->end_time = (time_t)end;
        json_obj_leave_object(jctx);
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __build_schedule_details_for_node_locked(
    const esp_rmaker_node_t *node, const char *local_id, const char *data, size_t data_len)
{
    if (!node || !data || data_len == 0) {
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Owner routing token persisted in each schedule's priv_data: the
     * child's fixed NVS key (or "" for self). ``local_id`` itself stays
     * in use below only for the (local_id ":" cloud_id) name derivation. */
    char node_key[RMAKER_NVS_KEY_LEN_MAX + 1] = {0};
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    if (local_id && local_id[0]) {
        if (bridge_internal_compute_nvs_key(local_id, node_key, sizeof(node_key)) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to derive owner NVS key for local_id='%s'", local_id);
            return ESP_RMAKER_FAIL;
        }
    }
#endif

    /* Drop the node's current schedules wholesale. */
    __node_release_locked(node);

    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, (char *)data, data_len) != 0) {
        OSAL_LOGE(TAG, "Json parse start failed");
        return ESP_RMAKER_FAIL;
    }

    int schedule_count = 0;
    while (json_arr_get_object(&jctx, schedule_count) == 0) {
        schedule_count++;
        json_arr_leave_object(&jctx);
    }

    if (schedule_count > MAX_SCHEDULES_PER_NODE) {
        OSAL_LOGW(TAG, "Received %d schedules; clamping to MAX_SCHEDULES_PER_NODE (%d)",
                  schedule_count, MAX_SCHEDULES_PER_NODE);
        schedule_count = MAX_SCHEDULES_PER_NODE;
    }

    int current_schedule = 0;
    while (current_schedule < schedule_count && json_arr_get_object(&jctx, current_schedule) == 0) {
        esp_schedule_config_t schedule_config = {0};
        char cloud_id[MAX_SCHEDULE_NAME_LEN + 1] = {0};

        bool enabled = true;
        json_obj_get_bool(&jctx, "enabled", &enabled);
        if (!enabled) {
            OSAL_LOGD(TAG, "Schedule %d is disabled. Skipping.", current_schedule);
            goto cleanup;
        }

        json_obj_get_string(&jctx, "id", cloud_id, sizeof(cloud_id));
        if (cloud_id[0] == '\0') {
            OSAL_LOGE(TAG, "Schedule %d has no valid id (missing, empty, non-string, or too long)", current_schedule);
            goto cleanup;
        }

        /* Derive a namespaced NVS-safe name from (local_id, cloud_id). */
        if (__derive_schedule_name(local_id, cloud_id, schedule_config.name) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to derive schedule NVS-safe name for id='%s'", cloud_id);
            goto cleanup;
        }
        OSAL_LOGI(TAG, "Derived schedule NVS-safe name for id='%s' -> %s", cloud_id, schedule_config.name);

        /* Get the trigger */
        time_t persisted_fire_ts = 0;
        if (__parse_trigger(&jctx, &schedule_config.trigger, &persisted_fire_ts) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to parse trigger for schedule '%s'", cloud_id);
            goto cleanup;
        }

        const bool one_shot = __trigger_is_one_shot(&schedule_config.trigger);

        /* A one-shot whose armed instant is already behind us is spent: it either
         * fired and the write that removed it was lost, or it came due while the
         * device was powered off. Either way it must not be recreated, and there
         * is no point building a handle just to prune it. Only ever true when
         * replaying our own payload -- the cloud never sends "ts". */
        if (one_shot && persisted_fire_ts != 0) {
            time_t now = 0;
            time(&now);
            if (osal_timesync_is_synced() && persisted_fire_ts <= now) {
                OSAL_LOGI(TAG, "One-shot schedule '%s' already came due; not restoring it", cloud_id);
                goto cleanup;
            }
        }

        /* Get validity */
        if (__parse_validity(&jctx, &schedule_config.validity) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to parse validity for schedule '%s'", cloud_id);
            goto cleanup;
        }

        /* Allocate a new priv_data container and parse the action into it. */
        __schedule_priv_data_t *priv = __schedule_priv_data_new(
                                           NULL, 0, node_key, cloud_id, one_shot);
        if (!priv) {
            OSAL_LOGE(TAG, "Failed to build schedule priv_data for '%s'", cloud_id);
            goto cleanup;
        }
        /* Parse the action into the priv_data */
        if (__parse_action(&jctx, &priv->action) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to parse action for schedule '%s'", cloud_id);
            __schedule_priv_data_free(priv);
            goto cleanup;
        }
        schedule_config.priv_data = priv;
        schedule_config.trigger_cb = __schedule_trigger_callback;
        schedule_config.timestamp_cb = __schedule_timestamp_callback;

        esp_schedule_handle_t handle = esp_schedule_create(&schedule_config);
        if (!handle) {
            OSAL_LOGE(TAG, "Failed to create schedule '%s'", cloud_id);
            __schedule_priv_data_free(priv);
            goto cleanup;
        }
        /* Arm before taking ownership: if the schedule turns out to have no
         * future occurrence there is nothing worth keeping, and dropping it
         * here means it never enters the node's list and never reaches the
         * re-serialized payload. That is what prunes an expired one-shot on the
         * boot after it fired, or one whose window closed while powered off. */
        if (__arm_or_defer(handle, priv) && __schedule_is_spent(priv)) {
            OSAL_LOGI(TAG, "Schedule '%s' has no future occurrence; dropping it", cloud_id);
            __release_handle(handle);
            goto cleanup;
        }
        if (__node_append_handle_locked(node, handle) != ESP_RMAKER_OK) {
            /* Already logged; drop the handle + priv_data we just attached. */
            __release_handle(handle);
            goto cleanup;
        }
        OSAL_LOGD(TAG, "Created schedule '%s' (handle %p) for %s",
                  cloud_id, handle, (local_id && local_id[0]) ? local_id : "self");

cleanup:
        json_arr_leave_object(&jctx);
        current_schedule++;
    }

    json_parse_end(&jctx);
    return ESP_RMAKER_OK;
}

static const char *__node_local_id(const esp_rmaker_node_t *node)
{
    if (!node || esp_rmaker_node_is_self(node)) {
        return NULL;
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
    if (child) {
        const char *lid = bridge_internal_child_local_id(child);
        if (lid && lid[0]) {
            return lid;
        }
    }
#endif
    return NULL;
}

static void __signal_details_received(const esp_rmaker_node_t *node)
{
    if (!node || esp_rmaker_node_is_self(node)) {
        esp_rmaker_event_flags_set_sched_details_received();
        return;
    }
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
    if (child) {
        bridge_internal_dispatch_child_event(child, BRIDGE_CHILD_EVENT_SCHED_DETAILS_RECEIVED);
    }
#endif
}

/* --- Update work-queue task --- */

static void __update_details_work_queue_task(void *arg_in)
{
    __update_details_arg_t *arg = (__update_details_arg_t *)arg_in;
    if (!arg) {
        return;
    }
    const esp_rmaker_node_t *node = arg->node;
    char *data = arg->data;
    const char *local_id = arg->bridge_local_id; /* NULL = self */

    esp_rmaker_error_t err = ESP_RMAKER_FAIL;
    if (node && data) {
        esp_rmaker_node_lock(node);
        err = __build_schedule_details_for_node_locked(node, local_id, data, strlen(data));
        esp_rmaker_node_unlock(node);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to install schedule details: %d", err);
        } else {
            /* Persist what we actually installed -- re-serialized from the
             * live handles, not the raw cloud payload. That normalises the
             * arms we chose, drops entries we refused, and records each
             * trigger's computed next-fire timestamp so a relative one-shot
             * cannot fire again after a reboot.
             *
             * Done only after a successful build, so a payload we could not
             * apply is never stored. A persistence failure is not fatal for the
             * live schedules, but the version handshake would then believe the
             * details are stored: void the version so the cloud re-pushes on
             * the next connect. */
            esp_rmaker_error_t perr = __reserialize_and_persist(node);
            if (perr != ESP_RMAKER_OK) {
                OSAL_LOGE(TAG, "Failed to persist schedule details (%d); voiding version to refetch", perr);
                __invalidate_sched_version(node);
            }
        }
    }

    /* Signal completion only after the install finishes, so waiters don't race
     * ahead of the live schedule list. */
    __signal_details_received(node);

    if (data) {
        free(data);
    }
    if (arg->bridge_local_id) {
        free(arg->bridge_local_id);
    }
    free(arg);
}

/* --- Timezone change event handling --- */

static esp_rmaker_error_t __reschedule_node_visitor(const esp_rmaker_node_t *node, void *priv)
{
    (void)priv;
    node_schedule_state_t *s = __node_sched(node);
    if (!s || s->count == 0) {
        return ESP_RMAKER_OK;
    }
    bool pruned = false;
    esp_rmaker_node_lock(node);
    /* Walk backwards: a prune compacts the array, and counting down keeps the
     * remaining indices valid. */
    for (uint8_t i = s->count; i > 0; i--) {
        esp_schedule_handle_t h = s->handles[i - 1];
        if (!h) {
            continue;
        }
        /* esp_schedule_enable recomputes the next-fire off the current wall
         * clock and timezone on every arm, so re-arming is all a timezone
         * change needs. If the re-arm finds no occurrence left, the schedule is
         * spent and goes the same way as one dropped at build time. */
        esp_schedule_config_t cfg = {0};
        if (esp_schedule_get(h, &cfg) != ESP_OK) {
            continue;
        }
        __schedule_priv_data_t *sp = (__schedule_priv_data_t *)cfg.priv_data;
        if (__arm_or_defer(h, sp) && __schedule_is_spent(sp)) {
            if (sp && sp->cloud_id[0] && __node_remove_by_cloud_id_locked(node, sp->cloud_id)) {
                OSAL_LOGI(TAG, "Schedule '%s' has no occurrence left after re-arm; removing it",
                          sp->cloud_id);
                pruned = true;
            }
        }
    }
    esp_rmaker_node_unlock(node);

    if (pruned) {
        (void)__reserialize_and_persist(node);
    }
    return ESP_RMAKER_OK;
}

static void __reschedule_all(void *arg)
{
    (void)arg;
    /* Runtime gate: don't walk nodes to reschedule while stopping/stopped/resetting. */
    if (!esp_rmaker_should_do_work()) {
        return;
    }
    esp_rmaker_node_for_each(__reschedule_node_visitor, NULL);
}

static void __on_timezone_change_event_handler(void *event_handler_arg, osal_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)event_handler_arg;
    (void)event_base;
    (void)event_data;
    if (event_id == RMAKER_EVENT_TZ_CHANGED) {
        /* Ignore timezone change event */
        return;
    }
    esp_rmaker_work_queue_add_task(__reschedule_all, NULL);
}

/* --- Reload path (replaces esp_schedule's NVS rehydrate) --- */

/**
 * @brief Rebuild ``node``'s schedules from its persisted details JSON.
 *
 * Nothing is materialized until the owning node exists, so this runs for the
 * self node at on_start and per child once the bridge registers it -- which
 * is why no orphan parking is needed any more.
 *
 * An unbuildable payload voids the node's schedule version so the cloud
 * re-pushes; it is not treated as a fatal boot error.
 */
static esp_rmaker_error_t __reload_for_node(const esp_rmaker_node_t *node)
{
    if (!node) {
        return ESP_RMAKER_INVALID_ARG;
    }

    char *data = __load_details(node);
    if (!data) {
        return ESP_RMAKER_OK; /* nothing stored */
    }

    esp_rmaker_node_lock(node);
    esp_rmaker_error_t err = __build_schedule_details_for_node_locked(
                                 node, __node_local_id(node), data, strlen(data));
    esp_rmaker_node_unlock(node);

    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Unreadable persisted schedule details (%d); voiding version to refetch", err);
        __invalidate_sched_version(node);
        free(data);
        return ESP_RMAKER_OK;
    }

    /* Replay may have dropped entries that can no longer fire (build-time
     * prune), so write the surviving set back. Compared first: a boot that
     * changes nothing must not spend a flash write. */
    esp_rmaker_node_lock(node);
    char *rebuilt = __serialize_node_schedules_locked(node);
    esp_rmaker_node_unlock(node);
    if (rebuilt) {
        if (strcmp(rebuilt, data) != 0) {
            OSAL_LOGI(TAG, "Persisted schedules changed on replay; rewriting them");
            (void)__persist_details(node, rebuilt);
        }
        free(rebuilt);
    }
    free(data);

    /* Mirror the cloud-install path so waiters gating on
     * ``wait_on_sched_details`` see NVS-rehydrated schedules go live. */
    __signal_details_received(node);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __unload_visitor(const esp_rmaker_node_t *node, void *priv)
{
    (void)priv;
    esp_rmaker_node_lock(node);
    __node_release_locked(node);
    esp_rmaker_node_unlock(node);
    return ESP_RMAKER_OK;
}

/* Public function definitions *******************************************************/

esp_rmaker_error_t esp_rmaker_schedule_service_init(void)
{
    /* No timesync-initialized gate: the schedule service must init
     * regardless of who owns SNTP (this SDK or an external component).
     * Arming is what needs a synced clock, and that is deferred via
     * ::__arm_or_defer until ::esp_rmaker_schedule_service_arm_all fires. */

    /* Register timezone change callback */
    esp_rmaker_error_t err = event_loop_register_timezone_change_handler(__on_timezone_change_event_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to register timezone change event handler");
        return err;
    }

    __initialized = true;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_schedule_service_deinit(void)
{
    if (!__initialized) {
        return ESP_RMAKER_OK;
    }
    /* Unregister the event handler first so no new reschedule task can be
     * queued against per-node state once we start tearing it down. */
    esp_rmaker_error_t err = event_loop_unregister_timezone_change_handler(__on_timezone_change_event_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to unregister timezone change event handler");
    }

    /* Walk every node (self + ready children) and release its handles from
     * memory. The persisted details JSON is left intact, so the next init
     * rebuilds from it. */
    esp_rmaker_node_for_each(__unload_visitor, NULL);

#if TIME_SYNC_DECOUPLED_FLOW
    __time_ready = false;
#endif
    __initialized = false;
    return err;
}

#if TIME_SYNC_DECOUPLED_FLOW
void esp_rmaker_schedule_service_arm_all(void)
{
    if (!__initialized) {
        OSAL_LOGW(TAG, "Schedule service not initialized; ignoring arm_all.");
        return;
    }
    /* Time is now valid: latch it so future create/rehydrate paths arm
     * inline, then re-arm every enabled handle across all nodes. The
     * visitor calls __arm_or_defer, and esp_schedule_enable recomputes each
     * trigger's next-fire off the (now synced) wall clock, so with
     * __time_ready set every handle arms with a correct fire time. */
    __time_ready = true;
    esp_rmaker_node_for_each(__reschedule_node_visitor, NULL);
}
#endif /* TIME_SYNC_DECOUPLED_FLOW */

esp_rmaker_error_t esp_rmaker_schedule_service_on_start(void)
{
    if (!__initialized) {
        OSAL_LOGW(TAG, "Schedule service is not initialized. Ignoring on start.");
        return ESP_RMAKER_OK;
    }

    /* Make sure any stale memory handles from a previous run are released
     * before we rebuild from the persisted details. */
    esp_rmaker_node_for_each(__unload_visitor, NULL);

    /* esp_schedule runs with NVS disabled: this service persists the details
     * JSON itself (see the file header), because esp_schedule stores only the
     * trigger config and not the action a fired schedule has to apply. */
    /* Initialized against the SDK's own port rather than esp_schedule_init(),
     * which keeps schedule timers on osal_scheduler (so firmware tests can drive
     * them from the virtual scheduler) and keeps the component's built-in
     * ESP-IDF port out of the link.
     *
     * Returns a handle list only when NVS is enabled; the port supplies no
     * storage ops and NVS is off, so there is nothing to take ownership of. */
    (void)esp_schedule_init_with_config(esp_schedule_port_osal_get(),
                                        false /* enable_nvs */, NULL, NULL);

    /* Rebuild the self node from its stored payload. Bridge children are not
     * in the pool yet (they arrive after MQTT is up), so each one is reloaded
     * by ::esp_rmaker_schedule_service_on_child_added instead. */
    (void)__reload_for_node(esp_rmaker_get_node());

    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_schedule_service_update_details_for_node(const esp_rmaker_node_t *node, const char *data)
{
    /* Validate inputs before the initialization gate so callers always get
     * INVALID_ARG for NULL inputs. */
    if (!node || !data) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (!__initialized) {
        OSAL_LOGW(TAG, "Schedule service is not initialized. Ignoring update details.");
        return ESP_RMAKER_OK;
    }

    __update_details_arg_t *arg = OSAL_CALLOC_EXTRAM(1, sizeof(__update_details_arg_t));
    if (!arg) {
        OSAL_LOGE(TAG, "Failed to allocate update arg");
        return ESP_RMAKER_NO_MEM;
    }
    arg->node = node;
    arg->data = OSAL_STRDUP_EXTRAM(data);
    if (!arg->data) {
        OSAL_LOGE(TAG, "Failed to duplicate schedule details");
        free(arg);
        return ESP_RMAKER_NO_MEM;
    }
    /* Snapshot the owning local_id once at queue time so we don't have to
     * re-walk the bridge pool inside the work task. NULL <=> self. */
    if (!esp_rmaker_node_is_self(node)) {
#ifdef CONFIG_RMNG_BRIDGE_ENABLED
        esp_rmaker_bridge_child_handle_t child = bridge_internal_child_from_node(node);
        if (child) {
            const char *lid = bridge_internal_child_local_id(child);
            if (lid && lid[0]) {
                arg->bridge_local_id = OSAL_STRDUP_EXTRAM(lid);
                if (!arg->bridge_local_id) {
                    OSAL_LOGE(TAG, "Failed to duplicate child local_id");
                    free(arg->data);
                    free(arg);
                    return ESP_RMAKER_NO_MEM;
                }
            }
        }
#endif
        if (!arg->bridge_local_id) {
            OSAL_LOGE(TAG, "Non-self node has no resolvable local_id; dropping update");
            free(arg->data);
            free(arg);
            return ESP_RMAKER_NOT_FOUND;
        }
    }

    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(__update_details_work_queue_task, arg);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to enqueue schedule update task");
        if (arg->bridge_local_id) {
            free(arg->bridge_local_id);
        }
        free(arg->data);
        free(arg);
    }
    return err;
}

esp_rmaker_error_t esp_rmaker_schedule_service_update_details(const char *data)
{
    return esp_rmaker_schedule_service_update_details_for_node(esp_rmaker_get_node(), data);
}

void esp_rmaker_schedule_service_unload_node(const esp_rmaker_node_t *node)
{
    if (!node) {
        return;
    }
    /* RAM-only: release the node's handles but leave its persisted details
     * JSON in place, so the next on_start / on_child_added rebuilds them.
     * The matching erase path (drops the stored JSON too) is
     * ::esp_rmaker_schedule_service_erase_node, used by bridge_remove_child /
     * add_child rollback where the child is permanently gone. */
    esp_rmaker_node_lock(node);
    __node_release_locked(node);
    esp_rmaker_node_unlock(node);
}

void esp_rmaker_schedule_service_erase_node(const esp_rmaker_node_t *node)
{
    if (!node) {
        return;
    }
    /* RAM + NVS: release the handles and drop the stored details JSON, so
     * nothing is rebuilt on the next boot. Used when the node is permanently
     * going away (bridge_remove_child confirmed by cloud, or add_child
     * rollback after a failed ack). */
    esp_rmaker_node_lock(node);
    __node_release_locked(node);
    esp_rmaker_node_unlock(node);
    __erase_details(node);
}

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
void esp_rmaker_schedule_service_on_child_added(esp_rmaker_bridge_child_handle_t child)
{
    /* Rebuild this child's schedules from its persisted details JSON now that
     * the slot is in the pool with a resolvable node.
     *
     * on_start cannot do this: it runs before MQTT is up, and bridge children
     * only enter the pool once the remote bridge handler processes a
     * re-announce. Nor can the cloud rescue the path -- schedule details are
     * version-gated, so a child whose details were never rebuilt locally would
     * not be re-pushed. Replaying our own stored JSON here is what survives a
     * parent restart-without-reset. */
    if (!child || !__initialized) {
        return;
    }
    esp_rmaker_node_t *node = bridge_internal_child_node(child);
    if (!node) {
        return;
    }
    (void)__reload_for_node(node);
}
#endif
