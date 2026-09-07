/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file fake_port.c
 * @brief Recording fake of the mirror port for engine unit tests.
 */

#include <string.h>

#include "fake_port.h"
#include "rm_mirror_engine.h"

fake_port_state_t fake_port_state;

static bool __endpoint_live(uint16_t endpoint_id)
{
    if (endpoint_id == 0 || endpoint_id > fake_port_state.n_endpoints) {
        return false;
    }
    for (size_t i = 0; i < fake_port_state.n_destroyed; i++) {
        if (fake_port_state.destroyed[i] == endpoint_id) {
            return false;
        }
    }
    return true;
}

/* A resumed endpoint is live again, so writes to it no longer count as reaching a destroyed one */
static void __forget_destroyed(uint16_t endpoint_id)
{
    size_t kept = 0;
    for (size_t i = 0; i < fake_port_state.n_destroyed; i++) {
        if (fake_port_state.destroyed[i] != endpoint_id) {
            fake_port_state.destroyed[kept++] = fake_port_state.destroyed[i];
        }
    }
    fake_port_state.n_destroyed = kept;
}

static esp_rmaker_error_t __endpoint_create(uint16_t parent_endpoint_id, uint16_t endpoint_id,
        uint32_t device_type_id, uint8_t device_type_version, uint16_t *out_endpoint_id)
{
    uint16_t id;
    if (endpoint_id != RM_MIRROR_ENDPOINT_NONE) {
        /* Like resume(): any ID that is not live, unless the test makes it fail */
        if (fake_port_state.fixed_id_fails || endpoint_id == 0 || endpoint_id > FAKE_PORT_MAX_ENDPOINTS ||
                __endpoint_live(endpoint_id)) {
            return ESP_RMAKER_FAIL;
        }
        id = endpoint_id;
        __forget_destroyed(id);
    } else {
        if (fake_port_state.n_endpoints >= FAKE_PORT_MAX_ENDPOINTS) {
            return ESP_RMAKER_NO_MEM;
        }
        /* Endpoint ids start at 1, like esp-matter (0 is the root endpoint) */
        id = (uint16_t)(fake_port_state.n_endpoints + 1);
    }
    if (id > fake_port_state.n_endpoints) {
        fake_port_state.n_endpoints = id;
    }
    fake_port_endpoint_t *endpoint = &fake_port_state.endpoints[id - 1];
    endpoint->parent_endpoint_id = parent_endpoint_id;
    endpoint->requested_endpoint_id = endpoint_id;
    endpoint->device_type_id = device_type_id;
    endpoint->device_type_version = device_type_version;
    endpoint->n_extra_device_types = 0;
    *out_endpoint_id = id;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __endpoint_device_type_add(uint16_t endpoint_id, uint32_t device_type_id,
        uint8_t device_type_version)
{
    (void)device_type_version;
    if (endpoint_id == 0 || endpoint_id > fake_port_state.n_endpoints) {
        return ESP_RMAKER_NOT_FOUND;
    }
    fake_port_endpoint_t *endpoint = &fake_port_state.endpoints[endpoint_id - 1];
    if (endpoint->n_extra_device_types >= FAKE_PORT_MAX_EXTRA_DEVICE_TYPES) {
        return ESP_RMAKER_NO_MEM;
    }
    endpoint->extra_device_types[endpoint->n_extra_device_types++] = device_type_id;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __cluster_create(uint16_t endpoint_id, uint32_t cluster_id, uint32_t feature_map)
{
    for (size_t i = 0; i < fake_port_state.n_clusters; i++) {
        if (fake_port_state.clusters[i].endpoint_id == endpoint_id &&
                fake_port_state.clusters[i].cluster_id == cluster_id) {
            /* Existing cluster: merge FeatureMap bits, like the real port */
            fake_port_state.clusters[i].feature_map |= feature_map;
            return ESP_RMAKER_OK;
        }
    }
    if (fake_port_state.n_clusters >= FAKE_PORT_MAX_CLUSTERS) {
        return ESP_RMAKER_NO_MEM;
    }
    fake_port_cluster_t *cluster = &fake_port_state.clusters[fake_port_state.n_clusters++];
    cluster->endpoint_id = endpoint_id;
    cluster->cluster_id = cluster_id;
    cluster->feature_map = feature_map;
    return ESP_RMAKER_OK;
}

static void __count_write_to_destroyed(uint16_t endpoint_id)
{
    for (size_t i = 0; i < fake_port_state.n_destroyed; i++) {
        if (fake_port_state.destroyed[i] == endpoint_id) {
            fake_port_state.n_writes_after_destroy++;
            return;
        }
    }
}

/* A write the cluster makes on its own, drained as the sync task can before the engine reseeds */
static void __report_write(uint16_t endpoint_id)
{
    rm_mirror_engine_handle_matter_update(endpoint_id, fake_port_state.enable_report.cluster_id,
                                          fake_port_state.enable_report.attribute_id,
                                          fake_port_state.enable_report.value);
    rm_mirror_engine_drain_pending();
}

static esp_rmaker_error_t __attribute_set(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t value, rm_mirror_val_type_t type)
{
    __count_write_to_destroyed(endpoint_id);
    if (fake_port_state.n_set_calls >= FAKE_PORT_MAX_ATTR_CALLS) {
        return ESP_RMAKER_NO_MEM;
    }
    fake_port_attr_call_t *call = &fake_port_state.set_calls[fake_port_state.n_set_calls++];
    call->endpoint_id = endpoint_id;
    call->cluster_id = cluster_id;
    call->attribute_id = attribute_id;
    call->value = value;
    call->type = type;
    if (fake_port_state.set_reports) {
        __report_write(endpoint_id);
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __attribute_update(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t value, rm_mirror_val_type_t type)
{
    __count_write_to_destroyed(endpoint_id);
    if (fake_port_state.n_update_calls >= FAKE_PORT_MAX_ATTR_CALLS) {
        return ESP_RMAKER_NO_MEM;
    }
    fake_port_attr_call_t *call = &fake_port_state.update_calls[fake_port_state.n_update_calls++];
    call->endpoint_id = endpoint_id;
    call->cluster_id = cluster_id;
    call->attribute_id = attribute_id;
    call->value = value;
    call->type = type;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __attribute_write_str(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, const char *value)
{
    __count_write_to_destroyed(endpoint_id);
    if (fake_port_state.n_write_str_calls >= FAKE_PORT_MAX_ATTR_CALLS) {
        return ESP_RMAKER_NO_MEM;
    }
    fake_port_str_call_t *call = &fake_port_state.write_str_calls[fake_port_state.n_write_str_calls++];
    call->endpoint_id = endpoint_id;
    call->cluster_id = cluster_id;
    call->attribute_id = attribute_id;
    strncpy(call->value, value, FAKE_PORT_STR_MAX);
    call->value[FAKE_PORT_STR_MAX] = '\0';
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __attribute_get(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id, int32_t *out)
{
    for (size_t i = 0; i < fake_port_state.n_stored_attrs; i++) {
        const fake_port_stored_attr_t *attr = &fake_port_state.stored_attrs[i];
        if (attr->endpoint_id != endpoint_id || attr->cluster_id != cluster_id ||
                attr->attribute_id != attribute_id) {
            continue;
        }
        if (attr->null) {
            return ESP_RMAKER_INVALID_STATE;
        }
        *out = attr->value;
        return ESP_RMAKER_OK;
    }
    return ESP_RMAKER_NOT_FOUND;
}

static esp_rmaker_error_t __attribute_defer_persistence(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id)
{
    __count_write_to_destroyed(endpoint_id);
    if (fake_port_state.n_deferred_calls >= FAKE_PORT_MAX_ATTR_CALLS) {
        return ESP_RMAKER_NO_MEM;
    }
    fake_port_attr_call_t *call = &fake_port_state.deferred_calls[fake_port_state.n_deferred_calls++];
    call->endpoint_id = endpoint_id;
    call->cluster_id = cluster_id;
    call->attribute_id = attribute_id;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __command_invoke(uint16_t endpoint_id, uint32_t cluster_id,
        const rm_mirror_write_cmd_t *cmd, int32_t value)
{
    __count_write_to_destroyed(endpoint_id);
    if (fake_port_state.invoke_fails) {
        return ESP_RMAKER_INVALID_STATE;
    }
    if (fake_port_state.n_invoke_calls >= FAKE_PORT_MAX_ATTR_CALLS
            || cmd->n_args > FAKE_PORT_MAX_CMD_ARGS) {
        return ESP_RMAKER_NO_MEM;
    }
    fake_port_invoke_call_t *call = &fake_port_state.invoke_calls[fake_port_state.n_invoke_calls++];
    call->endpoint_id = endpoint_id;
    call->cluster_id = cluster_id;
    call->command_id = cmd->command_id;
    call->n_args = cmd->n_args;
    for (size_t i = 0; i < cmd->n_args; i++) {
        call->args[i] = cmd->args[i].from_value ? value : cmd->args[i].literal;
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __report_as(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                      const char *kind, int32_t value)
{
    __count_write_to_destroyed(endpoint_id);
    if (fake_port_state.n_report_calls >= FAKE_PORT_MAX_ATTR_CALLS) {
        return ESP_RMAKER_NO_MEM;
    }
    fake_port_report_call_t *call = &fake_port_state.report_calls[fake_port_state.n_report_calls++];
    call->endpoint_id = endpoint_id;
    call->cluster_id = cluster_id;
    call->attribute_id = attribute_id;
    strncpy(call->kind, kind ? kind : "", FAKE_PORT_KIND_LEN - 1);
    call->kind[FAKE_PORT_KIND_LEN - 1] = '\0';
    call->value = value;
    return ESP_RMAKER_OK;
}

static bool __boot_is_software_update(void)
{
    return fake_port_state.boot_software_update;
}

static esp_rmaker_error_t __onboarding_state_get(rm_mirror_onboarding_state_t *out)
{
    *out = fake_port_state.onboarding_state;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __commissioning_window_open(uint16_t timeout_s)
{
    fake_port_state.n_window_opens++;
    fake_port_state.last_window_timeout_s = timeout_s;
    return ESP_RMAKER_OK;
}

static bool __stack_lock_held(void)
{
    return fake_port_state.stack_lock_held;
}

static esp_rmaker_error_t __endpoint_enable(uint16_t endpoint_id)
{
    if (fake_port_state.enable_fails) {
        return ESP_RMAKER_FAIL;
    }
    if (fake_port_state.enable_reports) {
        __report_write(endpoint_id);
    }
    if (fake_port_state.n_enabled < FAKE_PORT_MAX_ENDPOINTS) {
        fake_port_state.updates_at_enable[fake_port_state.n_enabled] = fake_port_state.n_update_calls;
        fake_port_state.sets_at_enable[fake_port_state.n_enabled] = fake_port_state.n_set_calls;
        fake_port_state.enabled[fake_port_state.n_enabled++] = endpoint_id;
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __endpoint_destroy(uint16_t endpoint_id)
{
    if (fake_port_state.n_destroyed < FAKE_PORT_MAX_ENDPOINTS) {
        fake_port_state.destroyed[fake_port_state.n_destroyed++] = endpoint_id;
    }
    return ESP_RMAKER_OK;
}

/* Matter Aggregator device type */
#define FAKE_PORT_AGGREGATOR_DEVICE_TYPE 0x000E

static esp_rmaker_error_t __aggregator_create(uint16_t *out_endpoint_id)
{
    if (!fake_port_state.bridge) {
        return ESP_RMAKER_NOT_SUPPORTED;
    }
    return __endpoint_create(RM_MIRROR_ENDPOINT_NONE, RM_MIRROR_ENDPOINT_NONE, FAKE_PORT_AGGREGATOR_DEVICE_TYPE, 2,
                             out_endpoint_id);
}

static esp_rmaker_error_t __endpoint_bridge(uint16_t endpoint_id, const char *unique_id, const char *label,
        bool reachable)
{
    if (endpoint_id == 0 || endpoint_id > fake_port_state.n_endpoints) {
        return ESP_RMAKER_NOT_FOUND;
    }
    fake_port_bridged_t *b = &fake_port_state.bridged[endpoint_id - 1];
    memset(b, 0, sizeof(*b));
    b->bridged = true;
    strncpy(b->unique_id, unique_id, FAKE_PORT_BDBI_STR_MAX);
    strncpy(b->label, label, FAKE_PORT_BDBI_STR_MAX);
    b->reachable = reachable;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __endpoint_reachable_set(uint16_t endpoint_id, bool reachable)
{
    if (endpoint_id == 0 || endpoint_id > fake_port_state.n_endpoints ||
            !fake_port_state.bridged[endpoint_id - 1].bridged) {
        return ESP_RMAKER_NOT_FOUND;
    }
    fake_port_state.bridged[endpoint_id - 1].reachable = reachable;
    fake_port_state.bridged[endpoint_id - 1].n_reachable_sets++;
    return ESP_RMAKER_OK;
}

static const rm_mirror_port_ops_t __ops = {
    .endpoint_create = __endpoint_create,
    .endpoint_device_type_add = __endpoint_device_type_add,
    .cluster_create = __cluster_create,
    .endpoint_enable = __endpoint_enable,
    .endpoint_destroy = __endpoint_destroy,
    .aggregator_create = __aggregator_create,
    .endpoint_bridge = __endpoint_bridge,
    .endpoint_reachable_set = __endpoint_reachable_set,
    .attribute_set = __attribute_set,
    .attribute_update = __attribute_update,
    .attribute_write_str = __attribute_write_str,
    .attribute_get = __attribute_get,
    .attribute_defer_persistence = __attribute_defer_persistence,
    .command_invoke = __command_invoke,
    .report_as = __report_as,
    .boot_is_software_update = __boot_is_software_update,
    .stack_lock_held = __stack_lock_held,
    .onboarding_state_get = __onboarding_state_get,
    .commissioning_window_open = __commissioning_window_open,
};

void fake_port_clear_calls(void)
{
    fake_port_state.n_set_calls = 0;
    fake_port_state.n_update_calls = 0;
    fake_port_state.n_write_str_calls = 0;
    fake_port_state.n_invoke_calls = 0;
    fake_port_state.n_report_calls = 0;
    fake_port_state.n_window_opens = 0;
}

const rm_mirror_port_ops_t *fake_port_get_ops(void)
{
    return &__ops;
}

void fake_port_reset(void)
{
    memset(&fake_port_state, 0, sizeof(fake_port_state));
}

const fake_port_attr_call_t *fake_port_find_set(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id)
{
    for (size_t i = 0; i < fake_port_state.n_set_calls; i++) {
        const fake_port_attr_call_t *call = &fake_port_state.set_calls[i];
        if (call->endpoint_id == endpoint_id && call->cluster_id == cluster_id &&
                call->attribute_id == attribute_id) {
            return call;
        }
    }
    return NULL;
}

bool fake_port_endpoint_has_device_type(uint16_t endpoint_id, uint32_t device_type_id)
{
    if (endpoint_id == 0 || endpoint_id > fake_port_state.n_endpoints) {
        return false;
    }
    const fake_port_endpoint_t *endpoint = &fake_port_state.endpoints[endpoint_id - 1];
    if (endpoint->device_type_id == device_type_id) {
        return true;
    }
    for (size_t i = 0; i < endpoint->n_extra_device_types; i++) {
        if (endpoint->extra_device_types[i] == device_type_id) {
            return true;
        }
    }
    return false;
}

bool fake_port_has_cluster(uint16_t endpoint_id, uint32_t cluster_id)
{
    for (size_t i = 0; i < fake_port_state.n_clusters; i++) {
        if (fake_port_state.clusters[i].endpoint_id == endpoint_id &&
                fake_port_state.clusters[i].cluster_id == cluster_id) {
            return true;
        }
    }
    return false;
}

bool fake_port_cluster_has_feature(uint16_t endpoint_id, uint32_t cluster_id, uint32_t feature_bits)
{
    for (size_t i = 0; i < fake_port_state.n_clusters; i++) {
        const fake_port_cluster_t *cluster = &fake_port_state.clusters[i];
        if (cluster->endpoint_id == endpoint_id && cluster->cluster_id == cluster_id) {
            return (cluster->feature_map & feature_bits) == feature_bits;
        }
    }
    return false;
}

void fake_port_store_attr(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                          int32_t value, bool null)
{
    if (fake_port_state.n_stored_attrs >= FAKE_PORT_MAX_STORED_ATTRS) {
        return;
    }
    fake_port_stored_attr_t *attr = &fake_port_state.stored_attrs[fake_port_state.n_stored_attrs++];
    attr->endpoint_id = endpoint_id;
    attr->cluster_id = cluster_id;
    attr->attribute_id = attribute_id;
    attr->value = value;
    attr->null = null;
}

bool fake_port_is_deferred(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id)
{
    for (size_t i = 0; i < fake_port_state.n_deferred_calls; i++) {
        const fake_port_attr_call_t *call = &fake_port_state.deferred_calls[i];
        if (call->endpoint_id == endpoint_id && call->cluster_id == cluster_id &&
                call->attribute_id == attribute_id) {
            return true;
        }
    }
    return false;
}

const fake_port_report_call_t *fake_port_find_report(uint16_t endpoint_id, uint32_t cluster_id,
        uint32_t attribute_id)
{
    for (size_t i = 0; i < fake_port_state.n_report_calls; i++) {
        const fake_port_report_call_t *call = &fake_port_state.report_calls[i];
        if (call->endpoint_id == endpoint_id && call->cluster_id == cluster_id &&
                call->attribute_id == attribute_id) {
            return call;
        }
    }
    return NULL;
}
