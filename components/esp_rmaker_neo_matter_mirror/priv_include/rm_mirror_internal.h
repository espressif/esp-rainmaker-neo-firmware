/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_internal.h
 * @brief Declarations shared between the mirror engine's modules.
 */

#ifndef __RM_MIRROR_INTERNAL_H__
#define __RM_MIRROR_INTERNAL_H__

#include "esp_rmaker_data_model_introspect.h"

#include "rm_mirror_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NVS namespace of the mirror's own records */
#define RM_MIRROR_NVS_NAMESPACE "rm_mirror"

/**
 * @brief Initialize the sync module (binding table, lock, port reference) and load the endpoint map.
 */
esp_rmaker_error_t rm_mirror_sync_init(const rm_mirror_port_ops_t *port);

/**
 * @brief Lower the node into a Matter tree via the port (called by engine init).
 */
esp_rmaker_error_t rm_mirror_lowering_run(const esp_rmaker_node_t *node, const rm_mirror_port_ops_t *port);

/**
 * @brief Lower a single device (rule match, endpoint/cluster creation, bindings).
 * @note A device that binds no param gets no endpoint.
 *
 * @note A device the endpoint map knows asks the port for its old endpoint ID, except during engine init.
 *
 * @param[in] live The stack is up: inbound values on the endpoint are dropped until it is enabled.
 * @param[out] endpoint_id_out Endpoint created for the device, or 0 when it is not mirrored. May be NULL.
 * @return ESP_RMAKER_INVALID_ARG when the device is in no node, or in a node without a node ID.
 */
esp_rmaker_error_t rm_mirror_lowering_lower_device(const rm_mirror_port_ops_t *port,
        const esp_rmaker_device_t *device, bool live, uint16_t *endpoint_id_out);

/**
 * @brief Forget that @p endpoint_id was lowered, so its device seeds afresh when it comes back, and destroy it.
 */
void rm_mirror_lowering_drop_endpoint(const rm_mirror_port_ops_t *port, uint16_t endpoint_id);

/* Endpoint map: node ID + device ID -> endpoint ID, kept in storage; a NULL node ID is the mirrored node.
 * Used at init and under the membership lock only. */

/** @brief Load the map from storage; an unreadable map starts empty. */
void rm_mirror_endpoint_map_load(void);

/** @brief Forget the map in RAM; storage keeps it. */
void rm_mirror_endpoint_map_unload(void);

/** @brief Endpoint the map keeps for the device, or RM_MIRROR_ENDPOINT_NONE when none or in use. */
uint16_t rm_mirror_endpoint_map_find(const char *node_id, const char *device_id);

/**
 * @brief Record the device on @p endpoint_id, in place of any other device there.
 * @return Whether the port's store holds this device's state on the endpoint, from an earlier lowering.
 */
bool rm_mirror_endpoint_map_assign(const char *node_id, const char *device_id, uint16_t endpoint_id);

/** @brief Record that the device on @p endpoint_id is lowered, so a later lowering there can adopt its state. */
void rm_mirror_endpoint_map_set_lowered(uint16_t endpoint_id);

/** @brief The device on @p endpoint_id is removed: its state there is gone, its ID stays kept for it. */
void rm_mirror_endpoint_map_release(uint16_t endpoint_id);

/** @brief Write the map to storage if it changed. */
esp_rmaker_error_t rm_mirror_endpoint_map_commit(void);

/* Part of the stored map format: never derive it from another constant */
#define RM_MIRROR_DEVICE_KEY_LEN 14

/** @brief The device's map key: the first bytes of SHA-256(node ID, NUL, device ID). */
bool rm_mirror_device_key(const char *node_id, const char *device_id, uint8_t key[RM_MIRROR_DEVICE_KEY_LEN]);

/* Bridge: devices of other nodes go under an Aggregator. Used at init and under the membership lock only. */

/** @brief Create the Aggregator when the port can; before the first device is lowered. */
esp_rmaker_error_t rm_mirror_bridge_init(const rm_mirror_port_ops_t *port);

/** @brief Forget the Aggregator and every stored reachable value. */
void rm_mirror_bridge_deinit(void);

/** @brief The Aggregator endpoint, or RM_MIRROR_ENDPOINT_NONE when there is no bridge. */
uint16_t rm_mirror_bridge_aggregator(void);

/** @brief Make a new endpoint the bridged node of the device, with its stored reachable value. */
esp_rmaker_error_t rm_mirror_bridge_endpoint(const rm_mirror_port_ops_t *port, uint16_t endpoint_id,
        const char *node_id, const esp_rmaker_device_t *device);

/** @brief Store the device's reachable value, for its endpoint now and for a later add. */
esp_rmaker_error_t rm_mirror_bridge_store_reachable(const char *node_id, const char *device_id, bool reachable);

/**
 * @brief Store the reachable value of a device of another node.
 * @return ESP_RMAKER_NOT_SUPPORTED when there is no bridge or the device is in the mirrored node.
 */
esp_rmaker_error_t rm_mirror_lowering_store_reachable(const esp_rmaker_device_t *device, bool reachable);

