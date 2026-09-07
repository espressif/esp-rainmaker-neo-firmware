/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_engine.h
 * @brief Platform-neutral mirror engine: mapping-table lookups, value
 *        transforms, lowering (RainMaker Neo data model -> Matter tree via the port)
 *        and bidirectional state sync with echo suppression.
 */

#ifndef __RM_MIRROR_ENGINE_H__
#define __RM_MIRROR_ENGINE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_rmaker_data_model.h"
#include "esp_rmaker_matter_mirror.h"
#include "esp_rmaker_node.h"
#include "esp_rmaker_val.h"

#include "rm_mirror_mapping.h"
#include "rm_mirror_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Matter paths handled by the node-scoped name binding */
#define RM_MIRROR_CLUSTER_BASIC_INFORMATION 0x0028u
#define RM_MIRROR_ATTR_NODE_LABEL           0x0005u
#define RM_MIRROR_NODE_LABEL_MAX_LEN        32
/* The endpoint correlation key, served as a read-only FixedLabel entry, and Matter's cap on its
 * value. A device id is an NVS namespace of at most 15 chars, so it always fits. */
#define RM_MIRROR_LABEL_KEY_DEVICE_ID       "rmng.device"
#define RM_MIRROR_LABEL_VALUE_MAX_LEN       16
/* Widest string a binding carries, i.e. the NodeLabel cap */
#define RM_MIRROR_STR_MAX_LEN               RM_MIRROR_NODE_LABEL_MAX_LEN

/** What a binding connects, and how. */
typedef enum {
    RM_MIRROR_BINDING_SCALAR = 0,  /* param <-> attribute, Matter-domain int32 */
    RM_MIRROR_BINDING_STR_ATTR,    /* string param <-> the capability's attribute */
    RM_MIRROR_BINDING_COMPOSITE,   /* N params <-> M attributes via a conversion */
} rm_mirror_binding_kind_t;

/** Binding sync-state flags. Guarded by the engine lock. */
typedef enum {
    RM_MIRROR_F_LAST_VALID      = 1u << 0,  /* `last` holds a known Matter-side value */
    RM_MIRROR_F_COMMITTED_VALID = 1u << 1,  /* `committed` holds a value: the Matter one (scalar), the RainMaker Neo ones (composite) */
    RM_MIRROR_F_PENDING         = 1u << 2,  /* the committed value has not reached the app yet (composite: convert `last` and inject) */
    RM_MIRROR_F_ECHO_0          = 1u << 3,  /* bits 3..6, one per slot: `echo[i]` holds an outbound write in flight */
    RM_MIRROR_F_IDENTIFY_PENDING = 1u << 7,  /* `identify_seconds` waits for the next drain; set on the endpoint's first binding only */
} rm_mirror_binding_flag_t;

#define RM_MIRROR_F_ECHO(i) (RM_MIRROR_F_ECHO_0 << (i))

/** One tail slot: a bound param handle, or one value. */
typedef union {
    const esp_rmaker_param_t *param;
    esp_rmaker_val_t val;
} rm_mirror_slot_t;

/**
 * @brief One binding of any kind, plus its sync/echo state.
 *
 * The tail is four regions:
 *
 *   [ params n_params ][ last n_slots ][ echo n_slots ][ committed n_params ]
 *
 * `params` holds param handles, fixed at registration. The rest hold values:
 *
 *  - `last` (F_LAST_VALID): the Matter-side value as last seen, updated by every
 *    push and every inbound report, echo or not.
 *  - `echo` (F_ECHO(i)): the value of an outbound write still in flight. Equal to
 *    `last` right after a push, and separate from it once a controller writes
 *    before our own report comes back - which is what keeps the late echo from
 *    reading as user intent.
 *  - `committed` (F_COMMITTED_VALID): the app side, i.e. what the mirror has
 *    committed to the device write callbacks - delivered already, or queued for the
 *    next drain, which F_PENDING tells apart. Inbound values are judged against it
 *    rather than the param, which only catches up once an injection's write callback
 *    returns. The one region whose domain follows the kind:
 *      scalar    - the Matter value, recorded when the value is parked
 *      composite - the RainMaker Neo values a drain converted and injected, which the
 *                  observer suppresses against. Inbound composite values are not
 *                  parked here at all: the drain converts from `last`, so attribute
 *                  pairs and step storms coalesce.
 *      string    - parked heap string truncated to the sink width, released on
 *                  injection: no committed value is kept, so a rename is judged
 *                  against the param.
 *
 * `last`/`echo` are Matter-domain - an int for the scalar and composite kinds, a
 * heap string for the string kind.
 */
