/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_model.h
 * @brief The cached group graph: nodes, subgroups, devices and params.
 *
 * Singly linked like the SDK's own data model. Each object holds a validated handle, so a handle to
 * a freed object resolves to nothing.
 */

#ifndef __CONTROLLER_MODEL_H__
#define __CONTROLLER_MODEL_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_rmaker_controller.h"
#include "esp_rmaker_data_model.h"
#include "esp_rmaker_standard_types.h"
#include "esp_rmaker_val.h"

#include "constants/identity.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Buffer sizes, with the NUL, of the fields the parsers record. A longer value is skipped. */
#define CONTROLLER_DATA_MODEL_MAX_LEN    16
#define CONTROLLER_SUBGROUP_NAME_MAX_LEN 64
#define CONTROLLER_GROUP_NAME_MAX_LEN    64
/* The node firmware limits a device or param id to RMAKER_NVS_KEY_LEN_MAX (15) characters. */
#define CONTROLLER_CFG_ID_MAX_LEN        16
#define CONTROLLER_CFG_TYPE_MAX_LEN      64

/** Longest ``ncfg_ver`` change token the parser records. A SHA-256 hex string is 64 chars. */
#define CONTROLLER_NCFG_VER_MAX_LEN 72

/** The only data model this SDK parses. */
#define CONTROLLER_DATA_MODEL_DEFAULT "default"

/** Buffer size for a node's shadow name, ``"params-<group_info>"``. */
#define CONTROLLER_SHADOW_NAME_BUFFER_SIZE (RMAKER_GROUP_INFO_STR_BUFFER_SIZE + 8)

/** @brief The kind of object a handle names. 0 marks a free slot. */
typedef enum {
    CTRL_KIND_NODE = 1,
    CTRL_KIND_SUBGROUP,
    CTRL_KIND_DEVICE,
    CTRL_KIND_PARAM,
} ctrl_kind_t;

/** @brief The header every model object starts with. */
typedef struct {
    esp_rmaker_handle_t handle;
} ctrl_obj_t;

/** @brief One param of a device or service. */
typedef struct ctrl_param {
    ctrl_obj_t obj;
    char *id;
    char *type;
    esp_rmaker_val_type_t data_type;
    esp_rmaker_ui_type_t ui_type;
    uint8_t prop_flags;                 /**< esp_rmaker_param_property_flags_t */
    esp_rmaker_param_bounds_t *bounds;
    esp_rmaker_param_val_t val;
    bool val_valid;
    struct ctrl_device *parent;
    struct ctrl_param *next;
} ctrl_param_t;

/** @brief One device or service of a node. */
typedef struct ctrl_device {
    ctrl_obj_t obj;
    char *id;
    char *type;
    char *primary;                      /**< Id of the primary param, as the config reports it. */
    bool is_service;
    ctrl_param_t *params;
    struct ctrl_node *parent;
    struct ctrl_device *next;
} ctrl_device_t;

/** @brief One param write staged into a node's open batch. */
typedef struct ctrl_staged_write {
    esp_rmaker_handle_t param;
    esp_rmaker_param_val_t val;
    struct ctrl_staged_write *next;
} ctrl_staged_write_t;

/**
 * @brief One param write staged into the open group batch.
 *
 * A group write is keyed by bare type strings rather than by a param of the model, so this holds
 * its own copies and names no ctrl_param_t.
 */
typedef struct ctrl_staged_group_write {
    char *device_type;
    char *param_type;
    esp_rmaker_param_val_t val;
    struct ctrl_staged_group_write *next;
} ctrl_staged_group_write_t;

/** @brief One node in the controller's group. */
typedef struct ctrl_node {
    ctrl_obj_t obj;
    char node_id[RMAKER_THING_NAME_BUFFER_SIZE];
    char group_info[RMAKER_GROUP_INFO_STR_BUFFER_SIZE];  /**< "<primary>[-<sg>...]", sorted. */
    char shadow[CONTROLLER_SHADOW_NAME_BUFFER_SIZE];      /**< "params-<group_info>". */
    char subgroups[RMAKER_SUBGROUP_MAX_COUNT][RMAKER_SUBGROUP_BUFFER_SIZE];
    size_t num_subgroups;
    char data_model[CONTROLLER_DATA_MODEL_MAX_LEN];
    bool data_model_supported;
    char ncfg_ver[CONTROLLER_NCFG_VER_MAX_LEN];
    bool online;
    bool config_held;
    bool config_dirty;                  /**< ncfg_ver moved; a re-fetch is owed. */
    ctrl_device_t *devices;             /**< Devices and services in one list. */
    bool batch_open;
    ctrl_staged_write_t *batch;
    struct ctrl_node *next;
} ctrl_node_t;

