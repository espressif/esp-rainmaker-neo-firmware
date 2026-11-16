/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_lowering.c
 * @brief Lowering: walk the RainMaker Neo node and construct the Matter tree via the
 *        port, per the generated mapping tables.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_rmaker_data_model_introspect.h"
#include "esp_rmaker_val.h"

#include "rm_mirror_engine.h"
#include "rm_mirror_internal.h"

#include "osal_log.h"

static const char *TAG = "rm_mirror_lowering";

esp_rmaker_error_t rm_mirror_param_val_as_i32(const esp_rmaker_param_t *param, int32_t *out)
{
    esp_rmaker_param_val_t *val = esp_rmaker_param_get_val((esp_rmaker_param_t *)param);
    if (!val) {
        return ESP_RMAKER_FAIL;
    }
    switch (val->type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        *out = val->val.b ? 1 : 0;
        return ESP_RMAKER_OK;
    case RMAKER_VAL_TYPE_INTEGER:
        *out = val->val.i;
        return ESP_RMAKER_OK;
    default:
        return ESP_RMAKER_NOT_SUPPORTED;
    }
}

esp_rmaker_error_t rm_mirror_param_to_matter(const esp_rmaker_param_t *param,
        const rm_mirror_capability_t *cap, int32_t *out)
{
    esp_rmaker_param_val_t *val = esp_rmaker_param_get_val((esp_rmaker_param_t *)param);
    if (!val) {
        return ESP_RMAKER_FAIL;
    }
    switch (val->type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        *out = rm_mirror_xform_to_matter(&cap->xform, val->val.b ? 1 : 0);
        return ESP_RMAKER_OK;
    case RMAKER_VAL_TYPE_INTEGER:
        *out = rm_mirror_xform_to_matter(&cap->xform, val->val.i);
        return ESP_RMAKER_OK;
    case RMAKER_VAL_TYPE_FLOAT:
        *out = rm_mirror_xform_to_matter_f(&cap->xform, val->val.f);
        return ESP_RMAKER_OK;
    default:
        return ESP_RMAKER_NOT_SUPPORTED;
    }
}

/* Set during an init lowering run */
static bool __init_run;

void rm_mirror_lowering_drop_endpoint(const rm_mirror_port_ops_t *port, uint16_t endpoint_id)
{
    rm_mirror_endpoint_map_release(endpoint_id);
    rm_mirror_endpoint_map_commit();
    if (port->endpoint_destroy && port->endpoint_destroy(endpoint_id) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Could not destroy endpoint %u.", (unsigned)endpoint_id);
    }
}

/** How a device is lowered: at init, or into a running stack. */
typedef struct {
    const rm_mirror_port_ops_t *port;
    const rm_mirror_devtype_rule_t *rule;
    uint16_t endpoint_id;
    bool adopt;  /* boot-state attributes hold this endpoint's state from the previous boot */
} __lower_ctx_t;