typedef struct {
    union {
        const rm_mirror_capability_t *cap;   /* scalar and string kinds */
        const rm_mirror_composite_t *comp;   /* composite kind */
    };
    rm_mirror_binding_kind_t kind;
    uint16_t endpoint_id;
    uint8_t flags;             /* rm_mirror_binding_flag_t */
    int32_t identify_seconds;  /* valid while F_IDENTIFY_PENDING */
    rm_mirror_slot_t slots[];
} rm_mirror_binding_t;

/* Transforms (pure) ***********************************************************/

int32_t rm_mirror_xform_to_matter(const rm_mirror_xform_t *xform, int32_t rmng_val);
int32_t rm_mirror_xform_to_rmng(const rm_mirror_xform_t *xform, int32_t matter_val);

/**
 * @brief Transform a float RainMaker Neo value into the Matter domain. Only RM_MIRROR_XFORM_SCALE keeps the
 *        fractional part (that is what it is for); the others round to an integer first.
 */
int32_t rm_mirror_xform_to_matter_f(const rm_mirror_xform_t *xform, float rmng_val);

/** @brief Inverse of rm_mirror_xform_to_matter_f(). */
float rm_mirror_xform_to_rmng_f(const rm_mirror_xform_t *xform, int32_t matter_val);

/* Composite conversions (pure) ************************************************/

/**
 * @brief Run @p comp's conversion in the params -> attributes direction.
 * @note Reads comp->n_params values and writes comp->n_attributes; a conversion this build left
 *       out leaves @p matter_vals untouched.
 */
void rm_mirror_composite_to_matter(const rm_mirror_composite_t *comp, const int32_t *rmng_vals,
                                   int32_t *matter_vals);

/** @brief Inverse of ::rm_mirror_composite_to_matter. */
void rm_mirror_composite_to_rmng(const rm_mirror_composite_t *comp, const int32_t *matter_vals,
                                 int32_t *rmng_vals);

/* Color conversion (pure, integer Q15; see rm_mirror_color.c) *****************/

/**
 * @brief HSV chromaticity (hue 0-360, saturation 0-100) -> CIE xy in the
 *        Matter CurrentX/CurrentY encoding (value = x * 65536, 0..0xFEFF).
 */
void rm_mirror_color_hs_to_xy(int32_t hue_deg, int32_t sat_pct, int32_t *out_x, int32_t *out_y);

/**
 * @brief Inverse of ::rm_mirror_color_hs_to_xy; out-of-gamut xy clips to the
 *        nearest sRGB-edge color.
 */
void rm_mirror_color_xy_to_hs(int32_t x, int32_t y, int32_t *out_hue, int32_t *out_sat);

/* Table lookups (pure) ********************************************************/

const rm_mirror_capability_t *rm_mirror_table_find_capability(const char *param_type);

/**
 * @brief Select the write_as command for a Matter-domain value: the entry whose
 *        @c when matches, else the unconditional entry. NULL when the
 *        capability declares none, i.e. the attribute is written directly.
 */
const rm_mirror_write_cmd_t *rm_mirror_table_find_write_cmd(const rm_mirror_capability_t *cap,
        int32_t matter_val);

/** @brief Whether the device being matched carries a param of type @p param_type. */
typedef bool (*rm_mirror_param_present_fn)(const char *param_type, void *ctx);

/**
 * @brief Match a device to the first (most specific) rule whose params @p present
 *        all reports as being on the device.
 */
const rm_mirror_devtype_rule_t *rm_mirror_table_match_rule(const char *device_type,
        rm_mirror_param_present_fn present, void *ctx);

/* Engine lifecycle ************************************************************/

/**
 * @brief Walk the node and lower it into a Matter tree via @p port, building
 *        the binding table and seeding initial attribute values.
 *
 * Call after all devices/params are added to @p node, before Matter starts.
 */
esp_rmaker_error_t rm_mirror_engine_init(const esp_rmaker_node_t *node, const rm_mirror_port_ops_t *port);

