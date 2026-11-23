/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_group.c
 * @brief The group inventory: which nodes are in the group, and which subgroups each sits under.
 *
 * The cloud already reports every node's subgroup path in GroupInfo, so the SDK derives every
 * shadow name itself and an application never supplies one.
 *
 * Every shadow name is keyed on the group, so a migration invalidates the whole inventory. The
 * cloud reports one through getGroupInfo, which lands here as controller_group_notify_group_info().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osal_err.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"

#include "controller_hooks.h"
#include "controller_internal.h"
#include "sdkconfig.h"

static const char *TAG = "rmng_controller";

/** The inventory being built, before it replaces the live one. */
typedef struct {
    char group_name[CONTROLLER_GROUP_NAME_MAX_LEN];
    ctrl_node_t *nodes;
    ctrl_node_t **node_tail;
    ctrl_subgroup_t *subgroups;
    ctrl_subgroup_t **subgroup_tail;
} sync_ctx_t;

/* The group lists the controller itself. It drives the other members, so its own entry stays out
 * of the inventory. */
static bool __is_self(const char *node_id)
{
    const char *self = g_controller_state.thing_name;
    return self && strcmp(self, node_id) == 0;
}

static ctrl_node_t *__pending_node(sync_ctx_t *sync, const char *node_id)
{
    for (ctrl_node_t *node = sync->nodes; node; node = node->next) {
        if (strcmp(node->node_id, node_id) == 0) {
            return node;
        }
    }
    return NULL;
}