static esp_rmaker_error_t __lower_capability(const __lower_ctx_t *ctx, const esp_rmaker_param_t *param,
        const rm_mirror_capability_t *cap)
{
    const rm_mirror_port_ops_t *port = ctx->port;
    const uint16_t endpoint_id = ctx->endpoint_id;
    esp_rmaker_error_t err = port->cluster_create(endpoint_id, cap->cluster_id, cap->feature_map);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    /* Feed bounds-derived attributes: the param's min/max bound, transformed, lands on
     * the attribute the mapping names for it (CCT: kelvin min -> PhysicalMaxMireds) */
    esp_rmaker_param_val_t min, max;
    if ((cap->bounds_min_attr || cap->bounds_max_attr) &&
            esp_rmaker_param_get_bounds(param, &min, &max, NULL) == ESP_RMAKER_OK &&
            min.type == RMAKER_VAL_TYPE_INTEGER) {
        if (cap->bounds_min_attr) {
            port->attribute_set(endpoint_id, cap->cluster_id, cap->bounds_min_attr,
                                rm_mirror_xform_to_matter(&cap->xform, min.val.i), cap->bounds_value_type);
        }
        if (cap->bounds_max_attr) {
            port->attribute_set(endpoint_id, cap->cluster_id, cap->bounds_max_attr,
                                rm_mirror_xform_to_matter(&cap->xform, max.val.i), cap->bounds_value_type);
        }
    }

    /* Seed the attribute from the current param value and register the binding */
    int32_t matter_val = 0;
    esp_rmaker_error_t val_err = rm_mirror_param_to_matter(param, cap, &matter_val);
    if (val_err != ESP_RMAKER_OK) {
        return val_err;
    }

    /* A capability with a startup policy carries the node's boot state, and the port keeps that
     * attribute across reboots: on any boot but the first it already holds the state the node had
     * when it went down. Adopt that rather than overwrite it with the param's compiled-in default,
     * which is all the param can hold this early - the injection below hands it to the
     * application, and the StartUp* policies are then resolved against a real previous value. */
    int32_t restored = 0;
    const bool adopt = cap->startup_policy != RM_MIRROR_STARTUP_NONE && port->attribute_get && ctx->adopt &&
                       port->attribute_get(endpoint_id, cap->cluster_id, cap->attribute_id,
                                           &restored) == ESP_RMAKER_OK;
    if (adopt) {
        matter_val = restored;
    } else if (rm_mirror_param_holds_state(param)) {
        rm_mirror_port_push(port, endpoint_id, cap, matter_val, true);
    }

    /* After the seed, so that write still persists immediately; every write from
     * here on (all of them post-start) gets the deferral. */
    if (cap->deferred_persistence && port->attribute_defer_persistence) {
        port->attribute_defer_persistence(endpoint_id, cap->cluster_id, cap->attribute_id);
    }

    esp_rmaker_error_t bind_err = rm_mirror_sync_add_binding(param, cap, endpoint_id, matter_val);
    if (bind_err == ESP_RMAKER_OK && adopt) {
        rm_mirror_engine_apply_boot_value(endpoint_id, cap->cluster_id, cap->attribute_id, matter_val);
    }
    return bind_err;
}

/**
 * @brief Whether @p param_type appears in the rule's params or optional_params.
 */
static bool __rule_lists_param(const rm_mirror_devtype_rule_t *rule, const char *param_type)
{
    for (size_t i = 0; i < rule->n_params; i++) {
        if (strcmp(rule->param_types[i], param_type) == 0) {
            return true;
        }
    }
    for (size_t i = 0; i < rule->n_optional_params; i++) {
        if (strcmp(rule->optional_params[i], param_type) == 0) {
            return true;
        }
    }
    return false;
}