/**
 * @brief Let devices be added and removed; the port calls it when enable() returns, also before a deferred start.
 */
void rm_mirror_engine_open_devices(void);

/**
 * @brief Mirror a device added to the node after rm_mirror_engine_open_devices(); ESP_RMAKER_NOT_INITIALIZED before.
 * @note Not from a device write callback. On ESP_RMAKER_TIMEOUT the device stays bound: keep it alive.
 *
 * @param[out] endpoint_id Endpoint the device is mirrored on, also when still bound on TIMEOUT; else 0. May be NULL.
 * @return ESP_RMAKER_NOT_SUPPORTED when there is nothing to mirror, ESP_RMAKER_ALREADY_EXISTS when mirrored,
 *         ESP_RMAKER_INVALID_STATE when the caller holds the Matter stack lock.
 */
esp_rmaker_error_t rm_mirror_engine_add_device(const esp_rmaker_device_t *device, uint16_t *endpoint_id);

/** @brief Node that rm_mirror_engine_init() mirrors; NULL before. */
const esp_rmaker_node_t *rm_mirror_engine_get_node(void);

/**
 * @brief Stop mirroring a device: free its bindings, then destroy its endpoint.
 * @note Call before the device is deleted, and not from a device write callback.
 *
 * @param[out] endpoint_id Endpoint the device is mirrored on, also on failure; 0 if none. May be NULL.
 * @return ESP_RMAKER_NOT_FOUND when not mirrored, ESP_RMAKER_TIMEOUT when the device stays mirrored,
 *         ESP_RMAKER_INVALID_STATE when the caller holds the Matter stack lock.
 */
esp_rmaker_error_t rm_mirror_engine_remove_device(const esp_rmaker_device_t *device, uint16_t *endpoint_id);

/**
 * @brief Set the Reachable value of a device of another node, now and for its later adds.
 * @return ESP_RMAKER_NOT_SUPPORTED when there is no bridge or the device is in the mirrored node.
 */
esp_rmaker_error_t rm_mirror_engine_set_reachable(const esp_rmaker_device_t *device, bool reachable);

/**
 * @brief Register the param update observer and start the injection task.
 */
esp_rmaker_error_t rm_mirror_engine_start(void);

/**
 * @brief Re-assert the canonical param values on the Matter side and start accepting inbound
 *        changes.
 *
 * Call once the Matter stack is up: cluster StartUp* handling and esp-matter's restore of
 * NONVOLATILE attributes both land after the mirror seeded the tree, and until this runs an inbound
 * change cannot be told apart from those.
 */
void rm_mirror_engine_reseed(void);

/**
 * @brief Apply the StartUp* policies, then reseed, with no device add or removal in between; once only.
 */
void rm_mirror_engine_start_inbound(void);

/**
 * @brief Tear down engine state (primarily for tests).
 */
void rm_mirror_engine_deinit(void);

/* Onboarding readiness *********************************************************/

/**
 * @brief Sample the onboarding facts once, when the Matter stack comes up.
 *
 * @param[in] port Port ops, for re-opening the commissioning window.
 * @param[in] state Facts sampled by the port.
 * @param[in] reopen_window Re-open the commissioning window when the last fabric
 *            is removed, rather than requiring a factory reset.
 * @param[in] window_timeout_s How long a re-opened window stays open.
 */
esp_rmaker_error_t rm_mirror_onboarding_init(const rm_mirror_port_ops_t *port,
        const rm_mirror_onboarding_state_t *state, bool reopen_window, uint16_t window_timeout_s);

void rm_mirror_onboarding_deinit(void);

/**
 * @brief Block until onboarding is complete, i.e. nothing stands between the
 *        node and starting the RainMaker agent.
 *
 * @return ESP_RMAKER_OK once ready, ESP_RMAKER_TIMEOUT if @p timeout_ms elapses,
 *         ESP_RMAKER_INVALID_STATE if the gate was never initialised.
 */
esp_rmaker_error_t rm_mirror_onboarding_wait(uint32_t timeout_ms);

/** @brief Whether the gate is already open (no blocking). */
bool rm_mirror_onboarding_ready(void);

/* Inbound entry points (called by the port) ***********************************/

/**
 * @brief Handle the network coming up (IP address assigned).
 */
void rm_mirror_engine_handle_network_up(void);

/**
 * @brief Handle Matter commissioning completing over any fabric.
 */