/** @brief One subgroup of the controller's group. */
typedef struct ctrl_subgroup {
    ctrl_obj_t obj;
    char id[RMAKER_SUBGROUP_BUFFER_SIZE];
    char name[CONTROLLER_SUBGROUP_NAME_MAX_LEN];
    struct ctrl_subgroup *next;
} ctrl_subgroup_t;

/* Model lock *****************************************************************/

/**
 * @brief Take the model lock.
 *
 * Recursive, so a callback delivered under it may call back into the accessors.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_NOT_INITIALIZED if the model is not open.
 */
esp_rmaker_error_t controller_model_lock(void);

/** @brief Release the model lock. */
void controller_model_unlock(void);

/* Lifecycle ******************************************************************/

/** @brief Create the model lock on first use, and open the model. Idempotent. */
esp_rmaker_error_t controller_model_init(void);

/**
 * @brief Free every node, subgroup and parsed tree, then close the model. The lock is kept.
 *
 * Delivers the queued change lists and a REMOVED entry for each node and subgroup on the calling
 * task, so call it only after the controller task stopped.
 */
void controller_model_deinit(void);

/* Node list ******************************************************************/

/** @brief Head of the node list. Call under the model lock. */
ctrl_node_t *controller_model_node_head(void);

/** @brief Head of the subgroup list. Call under the model lock. */
ctrl_subgroup_t *controller_model_subgroup_head(void);

/** @brief Find a node by id, or NULL. Call under the model lock. */
ctrl_node_t *controller_model_find_node(const char *node_id);

/** @brief Find a node by id and length, for a thing name lifted out of a topic. */
ctrl_node_t *controller_model_find_node_n(const char *node_id, size_t node_id_len);

/**
 * @brief Replace the node and subgroup lists with a freshly synced inventory.
 *
 * A node or subgroup in both lists keeps its old object and handle, and takes the fresh fields. The
 * fresh duplicate is freed. A node or subgroup the cloud no longer reports is freed. Queues a change
 * list of what it added, updated and freed.
 *
 * @param[in] nodes     Head of the new node list. Ownership passes to the model.
 * @param[in] subgroups Head of the new subgroup list. Ownership passes to the model.
 */
void controller_model_replace_inventory(ctrl_node_t *nodes, ctrl_subgroup_t *subgroups);

/**
 * @brief Collect the distinct shadow names @p head observes into @p out.
 *
 * A node with no derived shadow name is skipped, and a name two nodes share is written once.
 *
 * @param[in]  head Head of the node list to walk.
 * @param[out] out  Receives the names.
 * @param[in]  max  Entries @p out holds.
 *
 * @return How many names were written, at most @p max.
 */
size_t controller_model_collect_shadows(const ctrl_node_t *head,
                                        char (*out)[CONTROLLER_SHADOW_NAME_BUFFER_SIZE], size_t max);

/** @brief Report whether any node in @p head observes @p shadow. */
bool controller_model_list_uses_shadow(const ctrl_node_t *head, const char *shadow);

/** @brief Count the nodes in @p head. */
size_t controller_model_count_nodes(const ctrl_node_t *head);

/** @brief Allocate a bare node with this id, or NULL. */
ctrl_node_t *controller_model_node_alloc(const char *node_id);

/** @brief Allocate a subgroup, or NULL. */
ctrl_subgroup_t *controller_model_subgroup_alloc(const char *id, const char *name);

/** @brief Free a node list built during a sync that then failed. */
void controller_model_free_node_list(ctrl_node_t *head);

/** @brief Free a subgroup list built during a sync that then failed. */
void controller_model_free_subgroup_list(ctrl_subgroup_t *head);

/**
 * @brief Record @p node's subgroup path and derive its shadow name from it.
 *
 * Sorts the subgroup ids, because the cloud keys the shadow on the sorted path.
 *
 * @return ESP_RMAKER_OK on success.
 */
esp_rmaker_error_t controller_model_node_set_group(ctrl_node_t *node, const char *primary);

/** @brief Add one subgroup id to @p node's path, ignoring a duplicate. */
esp_rmaker_error_t controller_model_node_add_subgroup(ctrl_node_t *node, const char *subgroup_id);

/* Device and param trees *****************************************************/

/** @brief Find a device of @p node by id, or NULL. */
ctrl_device_t *controller_model_device_by_id(ctrl_node_t *node, const char *id);

/** @brief Find a param of @p dev by id, or NULL. */
ctrl_param_t *controller_model_param_by_id(ctrl_device_t *dev, const char *id);

/** @brief Allocate a device and append it to @p node, or return NULL. */
ctrl_device_t *controller_model_device_add(ctrl_node_t *node, const char *id);