#if RM_MIRROR_USES_COMPOSITES
/** Whether every member of @p comp is rule-listed and present on @p device; fills the members and their values. */
static bool __composite_members(const rm_mirror_devtype_rule_t *rule, const esp_rmaker_device_t *device,
                                const rm_mirror_composite_t *comp, const esp_rmaker_param_t **members,
                                int32_t *rmng_vals)
{
    if (comp->n_params > RM_MIRROR_COMPOSITE_MAX_PARAMS || comp->n_attributes > RM_MIRROR_COMPOSITE_MAX_ATTRS) {
        return false;
    }
    for (size_t i = 0; i < comp->n_params; i++) {
        if (!__rule_lists_param(rule, comp->param_types[i])) {
            return false;
        }
        members[i] = esp_rmaker_device_get_param_by_type(device, comp->param_types[i]);
        if (!members[i] || rm_mirror_param_val_as_i32(members[i], &rmng_vals[i]) != ESP_RMAKER_OK) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Lower every composite whose member params are all present (and
 *        rule-listed) on this endpoint: enable its cluster features, seed the
 *        derived attributes from current param values, register the binding.
 */
static esp_rmaker_error_t __lower_composites(const __lower_ctx_t *ctx, const esp_rmaker_device_t *device)
{
    const rm_mirror_port_ops_t *port = ctx->port;
    const rm_mirror_devtype_rule_t *rule = ctx->rule;
    const uint16_t endpoint_id = ctx->endpoint_id;
    for (size_t c = 0; c < rm_mirror_n_composites; c++) {
        const rm_mirror_composite_t *comp = &rm_mirror_composites[c];
        const esp_rmaker_param_t *members[RM_MIRROR_COMPOSITE_MAX_PARAMS] = {0};
        int32_t rmng_vals[RM_MIRROR_COMPOSITE_MAX_PARAMS] = {0};
        if (!__composite_members(rule, device, comp, members, rmng_vals)) {
            continue;
        }

        esp_rmaker_error_t err = port->cluster_create(endpoint_id, comp->cluster_id, comp->feature_map);
        if (err != ESP_RMAKER_OK) {
            return err;
        }

        /* Seed the derived attributes from the members' current values */
        int32_t matter_vals[RM_MIRROR_COMPOSITE_MAX_ATTRS] = {0};
        rm_mirror_composite_to_matter(comp, rmng_vals, matter_vals);
        for (size_t i = 0; i < comp->n_attributes; i++) {
            port->attribute_set(endpoint_id, comp->cluster_id, comp->attribute_ids[i], matter_vals[i],
                                comp->value_types[i]);
            if (comp->deferred_persistence && port->attribute_defer_persistence) {
                port->attribute_defer_persistence(endpoint_id, comp->cluster_id, comp->attribute_ids[i]);
            }
        }

        err = rm_mirror_sync_add_composite_binding(comp, members, endpoint_id, matter_vals);
        if (err != ESP_RMAKER_OK) {
            return err;
        }
        OSAL_LOGI(TAG, "Composite %s lowered on endpoint %u.", comp->id, endpoint_id);
    }
    return ESP_RMAKER_OK;
}
#endif /* RM_MIRROR_USES_COMPOSITES */

static bool __device_has_param(const char *param_type, void *ctx)
{
    return esp_rmaker_device_get_param_by_type((const esp_rmaker_device_t *)ctx, param_type) != NULL;
}

/** Capability @p param lowers to on an endpoint of @p rule, or NULL when it stays cloud-only. */
static const rm_mirror_capability_t *__param_capability(const rm_mirror_devtype_rule_t *rule,
        const esp_rmaker_param_t *param, bool log)
{
    const char *param_type = esp_rmaker_param_get_type((esp_rmaker_param_t *)param);
    const rm_mirror_capability_t *cap = param_type ? rm_mirror_table_find_capability(param_type) : NULL;
    if (!cap || cap->node_scoped) {
        return NULL; /* custom/unmapped param (cloud-only by design), or node-scoped
                      * (bound on endpoint 0 by the engine, not per device endpoint) */
    }
    if (cap->rule_gated && !__rule_lists_param(rule, cap->param_type)) {
        /* Present on the device but not part of the matched rule's shape: stays
         * cloud-only. Keeps e.g. ColorControl off device types whose Matter
         * definition forbids it. */
        if (log) {
            OSAL_LOGI(TAG, "Param type %s not in the matched rule; cloud-only.", cap->param_type);
        }
        return NULL;
    }
    int32_t unused;
    if (rm_mirror_param_to_matter(param, cap, &unused) == ESP_RMAKER_NOT_SUPPORTED) {
        if (log) {
            OSAL_LOGW(TAG, "Param type %s not mirrorable (bool/int/float only); cloud-only.", cap->param_type);
        }
        return NULL;
    }
    return cap;
}

static esp_rmaker_error_t __lower_param(const esp_rmaker_param_t *param, void *priv)
{
    const __lower_ctx_t *ctx = (const __lower_ctx_t *)priv;
    const rm_mirror_capability_t *cap = __param_capability(ctx->rule, param, true);
    return cap ? __lower_capability(ctx, param, cap) : ESP_RMAKER_OK;
}

/* Stops the param walk at the first param that binds */
static esp_rmaker_error_t __param_binds(const esp_rmaker_param_t *param, void *priv)
{
    return __param_capability((const rm_mirror_devtype_rule_t *)priv, param, false)
           ? ESP_RMAKER_ALREADY_EXISTS : ESP_RMAKER_OK;
}

/** Whether lowering @p device on @p rule binds a param or a composite. */
static bool __device_binds(const rm_mirror_devtype_rule_t *rule, const esp_rmaker_device_t *device)
{
    if (esp_rmaker_device_for_each_param(device, __param_binds, (void *)rule) == ESP_RMAKER_ALREADY_EXISTS) {
        return true;
    }
#if RM_MIRROR_USES_COMPOSITES
    for (size_t c = 0; c < rm_mirror_n_composites; c++) {
        const esp_rmaker_param_t *members[RM_MIRROR_COMPOSITE_MAX_PARAMS];
        int32_t rmng_vals[RM_MIRROR_COMPOSITE_MAX_PARAMS];
        if (__composite_members(rule, device, &rm_mirror_composites[c], members, rmng_vals)) {
            return true;
        }
    }
#endif
    return false;
}

/** Node ID part of the device's map key: NULL for the mirrored node, as its ID can first appear at claim. */
static esp_rmaker_error_t __map_node_id(const esp_rmaker_device_t *device, char **node_id)
{
    *node_id = NULL;
    const esp_rmaker_node_t *node = esp_rmaker_device_get_node(device);
    if (node == rm_mirror_engine_get_node()) {
        return ESP_RMAKER_OK;
    }
    *node_id = node ? esp_rmaker_get_node_id_for_node(node) : NULL;
    return *node_id ? ESP_RMAKER_OK : ESP_RMAKER_INVALID_ARG;
}

static esp_rmaker_error_t __lower_device_keyed(const rm_mirror_port_ops_t *port, const char *node_id,
        const esp_rmaker_device_t *device, bool live, uint16_t *endpoint_id_out)
{
    const char *device_type = esp_rmaker_device_get_type(device);
    if (!device_type) {
        OSAL_LOGI(TAG, "Device '%s' has no type; not mirrored.", esp_rmaker_device_get_id(device));
        return ESP_RMAKER_OK;
    }

    const rm_mirror_devtype_rule_t *rule = rm_mirror_table_match_rule(device_type, __device_has_param,
                                           (void *)device);
    if (!rule) {
        OSAL_LOGI(TAG, "No mapping rule for device '%s' (%s); not mirrored.",
                  esp_rmaker_device_get_id(device), device_type);
        return ESP_RMAKER_OK;
    }
    /* A rule can match a device none of whose params the mirror carries */
    if (!__device_binds(rule, device)) {
        OSAL_LOGI(TAG, "Device '%s' binds no param; not mirrored.", esp_rmaker_device_get_id(device));
        return ESP_RMAKER_OK;
    }

    const char *device_id = esp_rmaker_device_get_id(device);
    /* Before start the port gives IDs in creation order only */
    const uint16_t kept_id = __init_run ? RM_MIRROR_ENDPOINT_NONE : rm_mirror_endpoint_map_find(node_id, device_id);
    const uint16_t parent_id = node_id ? rm_mirror_bridge_aggregator() : RM_MIRROR_ENDPOINT_NONE;
    uint16_t endpoint_id = 0;
    esp_rmaker_error_t err = ESP_RMAKER_FAIL;
    if (kept_id != RM_MIRROR_ENDPOINT_NONE) {
        err = port->endpoint_create(parent_id, kept_id, rule->matter_device_type_id,
                                    rule->matter_device_type_version, &endpoint_id);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGW(TAG, "Device '%s' cannot have endpoint %u again; it takes a new one.", device_id,
                      (unsigned)kept_id);
        }
    }
    if (err != ESP_RMAKER_OK) {
        err = port->endpoint_create(parent_id, RM_MIRROR_ENDPOINT_NONE, rule->matter_device_type_id,
                                    rule->matter_device_type_version, &endpoint_id);
    }
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to create endpoint for device '%s'.", device_id);
        return err;
    }
    if (endpoint_id_out) {
        *endpoint_id_out = endpoint_id;
    }
    if (live) {
        rm_mirror_sync_set_dormant(endpoint_id);
    }
    OSAL_LOGI(TAG, "Device '%s' lowered to endpoint %u (device type 0x%04X).",
              esp_rmaker_device_get_id(device), endpoint_id, (unsigned)rule->matter_device_type_id);

    if (parent_id != RM_MIRROR_ENDPOINT_NONE) {
        err = rm_mirror_bridge_endpoint(port, endpoint_id, node_id, device);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to make endpoint %u a bridged node.", (unsigned)endpoint_id);
            return err;
        }
    }

    for (size_t i = 0; i < rule->n_mandatory_clusters; i++) {
        err = port->cluster_create(endpoint_id, rule->mandatory_clusters[i], 0);
        if (err != ESP_RMAKER_OK) {
            return err;
        }
    }

    /* Clusters that belong to a device type of their own (electrical measurement to an Electrical
     * Sensor, say) only count as declared once the endpoint carries that device type too. */
    for (size_t i = 0; i < rule->n_extra_device_types; i++) {
        const rm_mirror_extra_devtype_t *extra = &rule->extra_device_types[i];
        if (!port->endpoint_device_type_add) {
            OSAL_LOGE(TAG, "Rule needs device type 0x%04X on the endpoint, which the port cannot add.",
                      (unsigned)extra->device_type_id);
            return ESP_RMAKER_NOT_SUPPORTED;
        }
        err = port->endpoint_device_type_add(endpoint_id, extra->device_type_id, extra->device_type_version);
        if (err != ESP_RMAKER_OK) {
            return err;
        }
        for (size_t c = 0; c < extra->n_mandatory_clusters; c++) {
            err = port->cluster_create(endpoint_id, extra->mandatory_clusters[c], 0);
            if (err != ESP_RMAKER_OK) {
                return err;
            }
        }
        OSAL_LOGI(TAG, "Endpoint %u also declares device type 0x%04X.", endpoint_id,
                  (unsigned)extra->device_type_id);
    }

    const bool lowered_before = rm_mirror_endpoint_map_assign(node_id, device_id, endpoint_id);
    __lower_ctx_t ctx = {
        .port = port, .rule = rule, .endpoint_id = endpoint_id,
        .adopt = lowered_before,
    };
    err = esp_rmaker_device_for_each_param(device, __lower_param, &ctx);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

