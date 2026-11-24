/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_model.c
 * @brief The cached group graph and the public handle accessors over it.
 *
 * Each public accessor takes the model lock, resolves its handle, reads, and releases the lock.
 */

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_semaphore.h"
#include "osal_ticks.h"

#include "local_config.h"

#include "controller_internal.h"

static const char *TAG = "rmng_controller";

/* Never deleted, so an MQTT callback that races a disable cannot take a freed lock. */
static osal_semaphore_handle_t s_lock;
static atomic_bool s_open;
static ctrl_node_t *s_nodes;
static ctrl_subgroup_t *s_subgroups;

/* Lock ************************************************************************/

esp_rmaker_error_t controller_model_init(void)
{
    if (!s_lock) {
        s_lock = osal_semaphore_create_recursive_mutex();
    }
    if (!s_lock) {
        return ESP_RMAKER_NO_MEM;
    }
    atomic_store(&s_open, true);
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t controller_model_lock(void)
{
    if (!s_lock || !atomic_load(&s_open)) {
        return ESP_RMAKER_NOT_INITIALIZED;
    }
    return osal_semaphore_take_recursive(s_lock, OSAL_MAX_DELAY) == OSAL_ERR_OK
           ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

void controller_model_unlock(void)
{
    if (s_lock) {
        osal_semaphore_give_recursive(s_lock);
    }
}

/* Allocation ******************************************************************/

/* Each free releases the slot first, so a handle never resolves to freed memory. */
static void __param_free(ctrl_param_t *param)
{
    controller_handle_free(param->obj.handle);
    free(param->id);
    free(param->type);
    free(param->bounds);
    if (param->val_valid) {
        esp_rmaker_val_free(&param->val);
    }
    free(param);
}

void controller_model_free_param_list(ctrl_param_t *head)
{
    while (head) {
        ctrl_param_t *next = head->next;
        __param_free(head);
        head = next;
    }
}

static void __device_free(ctrl_device_t *dev)
{
    controller_handle_free(dev->obj.handle);
    controller_model_free_param_list(dev->params);
    free(dev->id);
    free(dev->type);
    free(dev->primary);
    free(dev);
}

void controller_model_free_device_list(ctrl_device_t *head)
{
    while (head) {
        ctrl_device_t *next = head->next;
        __device_free(head);
        head = next;
    }
}

void controller_model_batch_clear(ctrl_node_t *node)
{
    ctrl_staged_write_t *staged = node->batch;
    while (staged) {
        ctrl_staged_write_t *next = staged->next;
        esp_rmaker_val_free(&staged->val);
        free(staged);
        staged = next;
    }
    node->batch = NULL;
    node->batch_open = false;
}

void controller_model_release_config(ctrl_node_t *node)
{
    controller_model_free_device_list(node->devices);
    node->devices = NULL;
    node->config_held = false;
    controller_model_batch_clear(node);
}

static void __node_free(ctrl_node_t *node)
{
    controller_handle_free(node->obj.handle);
    controller_model_release_config(node);
    free(node);
}

static void __subgroup_free(ctrl_subgroup_t *sg)
{
    controller_handle_free(sg->obj.handle);
    free(sg);
}

void controller_model_free_node_list(ctrl_node_t *head)
{
    while (head) {
        ctrl_node_t *next = head->next;
        __node_free(head);
        head = next;
    }
}

void controller_model_free_subgroup_list(ctrl_subgroup_t *head)
{
    while (head) {
        ctrl_subgroup_t *next = head->next;
        __subgroup_free(head);
        head = next;
    }
}

static void __collect_removed(ctrl_changes_t *changes, const ctrl_node_t *nodes, const ctrl_subgroup_t *subgroups)
{
    for (const ctrl_node_t *node = nodes; node; node = node->next) {
        controller_changes_add(changes, ESP_RMAKER_CONTROLLER_REMOVED, ESP_RMAKER_CONTROLLER_KIND_NODE,
                               node->obj.handle, 0);
    }
    for (const ctrl_subgroup_t *sg = subgroups; sg; sg = sg->next) {
        controller_changes_add(changes, ESP_RMAKER_CONTROLLER_REMOVED, ESP_RMAKER_CONTROLLER_KIND_SUBGROUP,
                               sg->obj.handle, 0);
    }
}

void controller_model_deinit(void)
{
    if (controller_model_lock() == ESP_RMAKER_OK) {
        ctrl_changes_t changes;
        ctrl_changes_t *collect = controller_changes_begin(&changes);
        __collect_removed(collect, s_nodes, s_subgroups);
        controller_changes_commit(collect);
        controller_model_free_node_list(s_nodes);
        controller_model_free_subgroup_list(s_subgroups);
        s_nodes = NULL;
        s_subgroups = NULL;
        controller_model_unlock();
        /* The controller task is parked, so the lists go out on this task. */
        controller_changes_drain();
    }
    atomic_store(&s_open, false);
}

ctrl_node_t *controller_model_node_alloc(const char *node_id)
{
    if (!node_id || node_id[0] == '\0') {
        return NULL;
    }
    ctrl_node_t *node = OSAL_CALLOC_EXTRAM(1, sizeof(*node));
    if (!node) {
        return NULL;
    }
    if (snprintf(node->node_id, sizeof(node->node_id), "%s", node_id) >= (int)sizeof(node->node_id)) {
        OSAL_LOGE(TAG, "Node id '%s' does not fit", node_id);
        free(node);
        return NULL;
    }
    node->obj.handle = controller_handle_alloc(node, CTRL_KIND_NODE);
    if (!node->obj.handle) {
        free(node);
        return NULL;
    }
    return node;
}

ctrl_subgroup_t *controller_model_subgroup_alloc(const char *id, const char *name)
{
    if (!id || id[0] == '\0') {
        return NULL;
    }
    ctrl_subgroup_t *sg = OSAL_CALLOC_EXTRAM(1, sizeof(*sg));
    if (!sg) {
        return NULL;
    }
    snprintf(sg->id, sizeof(sg->id), "%s", id);
    snprintf(sg->name, sizeof(sg->name), "%s", name ? name : "");
    sg->obj.handle = controller_handle_alloc(sg, CTRL_KIND_SUBGROUP);
    if (!sg->obj.handle) {
        free(sg);
        return NULL;
    }
    return sg;
}

/* Topic ctx *******************************************************************/

static int __node_write_thing_name(void *priv, char *buf, size_t buf_size)
{
    const ctrl_node_t *node = (const ctrl_node_t *)priv;
    int written = snprintf(buf, buf_size, "%s", node->node_id);
    return (written > 0 && (size_t)written < buf_size) ? written : -1;
}

static int __node_write_group_info_str(void *priv, char *buf, size_t buf_size)
{
    const ctrl_node_t *node = (const ctrl_node_t *)priv;
    int written = snprintf(buf, buf_size, "%s", node->group_info);
    return (written > 0 && (size_t)written < buf_size) ? written : -1;
}

static const esp_rmaker_topic_ops_t s_node_topic_ops = {
    .write_thing_name = __node_write_thing_name,
    .write_group_info_str = __node_write_group_info_str,
};

void controller_node_topic_ctx(ctrl_node_t *node, esp_rmaker_topic_ctx_t *ctx)
{
    ctx->ops = &s_node_topic_ops;
    ctx->priv = node;
    ctx->valid = NULL;
}

/* Node list *******************************************************************/

ctrl_node_t *controller_model_node_head(void)
{
    return s_nodes;
}

ctrl_subgroup_t *controller_model_subgroup_head(void)
{
    return s_subgroups;
}

ctrl_node_t *controller_model_find_node(const char *node_id)
{
    if (!node_id) {
        return NULL;
    }
    for (ctrl_node_t *node = s_nodes; node; node = node->next) {
        if (strcmp(node->node_id, node_id) == 0) {
            return node;
        }
    }
    return NULL;
}

ctrl_node_t *controller_model_find_node_n(const char *node_id, size_t node_id_len)
{
    if (!node_id) {
        return NULL;
    }
    for (ctrl_node_t *node = s_nodes; node; node = node->next) {
        if (strlen(node->node_id) == node_id_len && strncmp(node->node_id, node_id, node_id_len) == 0) {
            return node;
        }
    }
    return NULL;
}

ctrl_node_t *controller_model_node_on_task(esp_rmaker_handle_t h)
{
    if (controller_model_lock() != ESP_RMAKER_OK) {
        return NULL;
    }
    ctrl_node_t *node = controller_node_from(h);
    controller_model_unlock();
    return node;
}

static ctrl_node_t *__unlink_node(ctrl_node_t **list, const char *node_id)
{
    for (ctrl_node_t **link = list; *link; link = &(*link)->next) {
        if (strcmp((*link)->node_id, node_id) == 0) {
            ctrl_node_t *node = *link;
            *link = node->next;
            node->next = NULL;
            return node;
        }
    }
    return NULL;
}

static ctrl_subgroup_t *__unlink_subgroup(ctrl_subgroup_t **list, const char *id)
{
    for (ctrl_subgroup_t **link = list; *link; link = &(*link)->next) {
        if (strcmp((*link)->id, id) == 0) {
            ctrl_subgroup_t *sg = *link;
            *link = sg->next;
            sg->next = NULL;
            return sg;
        }
    }
    return NULL;
}

/* The group info is all a sync reports, so a survivor takes only those fields from its fresh copy.
 * Returns true when they changed. */
static bool __adopt_group_fields(ctrl_node_t *old, const ctrl_node_t *fresh)
{
    bool changed = strcmp(old->group_info, fresh->group_info) != 0 || strcmp(old->shadow, fresh->shadow) != 0 ||
                   old->num_subgroups != fresh->num_subgroups;
    memcpy(old->group_info, fresh->group_info, sizeof(old->group_info));
    memcpy(old->shadow, fresh->shadow, sizeof(old->shadow));
    memcpy(old->subgroups, fresh->subgroups, sizeof(old->subgroups));
    old->num_subgroups = fresh->num_subgroups;
    return changed;
}

void controller_model_replace_inventory(ctrl_node_t *nodes, ctrl_subgroup_t *subgroups)
{
    ctrl_changes_t changes;
    ctrl_changes_t *collect = controller_changes_begin(&changes);
    ctrl_node_t *node_head = NULL;
    ctrl_node_t **node_tail = &node_head;
    while (nodes) {
        ctrl_node_t *fresh = nodes;
        nodes = fresh->next;
        fresh->next = NULL;
        ctrl_node_t *keep = __unlink_node(&s_nodes, fresh->node_id);
        esp_rmaker_controller_change_type_t type = ESP_RMAKER_CONTROLLER_ADDED;
        bool report = true;
        if (keep) {
            type = ESP_RMAKER_CONTROLLER_UPDATED;
            report = __adopt_group_fields(keep, fresh);
            __node_free(fresh);
        } else {
            keep = fresh;
        }
        if (report) {
            controller_changes_add(collect, type, ESP_RMAKER_CONTROLLER_KIND_NODE, keep->obj.handle, 0);
        }
        *node_tail = keep;
        node_tail = &keep->next;
    }

    ctrl_subgroup_t *sg_head = NULL;
    ctrl_subgroup_t **sg_tail = &sg_head;
    while (subgroups) {
        ctrl_subgroup_t *fresh = subgroups;
        subgroups = fresh->next;
        fresh->next = NULL;
        ctrl_subgroup_t *keep = __unlink_subgroup(&s_subgroups, fresh->id);
        if (!keep) {
            keep = fresh;
            controller_changes_add(collect, ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_SUBGROUP,
                                   keep->obj.handle, 0);
        } else {
            if (strcmp(keep->name, fresh->name) != 0) {
                memcpy(keep->name, fresh->name, sizeof(keep->name));
                controller_changes_add(collect, ESP_RMAKER_CONTROLLER_UPDATED, ESP_RMAKER_CONTROLLER_KIND_SUBGROUP,
                                       keep->obj.handle, 0);
            }
            __subgroup_free(fresh);
        }
        *sg_tail = keep;
        sg_tail = &keep->next;
    }

    /* What stays in the old lists is what the cloud dropped. */
    __collect_removed(collect, s_nodes, s_subgroups);
    controller_model_free_node_list(s_nodes);
    controller_model_free_subgroup_list(s_subgroups);
    s_nodes = node_head;
    s_subgroups = sg_head;
    controller_changes_commit(collect);
}

size_t controller_model_count_nodes(const ctrl_node_t *head)
{
    size_t num = 0;
    for (const ctrl_node_t *node = head; node; node = node->next) {
        num++;
    }
    return num;
}

bool controller_model_list_uses_shadow(const ctrl_node_t *head, const char *shadow)
{
    if (!shadow) {
        return false;
    }
    for (const ctrl_node_t *node = head; node; node = node->next) {
        if (strcmp(node->shadow, shadow) == 0) {
            return true;
        }
    }
    return false;
}

size_t controller_model_collect_shadows(const ctrl_node_t *head,
                                        char (*out)[CONTROLLER_SHADOW_NAME_BUFFER_SIZE], size_t max)
{
    if (!out) {
        return 0;
    }
    size_t num = 0;
    for (const ctrl_node_t *node = head; node && num < max; node = node->next) {
        if (node->shadow[0] == '\0') {
            continue;
        }
        bool seen = false;
        for (size_t i = 0; i < num && !seen; i++) {
            seen = strcmp(out[i], node->shadow) == 0;
        }
        if (!seen) {
            snprintf(out[num++], CONTROLLER_SHADOW_NAME_BUFFER_SIZE, "%s", node->shadow);
        }
    }
    return num;
}

/* Subgroup path ***************************************************************/

esp_rmaker_error_t controller_model_node_add_subgroup(ctrl_node_t *node, const char *subgroup_id)
{
    if (!node || !subgroup_id || subgroup_id[0] == '\0') {
        return ESP_RMAKER_INVALID_ARG;
    }
    for (size_t i = 0; i < node->num_subgroups; i++) {
        if (strcmp(node->subgroups[i], subgroup_id) == 0) {
            return ESP_RMAKER_OK;
        }
    }
    if (node->num_subgroups >= RMAKER_SUBGROUP_MAX_COUNT) {
        OSAL_LOGW(TAG, "Node '%s' is in more than %d subgroups; ignoring '%s'",
                  node->node_id, RMAKER_SUBGROUP_MAX_COUNT, subgroup_id);
        return ESP_RMAKER_NO_MEM;
    }
    if (strlen(subgroup_id) >= RMAKER_SUBGROUP_BUFFER_SIZE) {
        OSAL_LOGW(TAG, "Subgroup id '%s' does not fit", subgroup_id);
        return ESP_RMAKER_INVALID_ARG;
    }
    snprintf(node->subgroups[node->num_subgroups], RMAKER_SUBGROUP_BUFFER_SIZE, "%s", subgroup_id);
    node->num_subgroups++;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t controller_model_node_set_group(ctrl_node_t *node, const char *primary)
{
    if (!node || !primary || primary[0] == '\0') {
        return ESP_RMAKER_INVALID_ARG;
    }
    /* The SDK builder sorts the subgroup ids, which is what the cloud keys the shadow on. */
    char *group_str = NULL;
    esp_rmaker_error_t err = esp_rmaker_local_config_format_group_info_str(
                                 primary, node->num_subgroups ? node->subgroups : NULL,
                                 node->num_subgroups, &group_str);
    if (err != ESP_RMAKER_OK || !group_str) {
        return err == ESP_RMAKER_OK ? ESP_RMAKER_NO_MEM : err;
    }
    int written = snprintf(node->group_info, sizeof(node->group_info), "%s", group_str);
    free(group_str);
    if (written >= (int)sizeof(node->group_info)) {
        OSAL_LOGE(TAG, "Group info for node '%s' does not fit", node->node_id);
        node->group_info[0] = '\0';
        return ESP_RMAKER_NO_MEM;
    }
    if (snprintf(node->shadow, sizeof(node->shadow), "params-%s", node->group_info)
            >= (int)sizeof(node->shadow)) {
        node->shadow[0] = '\0';
        return ESP_RMAKER_NO_MEM;
    }
    return ESP_RMAKER_OK;
}

/* Device and param trees ******************************************************/

ctrl_device_t *controller_model_device_by_id(ctrl_node_t *node, const char *id)
{
    if (!node || !id) {
        return NULL;
    }
    for (ctrl_device_t *dev = node->devices; dev; dev = dev->next) {
        if (dev->id && strcmp(dev->id, id) == 0) {
            return dev;
        }
    }
    return NULL;
}

ctrl_param_t *controller_model_param_by_id(ctrl_device_t *dev, const char *id)
{
    if (!dev || !id) {
        return NULL;
    }
    for (ctrl_param_t *param = dev->params; param; param = param->next) {
        if (param->id && strcmp(param->id, id) == 0) {
            return param;
        }
    }
    return NULL;
}

void controller_model_device_append(ctrl_node_t *node, ctrl_device_t *dev)
{
    /* Append, so the list order follows the config. */
    dev->parent = node;
    dev->next = NULL;
    ctrl_device_t **tail = &node->devices;
    while (*tail) {
        tail = &(*tail)->next;
    }
    *tail = dev;
}

void controller_model_param_append(ctrl_device_t *dev, ctrl_param_t *param)
{
    param->parent = dev;
    param->next = NULL;
    ctrl_param_t **tail = &dev->params;
    while (*tail) {
        tail = &(*tail)->next;
    }
    *tail = param;
}

ctrl_device_t *controller_model_device_take(ctrl_device_t **list, const char *id)
{
    if (!list || !id) {
        return NULL;
    }
    for (ctrl_device_t **link = list; *link; link = &(*link)->next) {
        if ((*link)->id && strcmp((*link)->id, id) == 0) {
            ctrl_device_t *dev = *link;
            *link = dev->next;
            dev->next = NULL;
            return dev;
        }
    }
    return NULL;
}

ctrl_param_t *controller_model_param_take(ctrl_param_t **list, const char *id)
{
    if (!list || !id) {
        return NULL;
    }
    for (ctrl_param_t **link = list; *link; link = &(*link)->next) {
        if ((*link)->id && strcmp((*link)->id, id) == 0) {
            ctrl_param_t *param = *link;
            *link = param->next;
            param->next = NULL;
            return param;
        }
    }
    return NULL;
}

ctrl_device_t *controller_model_device_add(ctrl_node_t *node, const char *id)
{
    if (!node || !id) {
        return NULL;
    }
    ctrl_device_t *dev = OSAL_CALLOC_EXTRAM(1, sizeof(*dev));
    if (!dev) {
        return NULL;
    }
    dev->id = OSAL_STRDUP_EXTRAM(id);
    dev->obj.handle = dev->id ? controller_handle_alloc(dev, CTRL_KIND_DEVICE) : 0;
    if (!dev->obj.handle) {
        free(dev->id);
        free(dev);
        return NULL;
    }
    controller_model_device_append(node, dev);
    return dev;
}

ctrl_param_t *controller_model_param_add(ctrl_device_t *dev, const char *id)
{
    if (!dev || !id) {
        return NULL;
    }
    ctrl_param_t *param = OSAL_CALLOC_EXTRAM(1, sizeof(*param));
    if (!param) {
        return NULL;
    }
    param->id = OSAL_STRDUP_EXTRAM(id);
    param->obj.handle = param->id ? controller_handle_alloc(param, CTRL_KIND_PARAM) : 0;
    if (!param->obj.handle) {
        free(param->id);
        free(param);
        return NULL;
    }
    controller_model_param_append(dev, param);
    return param;
}

static esp_rmaker_error_t __set_string(char **slot, const char *value)
{
    if (*slot && value && strcmp(*slot, value) == 0) {
        return ESP_RMAKER_OK;
    }
    char *dup = NULL;
    if (value) {
        dup = OSAL_STRDUP_EXTRAM(value);
        if (!dup) {
            return ESP_RMAKER_NO_MEM;
        }
    }
    free(*slot);
    *slot = dup;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t controller_model_device_set_type(ctrl_device_t *dev, const char *type)
{
    return dev ? __set_string(&dev->type, type) : ESP_RMAKER_INVALID_ARG;
}

esp_rmaker_error_t controller_model_device_set_primary(ctrl_device_t *dev, const char *id)
{
    return dev ? __set_string(&dev->primary, id) : ESP_RMAKER_INVALID_ARG;
}

esp_rmaker_error_t controller_model_param_set_type(ctrl_param_t *param, const char *type)
{
    return param ? __set_string(&param->type, type) : ESP_RMAKER_INVALID_ARG;
}

esp_rmaker_error_t controller_model_param_set_bounds(ctrl_param_t *param,
        const esp_rmaker_param_bounds_t *bounds)
{
    if (!param) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (!bounds) {
        free(param->bounds);
        param->bounds = NULL;
        return ESP_RMAKER_OK;
    }
    if (!param->bounds) {
        param->bounds = OSAL_CALLOC_EXTRAM(1, sizeof(*param->bounds));
        if (!param->bounds) {
            return ESP_RMAKER_NO_MEM;
        }
    }
    *param->bounds = *bounds;
    return ESP_RMAKER_OK;
}

/* Public handle accessors *****************************************************/

esp_rmaker_error_t controller_model_copy_string(const char *src, char **out)
{
    if (!src || src[0] == '\0') {
        return ESP_RMAKER_NOT_FOUND;
    }
    *out = OSAL_STRDUP_EXTRAM(src);
    return *out ? ESP_RMAKER_OK : ESP_RMAKER_NO_MEM;
}

/* Returns the field of @p obj to copy, or NULL when the source did not report it. */
typedef const char *(*field_fn_t)(const void *obj);

static esp_rmaker_error_t __get_string(esp_rmaker_handle_t h, ctrl_kind_t kind, field_fn_t field, char **out)
{
    if (!out) {
        return ESP_RMAKER_INVALID_ARG;
    }
    *out = NULL;
    esp_rmaker_error_t err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    void *obj = NULL;
    err = controller_handle_lookup(h, kind, &obj);
    if (err == ESP_RMAKER_OK) {
        err = controller_model_copy_string(field(obj), out);
    }
    controller_model_unlock();
    return err;
}

static const char *__node_id_field(const void *obj)
{
    return ((const ctrl_node_t *)obj)->node_id;
}

static const char *__node_data_model_field(const void *obj)
{
    return ((const ctrl_node_t *)obj)->data_model;
}

static const char *__subgroup_id_field(const void *obj)
{
    return ((const ctrl_subgroup_t *)obj)->id;
}

static const char *__subgroup_name_field(const void *obj)
{
    return ((const ctrl_subgroup_t *)obj)->name;
}

static const char *__device_id_field(const void *obj)
{
    return ((const ctrl_device_t *)obj)->id;
}

static const char *__device_type_field(const void *obj)
{
    return ((const ctrl_device_t *)obj)->type;
}

static const char *__param_id_field(const void *obj)
{
    return ((const ctrl_param_t *)obj)->id;
}

static const char *__param_type_field(const void *obj)
{
    return ((const ctrl_param_t *)obj)->type;
}

esp_rmaker_controller_node_t esp_rmaker_controller_node_first(void)
{
    if (controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    esp_rmaker_controller_node_t handle = CONTROLLER_HANDLE(s_nodes);
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_node_t esp_rmaker_controller_node_next(esp_rmaker_controller_node_t node)
{
    if (!node || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    ctrl_node_t *n = controller_node_from(node);
    esp_rmaker_controller_node_t handle = n ? CONTROLLER_HANDLE(n->next) : 0;
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_node_t esp_rmaker_controller_node_find(const char *node_id)
{
    if (controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    esp_rmaker_controller_node_t handle = CONTROLLER_HANDLE(controller_model_find_node(node_id));
    controller_model_unlock();
    return handle;
}

esp_rmaker_error_t esp_rmaker_controller_node_get_id(esp_rmaker_controller_node_t node, char **out)
{
    return __get_string(node, CTRL_KIND_NODE, __node_id_field, out);
}

bool esp_rmaker_controller_node_in_subgroup(esp_rmaker_controller_node_t node, const char *subgroup_id)
{
    if (!node || !subgroup_id || controller_model_lock() != ESP_RMAKER_OK) {
        return false;
    }
    bool found = false;
    ctrl_node_t *n = controller_node_from(node);
    for (size_t i = 0; n && i < n->num_subgroups && !found; i++) {
        found = strcmp(n->subgroups[i], subgroup_id) == 0;
    }
    controller_model_unlock();
    return found;
}

esp_rmaker_controller_subgroup_t esp_rmaker_controller_subgroup_first(void)
{
    if (controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    esp_rmaker_controller_subgroup_t handle = CONTROLLER_HANDLE(s_subgroups);
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_subgroup_t esp_rmaker_controller_subgroup_next(esp_rmaker_controller_subgroup_t sg)
{
    if (!sg || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    ctrl_subgroup_t *s = controller_subgroup_from(sg);
    esp_rmaker_controller_subgroup_t handle = s ? CONTROLLER_HANDLE(s->next) : 0;
    controller_model_unlock();
    return handle;
}

esp_rmaker_error_t esp_rmaker_controller_subgroup_get_id(esp_rmaker_controller_subgroup_t sg, char **out)
{
    return __get_string(sg, CTRL_KIND_SUBGROUP, __subgroup_id_field, out);
}

esp_rmaker_error_t esp_rmaker_controller_subgroup_get_name(esp_rmaker_controller_subgroup_t sg, char **out)
{
    return __get_string(sg, CTRL_KIND_SUBGROUP, __subgroup_name_field, out);
}

esp_rmaker_error_t esp_rmaker_controller_node_get_data_model(esp_rmaker_controller_node_t node, char **out)
{
    return __get_string(node, CTRL_KIND_NODE, __node_data_model_field, out);
}

bool esp_rmaker_controller_node_is_supported_data_model(esp_rmaker_controller_node_t node)
{
    if (!node || controller_model_lock() != ESP_RMAKER_OK) {
        return false;
    }
    ctrl_node_t *n = controller_node_from(node);
    bool supported = n && n->data_model_supported;
    controller_model_unlock();
    return supported;
}

bool esp_rmaker_controller_node_config_is_current(esp_rmaker_controller_node_t node)
{
    if (!node || controller_model_lock() != ESP_RMAKER_OK) {
        return false;
    }
    ctrl_node_t *n = controller_node_from(node);
    bool current = n && n->config_held && !n->config_dirty;
    controller_model_unlock();
    return current;
}

bool esp_rmaker_controller_node_is_online(esp_rmaker_controller_node_t node)
{
    if (!node || controller_model_lock() != ESP_RMAKER_OK) {
        return false;
    }
    ctrl_node_t *n = controller_node_from(node);
    bool online = n && n->online;
    controller_model_unlock();
    return online;
}

esp_rmaker_controller_device_t esp_rmaker_controller_node_device_first(esp_rmaker_controller_node_t node)
{
    if (!node || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    ctrl_node_t *n = controller_node_from(node);
    esp_rmaker_controller_device_t handle = n ? CONTROLLER_HANDLE(n->devices) : 0;
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_device_t esp_rmaker_controller_device_next(esp_rmaker_controller_device_t dev)
{
    if (!dev || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    ctrl_device_t *d = controller_device_from(dev);
    esp_rmaker_controller_device_t handle = d ? CONTROLLER_HANDLE(d->next) : 0;
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_device_t esp_rmaker_controller_node_device_by_id(esp_rmaker_controller_node_t node,
        const char *id)
{
    if (!node || !id || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    ctrl_device_t *dev = controller_model_device_by_id(controller_node_from(node), id);
    esp_rmaker_controller_device_t handle = CONTROLLER_HANDLE(dev);
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_device_t esp_rmaker_controller_node_device_by_type(esp_rmaker_controller_node_t node,
        const char *type)
{
    if (!node || !type || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    esp_rmaker_controller_device_t handle = 0;
    ctrl_node_t *n = controller_node_from(node);
    for (ctrl_device_t *dev = n ? n->devices : NULL; dev && !handle; dev = dev->next) {
        if (dev->type && strcmp(dev->type, type) == 0) {
            handle = CONTROLLER_HANDLE(dev);
        }
    }
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_device_t esp_rmaker_controller_node_service_by_type(esp_rmaker_controller_node_t node,
        const char *type)
{
    return esp_rmaker_controller_node_device_by_type(node, type);
}

esp_rmaker_error_t esp_rmaker_controller_device_get_id(esp_rmaker_controller_device_t dev, char **out)
{
    return __get_string(dev, CTRL_KIND_DEVICE, __device_id_field, out);
}

esp_rmaker_error_t esp_rmaker_controller_device_get_type(esp_rmaker_controller_device_t dev, char **out)
{
    return __get_string(dev, CTRL_KIND_DEVICE, __device_type_field, out);
}

bool esp_rmaker_controller_device_is_service(esp_rmaker_controller_device_t dev)
{
    if (!dev || controller_model_lock() != ESP_RMAKER_OK) {
        return false;
    }
    ctrl_device_t *d = controller_device_from(dev);
    bool is_service = d && d->is_service;
    controller_model_unlock();
    return is_service;
}

esp_rmaker_controller_node_t esp_rmaker_controller_device_get_node(esp_rmaker_controller_device_t dev)
{
    if (!dev || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    ctrl_device_t *d = controller_device_from(dev);
    esp_rmaker_controller_node_t handle = d ? CONTROLLER_HANDLE(d->parent) : 0;
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_param_t esp_rmaker_controller_device_get_primary_param(esp_rmaker_controller_device_t dev)
{
    if (!dev || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    ctrl_device_t *d = controller_device_from(dev);
    /* Resolved by id every call, so a primary the latest config dropped reports as gone. */
    ctrl_param_t *param = (d && d->primary) ? controller_model_param_by_id(d, d->primary) : NULL;
    esp_rmaker_controller_param_t handle = CONTROLLER_HANDLE(param);
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_param_t esp_rmaker_controller_device_param_first(esp_rmaker_controller_device_t dev)
{
    if (!dev || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    ctrl_device_t *d = controller_device_from(dev);
    esp_rmaker_controller_param_t handle = d ? CONTROLLER_HANDLE(d->params) : 0;
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_param_t esp_rmaker_controller_param_next(esp_rmaker_controller_param_t param)
{
    if (!param || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    ctrl_param_t *p = controller_param_from(param);
    esp_rmaker_controller_param_t handle = p ? CONTROLLER_HANDLE(p->next) : 0;
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_param_t esp_rmaker_controller_device_param_by_id(esp_rmaker_controller_device_t dev,
        const char *id)
{
    if (!dev || !id || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    ctrl_param_t *param = controller_model_param_by_id(controller_device_from(dev), id);
    esp_rmaker_controller_param_t handle = CONTROLLER_HANDLE(param);
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_param_t esp_rmaker_controller_device_param_by_type(esp_rmaker_controller_device_t dev,
        const char *type)
{
    if (!dev || !type || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    esp_rmaker_controller_param_t handle = 0;
    ctrl_device_t *d = controller_device_from(dev);
    for (ctrl_param_t *param = d ? d->params : NULL; param && !handle; param = param->next) {
        if (param->type && strcmp(param->type, type) == 0) {
            handle = CONTROLLER_HANDLE(param);
        }
    }
    controller_model_unlock();
    return handle;
}

esp_rmaker_controller_param_t esp_rmaker_controller_node_find_param(esp_rmaker_controller_node_t node,
        const char *device_type, const char *param_type)
{
    esp_rmaker_controller_device_t dev = esp_rmaker_controller_node_device_by_type(node, device_type);
    return dev ? esp_rmaker_controller_device_param_by_type(dev, param_type) : 0;
}

esp_rmaker_error_t esp_rmaker_controller_param_get_id(esp_rmaker_controller_param_t param, char **out)
{
    return __get_string(param, CTRL_KIND_PARAM, __param_id_field, out);
}

esp_rmaker_error_t esp_rmaker_controller_param_get_type(esp_rmaker_controller_param_t param, char **out)
{
    return __get_string(param, CTRL_KIND_PARAM, __param_type_field, out);
}

esp_rmaker_val_type_t esp_rmaker_controller_param_get_data_type(esp_rmaker_controller_param_t param)
{
    if (!param || controller_model_lock() != ESP_RMAKER_OK) {
        return RMAKER_VAL_TYPE_INVALID;
    }
    ctrl_param_t *p = controller_param_from(param);
    esp_rmaker_val_type_t type = p ? p->data_type : RMAKER_VAL_TYPE_INVALID;
    controller_model_unlock();
    return type;
}

esp_rmaker_ui_type_t esp_rmaker_controller_param_get_ui_type(esp_rmaker_controller_param_t param)
{
    if (!param || controller_model_lock() != ESP_RMAKER_OK) {
        return RMAKER_UI_TYPE_NONE;
    }
    ctrl_param_t *p = controller_param_from(param);
    esp_rmaker_ui_type_t type = p ? p->ui_type : RMAKER_UI_TYPE_NONE;
    controller_model_unlock();
    return type;
}

esp_rmaker_controller_device_t esp_rmaker_controller_param_get_device(esp_rmaker_controller_param_t param)
{
    if (!param || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    ctrl_param_t *p = controller_param_from(param);
    esp_rmaker_controller_device_t handle = p ? CONTROLLER_HANDLE(p->parent) : 0;
    controller_model_unlock();
    return handle;
}

esp_rmaker_param_property_flags_t esp_rmaker_controller_param_get_props(esp_rmaker_controller_param_t param)
{
    if (!param || controller_model_lock() != ESP_RMAKER_OK) {
        return 0;
    }
    ctrl_param_t *p = controller_param_from(param);
    esp_rmaker_param_property_flags_t props = p ? (esp_rmaker_param_property_flags_t)p->prop_flags : 0;
    controller_model_unlock();
    return props;
}

esp_rmaker_error_t esp_rmaker_controller_param_get_bounds(esp_rmaker_controller_param_t param,
        esp_rmaker_param_bounds_t *out)
{
    if (!out) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    void *obj = NULL;
    err = controller_handle_lookup(param, CTRL_KIND_PARAM, &obj);
    const ctrl_param_t *p = (const ctrl_param_t *)obj;
    if (err == ESP_RMAKER_OK && !p->bounds) {
        err = ESP_RMAKER_NOT_FOUND;
    } else if (err == ESP_RMAKER_OK) {
        *out = *p->bounds;
    }
    controller_model_unlock();
    return err;
}

esp_rmaker_error_t esp_rmaker_controller_param_get_val(esp_rmaker_controller_param_t param,
        esp_rmaker_param_val_t *out_val)
{
    if (!out_val) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t err = controller_model_lock();
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    void *obj = NULL;
    err = controller_handle_lookup(param, CTRL_KIND_PARAM, &obj);
    const ctrl_param_t *p = (const ctrl_param_t *)obj;
    if (err == ESP_RMAKER_OK && !p->val_valid) {
        err = ESP_RMAKER_NOT_FOUND;
    } else if (err == ESP_RMAKER_OK) {
        err = esp_rmaker_val_copy(&p->val, out_val);
    }
    controller_model_unlock();
    return err;
}