static esp_rmaker_error_t __on_node(void *ctx, const char *node_id)
{
    sync_ctx_t *sync = (sync_ctx_t *)ctx;
    if (__is_self(node_id) || __pending_node(sync, node_id)) {
        return ESP_RMAKER_OK;
    }
    ctrl_node_t *node = controller_model_node_alloc(node_id);
    if (!node) {
        return ESP_RMAKER_NO_MEM;
    }
    *sync->node_tail = node;
    sync->node_tail = &node->next;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __on_group(void *ctx, const char *group_name)
{
    sync_ctx_t *sync = (sync_ctx_t *)ctx;
    snprintf(sync->group_name, sizeof(sync->group_name), "%s", group_name);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __on_subgroup(void *ctx, const char *subgroup_id, const char *subgroup_name)
{
    sync_ctx_t *sync = (sync_ctx_t *)ctx;
    ctrl_subgroup_t *sg = controller_model_subgroup_alloc(subgroup_id, subgroup_name);
    if (!sg) {
        return ESP_RMAKER_NO_MEM;
    }
    *sync->subgroup_tail = sg;
    sync->subgroup_tail = &sg->next;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t __on_member(void *ctx, const char *subgroup_id, const char *node_id)
{
    sync_ctx_t *sync = (sync_ctx_t *)ctx;
    if (__is_self(node_id)) {
        return ESP_RMAKER_OK;
    }
    ctrl_node_t *node = __pending_node(sync, node_id);
    if (!node) {
        /* A subgroup member the group's own node_ids does not list is the cloud's inconsistency,
         * not something to fail a sync over. */
        OSAL_LOGW(TAG, "Subgroup '%s' lists node '%s', which the group does not", subgroup_id, node_id);
        return ESP_RMAKER_OK;
    }
    /* An over-full or oversized path is logged by the model and does not fail the sync. */
    (void)controller_model_node_add_subgroup(node, subgroup_id);
    return ESP_RMAKER_OK;
}

/* Sync ************************************************************************/

typedef char shadow_name_t[CONTROLLER_SHADOW_NAME_BUFFER_SIZE];

/* Under the model lock: copy the shadow names in use. The caller frees the array. */
static shadow_name_t *__snapshot_shadows(size_t *num)
{
    *num = 0;
    size_t max = controller_model_count_nodes(controller_model_node_head());
    shadow_name_t *names = max ? OSAL_CALLOC_EXTRAM(max, sizeof(shadow_name_t)) : NULL;
    if (names) {
        *num = controller_model_collect_shadows(controller_model_node_head(), names, max);
    } else if (max) {
        OSAL_LOGW(TAG, "No memory to track the shadows in use; stale filters may be left behind");
    }
    return names;
}

/* Forget the inventory of a group the node left, and drop every shadow filter it held. Returns
 * true when an inventory was held. */
static bool __drop_inventory(void)
{
    esp_rmaker_controller_state_t *st = &g_controller_state;
    if (controller_model_lock() != ESP_RMAKER_OK) {
        return false;
    }
    bool held = controller_model_node_head() || st->group_id[0] != '\0';
    size_t num_prev = 0;
    shadow_name_t *prev_shadows = __snapshot_shadows(&num_prev);
    if (held) {
        controller_model_replace_inventory(NULL, NULL);
        st->group_id[0] = '\0';
        st->group_name[0] = '\0';
        st->synced_group_id[0] = '\0';
    }
    controller_model_unlock();

    for (size_t i = 0; i < num_prev; i++) {
        OSAL_LOGI(TAG, "Dropping shadow '%s' of the group this node left", prev_shadows[i]);
        (void)controller_watch_unsubscribe_shadow(prev_shadows[i]);
    }
    free(prev_shadows);
    return held;
}

/* One inventory sync, against the group the node holds now. @p dropped reports whether the node
 * was in no group and a held inventory was dropped. */
static esp_rmaker_error_t __group_sync(bool *dropped)
{
    *dropped = false;
    esp_rmaker_controller_state_t *st = &g_controller_state;
    if (!st->enabled) {
        return ESP_RMAKER_NOT_INITIALIZED;
    }
    char group_id[CONTROLLER_GROUP_ID_MAX_LEN];
    char *stored = NULL;
    esp_rmaker_error_t err = controller_group_id(&stored);
    if (err == ESP_RMAKER_NOT_FOUND) {
        /* The node left its group, so the inventory and its filters name a group it is not in. */
        *dropped = __drop_inventory();
        return err;
    }
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    bool fits = strlen(stored) < sizeof(group_id);
    if (fits) {
        snprintf(group_id, sizeof(group_id), "%s", stored);
    }
    free(stored);
    if (!fits) {
        return ESP_RMAKER_INVALID_ARG;
    }

    char path[CONTROLLER_REST_PATH_MAX_LEN];
    if (snprintf(path, sizeof(path), "/v1/groups/%s", group_id) >= (int)sizeof(path)) {
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_aws_sign_response_t resp = {0};
    err = controller_rest_get(path, &resp);
    if (err == ESP_RMAKER_OK) {
        err = controller_rest_status_to_error(path, resp.status_code);
    }
    if (err != ESP_RMAKER_OK) {
        esp_rmaker_aws_sign_response_free(&resp);
        return err;
    }

    sync_ctx_t sync = {0};
    sync.node_tail = &sync.nodes;
    sync.subgroup_tail = &sync.subgroups;
    const controller_group_info_cbs_t cbs = {
        .group = __on_group,
        .node = __on_node,
        .subgroup = __on_subgroup,
        .member = __on_member,
    };
    err = controller_parse_group_info((const char *)resp.body, resp.body_len, &cbs, &sync);
    esp_rmaker_aws_sign_response_free(&resp);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to parse group info: %s", osal_err_strerror(err));
        goto fail;
    }

    /* The shadow name follows the sorted subgroup path, which is what the cloud keys it on. */
    for (ctrl_node_t *node = sync.nodes; node; node = node->next) {
        err = controller_model_node_set_group(node, group_id);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to derive the shadow name for node '%s'", node->node_id);
            goto fail;
        }
    }

    err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        goto fail;
    }
    /* Snapshot the names in use, so the ones the new inventory drops can be unsubscribed below. */
    size_t num_prev = 0;
    shadow_name_t *prev_shadows = __snapshot_shadows(&num_prev);
    controller_model_replace_inventory(sync.nodes, sync.subgroups);
    snprintf(st->group_id, sizeof(st->group_id), "%s", group_id);
    controller_model_unlock();

    /* The replace freed each fresh node that duplicated a survivor, so walk the live list. Only
     * this task frees nodes, so the walk needs no lock. */
    ctrl_node_t *nodes = controller_model_node_head();

    /* Subscribe after the model holds the nodes, so an inbound delta finds its node. Nodes
     * sharing a shadow name cost one subscription: the manager holds the rest. */
    size_t num_nodes = 0;
    for (ctrl_node_t *node = nodes; node; node = node->next) {
        num_nodes++;
        if (node->shadow[0] == '\0') {
            continue;
        }
        esp_rmaker_error_t sub_err = controller_watch_subscribe_shadow(node->shadow);
        if (sub_err != ESP_RMAKER_OK) {
            /* The model is already current; report the failure but keep what was synced. */
            OSAL_LOGE(TAG, "Failed to observe shadow '%s': %s", node->shadow, osal_err_strerror(sub_err));
            err = sub_err;
        }
    }

    /* A filter the new inventory does not use is replayed on every reconnect, and after a
     * migration the refreshed policy no longer authorizes it. */
    for (size_t i = 0; i < num_prev; i++) {
        if (controller_model_list_uses_shadow(nodes, prev_shadows[i])) {
            continue;
        }
        OSAL_LOGI(TAG, "Dropping stale shadow '%s'", prev_shadows[i]);
        (void)controller_watch_unsubscribe_shadow(prev_shadows[i]);
    }
    free(prev_shadows);

    if (err == ESP_RMAKER_OK) {
        st->synced = true;
        if (controller_model_lock() == ESP_RMAKER_OK) {
            snprintf(st->group_name, sizeof(st->group_name), "%s", sync.group_name);
            controller_model_unlock();
        }
        snprintf(st->synced_group_id, sizeof(st->synced_group_id), "%s", group_id);
    }
    OSAL_LOGI(TAG, "Synced group %s: %zu node(s)", group_id, num_nodes);
    return err;

fail:
    controller_model_free_node_list(sync.nodes);
    controller_model_free_subgroup_list(sync.subgroups);
    return err;
}

esp_rmaker_error_t controller_group_sync(void *unused)
{
    (void)unused;
    bool dropped;
    return __group_sync(&dropped);
}

/* Migration *******************************************************************/

/* The inventory the controller holds is keyed on the group, so a migration invalidates every
 * shadow name derived from it. Runs on the controller task, which owns every field it reads. */
static esp_rmaker_error_t __resync(bool migrated)
{
    esp_rmaker_controller_state_t *st = &g_controller_state;
    if (!st->enabled || !st->synced) {
        /* Nothing is held, so nothing is stale. The application's own sync reads the new group. */
        return ESP_RMAKER_OK;
    }
    if (migrated) {
        /* Recorded before the re-sync, so a re-sync that fails still leaves the inventory marked
         * stale for the next getGroupInfo to retry. */
        st->synced_group_id[0] = '\0';
    } else {
        char *group_id = NULL;
        esp_rmaker_error_t group_err = controller_group_id(&group_id);
        bool current = group_err == ESP_RMAKER_OK ? strcmp(st->synced_group_id, group_id) == 0
                       : group_err == ESP_RMAKER_NOT_FOUND && st->group_id[0] == '\0';
        free(group_id);
        if (current) {
            return ESP_RMAKER_OK;
        }
    }

    OSAL_LOGI(TAG, "Inventory no longer matches the group; re-syncing");
    /* The rebuild queues its change list, which the job runner delivers after this job. */
    bool dropped;
    esp_rmaker_error_t err = __group_sync(&dropped);
    if (dropped) {
        return ESP_RMAKER_OK;
    }
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Migration re-sync failed: %s", osal_err_strerror(err));
    }
    return err;
}