#if RM_MIRROR_USES_COMPOSITES
    err = __lower_composites(&ctx, device);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
#endif
    rm_mirror_endpoint_map_set_lowered(endpoint_id);
    if (!__init_run) {
        rm_mirror_endpoint_map_commit();
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t rm_mirror_lowering_lower_device(const rm_mirror_port_ops_t *port, const esp_rmaker_device_t *device,
        bool live, uint16_t *endpoint_id_out)
{
    if (endpoint_id_out) {
        *endpoint_id_out = 0;
    }
    char *node_id = NULL;
    esp_rmaker_error_t err = __map_node_id(device, &node_id);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Device '%s' is in no node with a node ID; not mirrored.", esp_rmaker_device_get_id(device));
        return err;
    }
    err = __lower_device_keyed(port, node_id, device, live, endpoint_id_out);
    free(node_id);
    return err;
}

esp_rmaker_error_t rm_mirror_lowering_store_reachable(const esp_rmaker_device_t *device, bool reachable)
{
    char *node_id = NULL;
    esp_rmaker_error_t err = __map_node_id(device, &node_id);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    if (!node_id || rm_mirror_bridge_aggregator() == RM_MIRROR_ENDPOINT_NONE) {
        free(node_id);
        return ESP_RMAKER_NOT_SUPPORTED;
    }
    err = rm_mirror_bridge_store_reachable(node_id, esp_rmaker_device_get_id(device), reachable);
    free(node_id);
    return err;
}

static esp_rmaker_error_t __lower_device(const esp_rmaker_device_t *device, void *priv)
{
    if (esp_rmaker_device_is_service(device)) {
        return ESP_RMAKER_OK;
    }
    return rm_mirror_lowering_lower_device((const rm_mirror_port_ops_t *)priv, device, false, NULL);
}

esp_rmaker_error_t rm_mirror_lowering_run(const esp_rmaker_node_t *node, const rm_mirror_port_ops_t *port)
{
    __init_run = true;
    esp_rmaker_error_t err = esp_rmaker_node_for_each_device(node, __lower_device, (void *)port);
    __init_run = false;
    if (err == ESP_RMAKER_OK) {
        rm_mirror_endpoint_map_commit();
    }
    return err;
}
