/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file fake_port.h
 * @brief Recording fake of the mirror port for engine unit tests.
 */

#ifndef __FAKE_PORT_H__
#define __FAKE_PORT_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "rm_mirror_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAKE_PORT_MAX_ENDPOINTS  8
#define FAKE_PORT_MAX_CLUSTERS   48
#define FAKE_PORT_MAX_ATTR_CALLS 64

#define FAKE_PORT_MAX_EXTRA_DEVICE_TYPES 2

typedef struct {
    uint16_t parent_endpoint_id;
    /* The ID asked for, or RM_MIRROR_ENDPOINT_NONE for the next free one */
    uint16_t requested_endpoint_id;
    uint32_t device_type_id;
    uint8_t device_type_version;
    /* Device types declared on top of the one the endpoint was created with */
    uint32_t extra_device_types[FAKE_PORT_MAX_EXTRA_DEVICE_TYPES];
    size_t n_extra_device_types;
} fake_port_endpoint_t;

typedef struct {
    uint16_t endpoint_id;
    uint32_t cluster_id;
    /* FeatureMap bits accumulate across cluster_create calls for the same cluster,
     * mirroring the real port's merge semantics (CT + HS + XY) */
    uint32_t feature_map;
} fake_port_cluster_t;

typedef struct {
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t attribute_id;
    int32_t value;
    /** Matter wire type the mapping declared for the attribute */
    rm_mirror_val_type_t type;
} fake_port_attr_call_t;

#define FAKE_PORT_STR_MAX        48

typedef struct {
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t attribute_id;
    char value[FAKE_PORT_STR_MAX + 1];
} fake_port_str_call_t;

#define FAKE_PORT_MAX_STORED_ATTRS 8

/** An attribute readable through attribute_get, e.g. a persisted StartUp* policy */
typedef struct {
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t attribute_id;
    int32_t value;
    /** Reads fail, as they do for an absent or null attribute */
    bool null;
} fake_port_stored_attr_t;

#define FAKE_PORT_MAX_CMD_ARGS 6

typedef struct {
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t command_id;
    size_t n_args;
    /** Resolved argument values: literals as mapped, from_value args substituted */
    int32_t args[FAKE_PORT_MAX_CMD_ARGS];
} fake_port_invoke_call_t;

#define FAKE_PORT_KIND_LEN 32

typedef struct {
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t attribute_id;
    char kind[FAKE_PORT_KIND_LEN];
    int32_t value;
} fake_port_report_call_t;

#define FAKE_PORT_BDBI_STR_MAX 32

/** What endpoint_bridge and endpoint_reachable_set gave an endpoint */
typedef struct {
    bool bridged;
    char unique_id[FAKE_PORT_BDBI_STR_MAX + 1];
    char label[FAKE_PORT_BDBI_STR_MAX + 1];
    bool reachable;
    size_t n_reachable_sets;
} fake_port_bridged_t;

typedef struct {
    fake_port_endpoint_t endpoints[FAKE_PORT_MAX_ENDPOINTS];
    size_t n_endpoints;
    fake_port_cluster_t clusters[FAKE_PORT_MAX_CLUSTERS];
    size_t n_clusters;
    fake_port_attr_call_t set_calls[FAKE_PORT_MAX_ATTR_CALLS];
    size_t n_set_calls;
    fake_port_attr_call_t update_calls[FAKE_PORT_MAX_ATTR_CALLS];
    size_t n_update_calls;
    fake_port_str_call_t write_str_calls[FAKE_PORT_MAX_ATTR_CALLS];
    size_t n_write_str_calls;
    fake_port_invoke_call_t invoke_calls[FAKE_PORT_MAX_ATTR_CALLS];
    size_t n_invoke_calls;
    fake_port_report_call_t report_calls[FAKE_PORT_MAX_ATTR_CALLS];
    size_t n_report_calls;
    /** Set to make command_invoke fail, as the real port does before Matter starts */
    bool invoke_fails;
    /** Attributes served to attribute_get; anything else reads as absent */
    fake_port_stored_attr_t stored_attrs[FAKE_PORT_MAX_STORED_ATTRS];
    size_t n_stored_attrs;
    /** Answer given to boot_is_software_update */
    bool boot_software_update;
    /** Attributes attribute_defer_persistence was called for */
    fake_port_attr_call_t deferred_calls[FAKE_PORT_MAX_ATTR_CALLS];
    size_t n_deferred_calls;
    /** Facts handed back by onboarding_state_get */
    rm_mirror_onboarding_state_t onboarding_state;
    size_t n_window_opens;
    uint16_t last_window_timeout_s;
    /** Endpoints enabled, with the update count at the time of each enable */
    uint16_t enabled[FAKE_PORT_MAX_ENDPOINTS];
    size_t updates_at_enable[FAKE_PORT_MAX_ENDPOINTS];
    size_t sets_at_enable[FAKE_PORT_MAX_ENDPOINTS];
    size_t n_enabled;
    uint16_t destroyed[FAKE_PORT_MAX_ENDPOINTS];
    size_t n_destroyed;
    /** Port calls that reached a destroyed endpoint */
    size_t n_writes_after_destroy;
    /** Set to make endpoint_enable fail */
    bool enable_fails;
    /** Set to make endpoint_create with a fixed ID fail, as it does before the stack starts */
    bool fixed_id_fails;
    /** Answer given to stack_lock_held */
    bool stack_lock_held;
    /** Set to make endpoint_enable report enable_report, as a cluster init callback does, then drain */
    bool enable_reports;
    /** Set to make every attribute_set report enable_report, as a cluster change callback does, then drain */
    bool set_reports;
    fake_port_attr_call_t enable_report;
    /** Set to make aggregator_create create the Aggregator; else it reports no bridge */
    bool bridge;
    fake_port_bridged_t bridged[FAKE_PORT_MAX_ENDPOINTS];
} fake_port_state_t;

extern fake_port_state_t fake_port_state;

const rm_mirror_port_ops_t *fake_port_get_ops(void);
void fake_port_reset(void);

/** Zero the recorded call counters, keeping endpoints and clusters. For skipping past setup
 *  traffic such as the reseed the port performs once Matter is up. */
void fake_port_clear_calls(void);

/** Find a recorded attribute_set call, or NULL */
const fake_port_attr_call_t *fake_port_find_set(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id);

/** True if the endpoint carries the device type, whether as its own or an additional one */
bool fake_port_endpoint_has_device_type(uint16_t endpoint_id, uint32_t device_type_id);

/** True if a cluster was created on the endpoint */
bool fake_port_has_cluster(uint16_t endpoint_id, uint32_t cluster_id);

/** True if every bit of @p feature_bits was requested for the cluster (across all calls) */
bool fake_port_cluster_has_feature(uint16_t endpoint_id, uint32_t cluster_id, uint32_t feature_bits);

/** Find a recorded report_as call, or NULL */
const fake_port_report_call_t *fake_port_find_report(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id);

/** Make an attribute readable through attribute_get; @p null makes the read fail */
void fake_port_store_attr(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                          int32_t value, bool null);

/** True if deferred persistence was requested for the attribute */
bool fake_port_is_deferred(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id);

#ifdef __cplusplus
}
#endif

#endif /* __FAKE_PORT_H__ */