esp_rmaker_error_t controller_group_resync_migrated(void *unused)
{
    (void)unused;
    return __resync(true);
}

esp_rmaker_error_t controller_group_resync_check(void *unused)
{
    (void)unused;
    return __resync(false);
}

void controller_group_notify_group_info(bool migrated)
{
    if (!g_controller_state.enabled) {
        return;
    }
    /* Async: the caller is the cloud event task, and a re-sync makes REST calls. */
    (void)controller_task_submit_async(migrated ? controller_group_resync_migrated
                                       : controller_group_resync_check, NULL);
}

void controller_group_unwatch_all(void)
{
    if (controller_model_lock() != ESP_RMAKER_OK) {
        return;
    }
    size_t num = 0;
    shadow_name_t *shadows = __snapshot_shadows(&num);
    controller_model_unlock();
    for (size_t i = 0; i < num; i++) {
        (void)controller_watch_unsubscribe_shadow(shadows[i]);
    }
    free(shadows);
}

/* Public API ******************************************************************/

esp_rmaker_error_t esp_rmaker_controller_group_sync(void)
{
    return controller_task_run_sync(controller_group_sync, NULL);
}

esp_rmaker_error_t esp_rmaker_controller_get_group_name(char **out)
{
    if (!out) {
        return ESP_RMAKER_INVALID_ARG;
    }
    *out = NULL;
    esp_rmaker_error_t err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    err = controller_model_copy_string(g_controller_state.group_name, out);
    controller_model_unlock();
    return err;
}