/** @brief Allocate a param and append it to @p dev, or return NULL. */
ctrl_param_t *controller_model_param_add(ctrl_device_t *dev, const char *id);

/** @brief Unlink the device with this id from the detached list @p list, or return NULL. */
ctrl_device_t *controller_model_device_take(ctrl_device_t **list, const char *id);

/** @brief Unlink the param with this id from the detached list @p list, or return NULL. */
ctrl_param_t *controller_model_param_take(ctrl_param_t **list, const char *id);

/** @brief Append an unlinked device to @p node. */
void controller_model_device_append(ctrl_node_t *node, ctrl_device_t *dev);

/** @brief Append an unlinked param to @p dev. */
void controller_model_param_append(ctrl_device_t *dev, ctrl_param_t *param);

/** @brief Free a detached device list, with the params of each device. */
void controller_model_free_device_list(ctrl_device_t *head);

/** @brief Free a detached param list. */
void controller_model_free_param_list(ctrl_param_t *head);

/** @brief Replace @p dev's type string. */
esp_rmaker_error_t controller_model_device_set_type(ctrl_device_t *dev, const char *type);

/** @brief Replace @p dev's primary param id, or drop it when @p id is NULL. */
esp_rmaker_error_t controller_model_device_set_primary(ctrl_device_t *dev, const char *id);

/** @brief Replace @p param's type string. */
esp_rmaker_error_t controller_model_param_set_type(ctrl_param_t *param, const char *type);

/** @brief Replace @p param's bounds, or drop them when @p bounds is NULL. */
esp_rmaker_error_t controller_model_param_set_bounds(ctrl_param_t *param, const esp_rmaker_param_bounds_t *bounds);

/** @brief Free @p node's whole device and param tree. */
void controller_model_release_config(ctrl_node_t *node);

/** @brief Discard @p node's staged batch. */
void controller_model_batch_clear(ctrl_node_t *node);

/**
 * @brief Allocate a copy of @p src into @p out, which the caller frees with free().
 *
 * @return ESP_RMAKER_OK, ESP_RMAKER_NOT_FOUND for NULL or "", or ESP_RMAKER_NO_MEM.
 */
esp_rmaker_error_t controller_model_copy_string(const char *src, char **out);

/* Handles ********************************************************************/

/** The handle of a model object, or 0 for NULL. */
#define CONTROLLER_HANDLE(p) ((p) ? (p)->obj.handle : (esp_rmaker_handle_t)0)

/**
 * @brief Take a slot for @p obj and return its handle, or 0 when the table cannot grow.
 *
 * Takes the model lock for the slot only.
 */
esp_rmaker_handle_t controller_handle_alloc(void *obj, ctrl_kind_t kind);

/** @brief Free the slot of @p handle, so the handle goes stale. Takes the model lock. */
void controller_handle_free(esp_rmaker_handle_t handle);

/**
 * @brief Look up the object of @p handle. Call under the model lock.
 *
 * @param[out] out Receives the object, or NULL.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG for 0, or a live handle of another kind.
 * @return ESP_RMAKER_NOT_FOUND for a stale handle.
 */
esp_rmaker_error_t controller_handle_lookup(esp_rmaker_handle_t handle, ctrl_kind_t kind, void **out);

/** @brief The object of @p handle, or NULL when the handle is 0, stale or of another kind. */
void *controller_handle_resolve(esp_rmaker_handle_t handle, ctrl_kind_t kind);

static inline ctrl_node_t *controller_node_from(esp_rmaker_handle_t h)
{
    return (ctrl_node_t *)controller_handle_resolve(h, CTRL_KIND_NODE);
}

static inline ctrl_subgroup_t *controller_subgroup_from(esp_rmaker_handle_t h)
{
    return (ctrl_subgroup_t *)controller_handle_resolve(h, CTRL_KIND_SUBGROUP);
}

static inline ctrl_device_t *controller_device_from(esp_rmaker_handle_t h)
{
    return (ctrl_device_t *)controller_handle_resolve(h, CTRL_KIND_DEVICE);
}

static inline ctrl_param_t *controller_param_from(esp_rmaker_handle_t h)
{
    return (ctrl_param_t *)controller_handle_resolve(h, CTRL_KIND_PARAM);
}

/**
 * @brief Resolve a node handle on the controller task.
 *
 * Only the controller task frees nodes, so the node stays valid until the calling job returns.
 * Device and param pointers under it are valid only under the model lock.
 */
ctrl_node_t *controller_model_node_on_task(esp_rmaker_handle_t h);

#ifdef __cplusplus
}
#endif

#endif /* __CONTROLLER_MODEL_H__ */
