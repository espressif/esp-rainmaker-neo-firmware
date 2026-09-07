/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_port.h
 * @brief Port interface between the platform-neutral mirror engine and the
 *        Matter stack implementation.
 *
 * The engine drives tree construction and outbound attribute updates through
 * this ops table; the port calls back into the engine (see rm_mirror_engine.h)
 * for inbound attribute changes and identification requests. Numeric values cross
 * the boundary as int32 (booleans as 0/1, floats through the capability's
 * transform); strings have their own entry points either way.
 */

#ifndef __RM_MIRROR_PORT_H__
#define __RM_MIRROR_PORT_H__

#include <stdint.h>
#include <stddef.h>

#include "esp_rmaker_error_types.h"

#include "rm_mirror_mapping.h"

#ifdef __cplusplus
extern "C" {
#endif

/** No endpoint: a top-level endpoint has no parent, and a new endpoint takes the next free ID */
#define RM_MIRROR_ENDPOINT_NONE 0xFFFFu

/** Onboarding facts, sampled by the port once the Matter stack is up */
typedef struct {
    /** The network only arrives through Matter commissioning (no RainMaker provisioning) */
    bool wait_for_commissioning;
    /** IP connectivity is already established */
    bool network_up;
    /** At least one fabric exists, so the node has been commissioned before */
    bool commissioned;
    /** CHIPoBLE is up and will be torn down, freeing its memory to the heap */
    bool ble_teardown_expected;
    /** CHIPoBLE has already been torn down */
    bool ble_reclaimed;
} rm_mirror_onboarding_state_t;

/**
 * Grouped by concern, and within the attribute group by (numeric, string) then
 * (set = init-time, update = runtime). Only the tree-construction and attribute
 * read/write ops are mandatory; the rest are optional and NULL-checked.
 *
 * Note for C++ ports: designated initializers must follow declaration order, so
 * an ops table has to be written in the order below.
 */
typedef struct {
    /* Tree construction (init-time) ******************************************/

    /**
     * @brief Create an endpoint of a Matter device type, with an optional parent and fixed ID.
     * @note After the stack starts, the endpoint stays invisible to controllers until endpoint_enable.
     *
     * @param[in] parent_endpoint_id Parent endpoint, or RM_MIRROR_ENDPOINT_NONE for a top-level endpoint.
     * @param[in] endpoint_id ID to use, or RM_MIRROR_ENDPOINT_NONE for the next free one. A fixed ID can fail, e.g.
     *            before the stack starts; when a destroyed endpoint still holds it, the call waits until it is freed.
     */
    esp_rmaker_error_t (*endpoint_create)(uint16_t parent_endpoint_id, uint16_t endpoint_id,
                                          uint32_t device_type_id, uint8_t device_type_version,
                                          uint16_t *out_endpoint_id);
    /**
     * @brief Declare a further Matter device type on an existing endpoint, for
     *        the device types a mapping rule lists as additional. Optional; a
     *        rule that needs it on a port without it fails lowering.
     */
    esp_rmaker_error_t (*endpoint_device_type_add)(uint16_t endpoint_id, uint32_t device_type_id,
            uint8_t device_type_version);
    /**
     * @brief Create a cluster on an endpoint, with the FeatureMap bits @p feature_map enabled.
     *        Must be a no-op if the cluster already exists on the endpoint.
     */
    esp_rmaker_error_t (*cluster_create)(uint16_t endpoint_id, uint32_t cluster_id, uint32_t feature_map);

    /**
     * @brief Make an endpoint created after the stack started visible to controllers; a no-op before that.
     * @note Idempotent: the stack can enable the endpoint at start between lowering and this call.
     */
    esp_rmaker_error_t (*endpoint_enable)(uint16_t endpoint_id);
    /**
     * @brief Destroy an endpoint and everything on it.
     * @note It can stay allocated, disabled, for a short time after the call; its ID is not free until then.
     */
    esp_rmaker_error_t (*endpoint_destroy)(uint16_t endpoint_id);
    /**
     * @brief Create the Aggregator endpoint for bridged endpoints. Optional; ESP_RMAKER_NOT_SUPPORTED means no bridge.
     */
    esp_rmaker_error_t (*aggregator_create)(uint16_t *out_endpoint_id);
    /**
     * @brief Add the Bridged Node device type and the Bridged Device Basic Information cluster to a new endpoint.
     */
    esp_rmaker_error_t (*endpoint_bridge)(uint16_t endpoint_id, const char *unique_id, const char *label,
                                          bool reachable);
    /** @brief Set Reachable on a bridged endpoint, from any task. */
    esp_rmaker_error_t (*endpoint_reachable_set)(uint16_t endpoint_id, bool reachable);

    /* Attribute access *******************************************************/

    /**
     * @brief Set an attribute value directly (init-time, before Matter starts).
     *        @p type is the attribute's Matter wire type, from the mapping.
     */
    esp_rmaker_error_t (*attribute_set)(uint16_t endpoint_id, uint32_t cluster_id,
                                        uint32_t attribute_id, int32_t value,
                                        rm_mirror_val_type_t type);
    /**
     * @brief Update an attribute value at runtime, from any task; it can wait for the Matter stack lock.
     */
    esp_rmaker_error_t (*attribute_update)(uint16_t endpoint_id, uint32_t cluster_id,
                                           uint32_t attribute_id, int32_t value,
                                           rm_mirror_val_type_t type);
    /**
     * @brief Write a string attribute at runtime.
     *
     * Runtime only, unlike attribute_set. A string attribute may belong to a
     * cluster the stack implements rather than to the port's own store, and
     * those reject writes until the stack has registered them; the port picks
     * the cheaper route where it can. Same threading contract as
     * attribute_update.
     */
    esp_rmaker_error_t (*attribute_write_str)(uint16_t endpoint_id, uint32_t cluster_id,
            uint32_t attribute_id, const char *value);
    /**
     * @brief Read a numeric attribute the mirror does not own, e.g. a persisted
     *        StartUp* policy attribute restored by the stack. Optional.
     *
     * @return ESP_RMAKER_OK when @p out holds a value; any error when the
     *         attribute is absent, null, or not a numeric type - all of which
     *         mean "no configured value" to the caller.
     */
    esp_rmaker_error_t (*attribute_get)(uint16_t endpoint_id, uint32_t cluster_id,
                                        uint32_t attribute_id, int32_t *out);
    /**
     * @brief Stop persisting an attribute on every write, for values that ramp
     *        during transitions (init-time, called once the attribute is seeded).
     *        Optional; absent means every write persists.
     */
    esp_rmaker_error_t (*attribute_defer_persistence)(uint16_t endpoint_id, uint32_t cluster_id,
            uint32_t attribute_id);
    /**
     * @brief Deliver a value whose cluster does not expose it as a plain scalar
     *        attribute, per the mapping's report_as kind (e.g. cumulative energy,
     *        which Matter carries in a struct the cluster owns). Optional; the
     *        engine reports ESP_RMAKER_NOT_SUPPORTED when a mapping needs it and
     *        the port has no handler. Same threading contract as attribute_update.
     */
    esp_rmaker_error_t (*report_as)(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                    const char *kind, int32_t value);

    /* Commands ***************************************************************/

    /**
     * @brief Invoke a cluster command at runtime instead of writing the matched
     *        attribute, so the cluster updates its derived attributes itself.
     *        @p value substitutes every argument the mapping marked from_value.
     *        Same threading contract as attribute_update. Returning an error
     *        makes the engine fall back to attribute_update.
     */
    esp_rmaker_error_t (*command_invoke)(uint16_t endpoint_id, uint32_t cluster_id,
                                         const rm_mirror_write_cmd_t *cmd, int32_t value);

    /* Platform facts *********************************************************/

    /**
     * @brief Whether this boot followed a software update rather than a power
     *        cycle. Optional; absent means "assume a power cycle".
     */
    bool (*boot_is_software_update)(void);
    /**
     * @brief Whether the calling task holds the Matter stack lock. Optional; absent means "never".
     */
    bool (*stack_lock_held)(void);

    /* Onboarding *************************************************************/

    /**
     * @brief Sample the onboarding facts. Called once, after the Matter stack is
     *        up: the fabric table is not readable before that.
     */
    esp_rmaker_error_t (*onboarding_state_get)(rm_mirror_onboarding_state_t *out);
    /**
     * @brief Open a basic commissioning window for @p timeout_s, advertising over
     *        whatever transports are still available. A no-op if one is open.
     */
    esp_rmaker_error_t (*commissioning_window_open)(uint16_t timeout_s);
} rm_mirror_port_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* __RM_MIRROR_PORT_H__ */