void rm_mirror_engine_handle_commissioning_complete(void);

/**
 * @brief Handle CHIPoBLE being torn down and its memory returned to the heap.
 */
void rm_mirror_engine_handle_ble_reclaimed(void);

/**
 * @brief Handle a fabric being removed, @p remaining fabrics left. Re-opens the
 *        commissioning window when the last one goes.
 */
void rm_mirror_engine_handle_fabric_removed(size_t remaining);

/**
 * @brief Handle a Matter-side attribute change (called from the Matter thread).
 */
void rm_mirror_engine_handle_matter_update(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t value);

/**
 * @brief Handle a Matter-side string attribute change (e.g. a controller
 *        writing BasicInformation.NodeLabel). Called from the Matter thread.
 */
void rm_mirror_engine_handle_matter_update_str(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, const char *value);

/**
 * @brief Set the handler for identify requests, or NULL to clear it.
 *
 * Kept across init/deinit: the application registers it before the mirror is enabled.
 */
void rm_mirror_engine_set_identify_handler(esp_rmaker_matter_mirror_identify_t handler);

/**
 * @brief Handle a Matter identification request for an endpoint.
 */
void rm_mirror_engine_handle_identify(uint16_t endpoint_id, int32_t seconds);

/**
 * @brief Handle a Matter identification request that names an effect.
 *
 * Offers it to the identify handler first; falls back to writing the device's
 * esp.param.identify param with @p seconds, which is all that param can carry.
 */
void rm_mirror_engine_handle_identify_effect(uint16_t endpoint_id,
        esp_rmaker_matter_mirror_identify_effect_t effect_id, uint8_t effect_variant, int32_t seconds);

/**
 * @brief Current bound param value converted to the Matter domain.
 *
 * Used by the port's StartUp* application to compute toggle-previous.
 *
 * @return ESP_RMAKER_NOT_FOUND when no binding matches the path.
 */
esp_rmaker_error_t rm_mirror_engine_get_bound_value(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t *matter_val);

/**
 * @brief Honor the persisted StartUp* attribute semantics (Matter spec: on
 *        power-up the device SHALL assume the configured state) for every
 *        binding whose capability declares a startup policy.
 *
 * Reads each policy attribute through the port, resolves it per the mapping's
 * enumerated policy, and injects the implied boot value with
 * ::rm_mirror_engine_apply_boot_value.
 *
 * Call once the stack is up and before ::rm_mirror_engine_reseed: the inbound
 * gate is still closed (so stack-start noise is discarded) and the reseed that
 * follows asserts the post-StartUp state on both sides. A no-op when the port
 * cannot read attributes.
 */
void rm_mirror_engine_apply_startup_policies(void);

/**
 * @brief Apply a spec-mandated boot value (Matter domain) to the binding's
 *        param: synchronous injection into the device write callback with
 *        SRC_EXTERNAL, skipped when the canonical store already matches.
 *
 * The injected state lands in the param store before the reseed, so the reseed
 * asserts the post-StartUp state and Matter/RainMaker Neo agree. Runs before inbound
 * opens, so it does not race the gate.
 *
 * @return ESP_RMAKER_NOT_FOUND when no binding matches the path.
 */
esp_rmaker_error_t rm_mirror_engine_apply_boot_value(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t matter_val);

/**
 * @brief Copy the RainMaker Neo device id behind a mirrored endpoint into @p buf; false if none.
 */
bool rm_mirror_engine_copy_device_id(uint16_t endpoint_id, char *buf, size_t len);

/* Test hooks ******************************************************************/

/**
 * @brief Mark the engine initialized and open to devices after rm_mirror_sync_init() alone (tests only).
 */
void rm_mirror_engine_set_initialized(void);

/**
 * @brief Number of bindings of a kind (tests only).
 */
size_t rm_mirror_engine_count_bindings(rm_mirror_binding_kind_t kind);

/**
 * @brief The @p nth binding of @p kind in registration order, NULL past the end (tests only).
 */
const rm_mirror_binding_t *rm_mirror_engine_get_binding(rm_mirror_binding_kind_t kind, size_t nth);

/**
 * @brief Drain pending inbound injections synchronously (tests only; the
 *        runtime path uses the injection task).
 */
void rm_mirror_engine_drain_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* __RM_MIRROR_ENGINE_H__ */