/**
 * @brief Drop inbound values on @p endpoint_id, a runtime add's endpoint that is not enabled yet; 0 stops it.
 */
void rm_mirror_sync_set_dormant(uint16_t endpoint_id);

/**
 * @brief Outbound sync handler (the param update observer body). Exposed for
 *        deterministic testing without task/observer plumbing.
 */
void rm_mirror_sync_handle_param_update(const esp_rmaker_param_t *param, const esp_rmaker_param_val_t *val);

/**
 * @brief Register a param <-> attribute binding with its seeded Matter value.
 */
esp_rmaker_error_t rm_mirror_sync_add_binding(const esp_rmaker_param_t *param,
        const rm_mirror_capability_t *cap, uint16_t endpoint_id, int32_t seeded_matter_val);

/**
 * @brief Register a composite (N params <-> M attributes) binding.
 *
 * @param[in] params Member param handles, ordered per the composite's
 *            param_types (comp->n_params entries).
 * @param[in] seeded_matter Seeded per-attribute Matter values, ordered per the
 *            composite's attribute_ids (comp->n_attributes entries).
 */
esp_rmaker_error_t rm_mirror_sync_add_composite_binding(const rm_mirror_composite_t *comp,
        const esp_rmaker_param_t *const *params, uint16_t endpoint_id, const int32_t *seeded_matter);

/**
 * @brief Drop and free every binding on @p endpoint_id, plus the node-scoped name binding of its
 *        device, once no task is using the table.
 * @note Must not run inside a device write callback: it would wait on itself and time out.
 *
 * @return ESP_RMAKER_NOT_FOUND when nothing is bound there, ESP_RMAKER_TIMEOUT when a task
 *         stayed in the table; nothing is removed then, so the device must stay alive.
 */
esp_rmaker_error_t rm_mirror_sync_remove_endpoint(uint16_t endpoint_id);

/**
 * @brief Current param value as an int32 in the RainMaker Neo domain (bool as 0/1).
 *
 * No transform is applied - this is the value composites convert from, and the
 * one the observer suppresses against.
 *
 * @return ESP_RMAKER_NOT_SUPPORTED for anything but a bool or int param.
 */
esp_rmaker_error_t rm_mirror_param_val_as_i32(const esp_rmaker_param_t *param, int32_t *out);

/**
 * @brief Current param value transformed into the Matter domain.
 *
 * Bool params cross as 0/1, int params as-is, float params through the capability's transform in
 * the float domain (so a scale transform keeps the fractional part).
 *
 * @return ESP_RMAKER_NOT_SUPPORTED for param value types the mirror does not carry.
 */
esp_rmaker_error_t rm_mirror_param_to_matter(const esp_rmaker_param_t *param,
        const rm_mirror_capability_t *cap, int32_t *out);

/**
 * @brief Whether the param holds state worth asserting on the Matter side.
 *
 * A write-only param is a request channel, not state: its value is a
 * compiled-in zero, and the attribute it matches is one the cluster manages itself. The binding
 * still exists - it carries writes outbound as the capability's write_as command.
 */
static inline bool rm_mirror_param_holds_state(const esp_rmaker_param_t *param)
{
    return (esp_rmaker_param_get_prop_flags((esp_rmaker_param_t *)param) & PROP_FLAG_READ) != 0;
}

/**
 * @brief Push one Matter-domain value at the port: writes the attribute (with its shadows), or
 *        hands it to the port's report_as hook for capabilities whose cluster does not expose a
 *        plain scalar attribute.
 *
 * @param[in] init true during lowering, before the endpoint is enabled (uses the port's set rather
 *            than update entry point).
 */
esp_rmaker_error_t rm_mirror_port_push(const rm_mirror_port_ops_t *port, uint16_t endpoint_id,
                                       const rm_mirror_capability_t *cap, int32_t matter_val, bool init);

/** @brief A port attribute write: either attribute_set or attribute_update. */
typedef esp_rmaker_error_t (*rm_mirror_attr_write_fn)(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t value, rm_mirror_val_type_t type);

/**
 * @brief Write a capability's matched attribute through @p write, followed by
 *        the shadow attributes the mapping declares for it (same cluster, same
 *        value - e.g. ColorMode into EnhancedColorMode). Shadows are skipped
 *        when the matched write fails.
 */
esp_rmaker_error_t rm_mirror_write_capability_attr(rm_mirror_attr_write_fn write, uint16_t endpoint_id,
        const rm_mirror_capability_t *cap, int32_t matter_val);

/**
 * @brief Register a string binding: @p param (a string param, typically
 *        esp.param.name) syncs bidirectionally with the capability's matched
 *        attribute on @p endpoint_id, truncated to its transform max_len.
 */
esp_rmaker_error_t rm_mirror_sync_add_str_binding(const esp_rmaker_param_t *param,
        const rm_mirror_capability_t *cap, uint16_t endpoint_id);

#ifdef __cplusplus
}
#endif

#endif /* __RM_MIRROR_INTERNAL_H__ */
