/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_controller.h
 * @brief Group controller support for RainMaker Neo nodes.
 *
 * A controller is a normal RainMaker Neo node that the cloud registers with the "controller"
 * capability. Scoped to the group it belongs to, it holds that group's node graph, data model and
 * param values, and drives those params over MQTT. It is a phone app running on firmware.
 *
 * The application drives opaque handles. The SDK owns the group graph, the data model and every
 * wire format: it derives each node's shadow name from the group info the cloud reports, keys node
 * writes by device id and group writes by device type, and serializes values exactly as the node
 * side does.
 *
 * The device X.509 certificate is the only identity. Every REST call is signed with SigV4
 * credentials that the AWS IoT Credential Provider issues for that certificate, under the role alias
 * CONFIG_ESP_RMAKER_NEO_CONTROLLER_ROLE_ALIAS. The cloud identifies the controller from those
 * credentials alone; there is no session token.
 *
 * Handles: a handle is a 32-bit reference to one node, subgroup, device or param. It is valid while
 * its object is in the latest snapshot: the group sync for nodes and subgroups, and the config fetch
 * of a node for its devices and params. An object that stays in the next snapshot keeps its handle.
 * An object that leaves it is freed, and its handle goes stale; if it comes back, it gets a new
 * handle. A stale handle never reads freed memory: each call returns ESP_RMAKER_NOT_FOUND, 0, false
 * or RMAKER_VAL_TYPE_INVALID, and a write publishes nothing. A handle does not survive a reboot or
 * esp_rmaker_controller_disable(), so store ids for references that must outlive one.
 *
 * Changes: esp_rmaker_controller_watch_changes() first reports ADDED for each object in the current
 * snapshot, then each structural change as a list. Keep a map from handle to the application's own
 * view entry: add an entry for ADDED, refresh it for UPDATED, and remove it and all its descendants
 * for REMOVED. Use a REMOVED handle only as a key into that map. Value changes and online changes
 * come from esp_rmaker_controller_watch_params() and esp_rmaker_controller_watch_nodes().
 *
 * Typical use:
 * @code
 * esp_rmaker_controller_enable();
 * esp_rmaker_controller_group_sync();
 * for (esp_rmaker_controller_node_t n = esp_rmaker_controller_node_first(); n;
 *      n = esp_rmaker_controller_node_next(n)) {
 *     if (esp_rmaker_controller_node_sync(n) != ESP_RMAKER_OK) {
 *         continue;
 *     }
 *     esp_rmaker_controller_param_t p = esp_rmaker_controller_node_find_param(
 *             n, ESP_RMAKER_DEVICE_LIGHTBULB, ESP_RMAKER_PARAM_POWER);
 *     if (p) {
 *         esp_rmaker_controller_param_write(p, esp_rmaker_bool(true));
 *     }
 * }
 * @endcode
 *
 * The controller's group is the node's own group, which the cloud sends in its group info over MQTT
 * like for any node. Read it with esp_rmaker_get_stored_group_id(); RMAKER_EVENT_GROUP_INFO_UPDATED
 * reports a change. Until the node is in a group, every operation below returns ESP_RMAKER_NOT_FOUND.
 *
 * The REST API base URL and the credential-provider endpoint come from the cloud's server config
 * (getServerConfig), so a controller needs no provisioning beyond that of any node.
 *
 * The cloud grants a controller only GET on /v1/groups/{group}/... routes. After a move to another
 * group, REST calls follow at once, and MQTT follows after the reconnect that the SDK makes when the
 * primary group changes.
 *
 * All entry points in this header require ``CONFIG_ESP_RMAKER_NEO_CONTROLLER=y``.
 */

#ifndef __ESP_RMAKER_CONTROLLER_H__
#define __ESP_RMAKER_CONTROLLER_H__

#include "sdkconfig.h"

#ifdef CONFIG_ESP_RMAKER_NEO_CONTROLLER

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_rmaker_data_model.h"
#include "esp_rmaker_error_types.h"
#include "esp_rmaker_handle.h"
#include "esp_rmaker_standard_types.h"
#include "esp_rmaker_val.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Handles ********************************************************************/

/** @brief Handle to one node in the controller's group. */
typedef esp_rmaker_handle_t esp_rmaker_controller_node_t;

/** @brief Handle to one device or service on a node. */
typedef esp_rmaker_handle_t esp_rmaker_controller_device_t;

/** @brief Handle to one param of a device or service. */
typedef esp_rmaker_handle_t esp_rmaker_controller_param_t;

/** @brief Handle to one subgroup of the controller's group. */
typedef esp_rmaker_handle_t esp_rmaker_controller_subgroup_t;

/**
 * @brief Callback delivering a response body.
 *
 * Invoked once, on the controller task. The buffer is owned by the SDK and valid only for the
 * duration of the call.
 *
 * @param[in] user_ctx Opaque user pointer passed at call time.
 * @param[in] data     Response body. Not NUL-terminated.
 * @param[in] len      Length of @p data in bytes.
 */
typedef esp_rmaker_error_t (*esp_rmaker_controller_data_cb_t)(void *user_ctx, const char *data, size_t len);

/**
 * @brief Callback reporting one param whose value changed.
 *
 * Delivered from the controller task, so it may call any controller API except
 * esp_rmaker_controller_disable().
 *
 * @param[in] param Param whose value changed.
 * @param[in] priv  Opaque pointer registered with esp_rmaker_controller_watch_params().
 */
typedef void (*esp_rmaker_controller_param_cb_t)(esp_rmaker_controller_param_t param, void *priv);

/**
 * @brief Callback reporting a node whose online flag changed.
 *
 * Delivered from the controller task, so it may call any controller API except
 * esp_rmaker_controller_disable().
 *
 * @param[in] node Node whose online flag changed.
 * @param[in] priv Opaque pointer registered with esp_rmaker_controller_watch_nodes().
 */
typedef void (*esp_rmaker_controller_node_cb_t)(esp_rmaker_controller_node_t node, void *priv);

/** @brief The type of one structural change. */
typedef enum {
    ESP_RMAKER_CONTROLLER_ADDED,        /**< The object is new in the snapshot. */
    ESP_RMAKER_CONTROLLER_UPDATED,      /**< A config field of the object changed. Never a value. */
    ESP_RMAKER_CONTROLLER_REMOVED,      /**< The object and all its descendants are freed. */
} esp_rmaker_controller_change_type_t;

/** @brief The kind of object a change names. */
typedef enum {
    ESP_RMAKER_CONTROLLER_KIND_NODE,
    ESP_RMAKER_CONTROLLER_KIND_SUBGROUP,
    ESP_RMAKER_CONTROLLER_KIND_DEVICE,
    ESP_RMAKER_CONTROLLER_KIND_PARAM,
} esp_rmaker_controller_kind_t;

/** @brief One structural change. */
typedef struct {
    esp_rmaker_controller_change_type_t type;
    esp_rmaker_controller_kind_t kind;
    esp_rmaker_handle_t handle;
    esp_rmaker_handle_t parent;         /**< The node of a device, the device of a param, else 0. */
} esp_rmaker_controller_change_t;

/**
 * @brief Callback reporting one list of structural changes.
 *
 * Delivered from the controller task. A list holds REMOVED entries first, then UPDATED, then ADDED
 * with parents before children. A REMOVED entry has no entries for the descendants of its object.
 *
 * @param[in] changes The list. Valid only during the call.
 * @param[in] count   Entries in @p changes.
 * @param[in] priv    Opaque pointer registered with esp_rmaker_controller_watch_changes().
 */
typedef void (*esp_rmaker_controller_change_cb_t)(const esp_rmaker_controller_change_t *changes, size_t count,
        void *priv);

/* Lifecycle ******************************************************************/

/**
 * @brief Enable controller support.
 *
 * Starts the controller task that every blocking call runs its work on. Call after
 * esp_rmaker_start(), which is what makes the node id available.
 *
 * @note This does not contact the cloud. Call esp_rmaker_controller_group_sync() for that. The first
 *       REST call waits for the server config and fetches the first credentials.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_ALREADY_INITIALIZED if already enabled.
 * @return ESP_RMAKER_NOT_INITIALIZED if the node id is not available yet.
 */
esp_rmaker_error_t esp_rmaker_controller_enable(void);

/**
 * @brief Disable controller support and release what enable and the operations acquired.
 *
 * Queued jobs run before the controller task parks. A blocking call that races the stop returns
 * ESP_RMAKER_INVALID_STATE, and one made after it returns ESP_RMAKER_NOT_INITIALIZED. Every shadow
 * filter is unsubscribed, an open group batch is dropped, and every handle is invalidated. The
 * change callback stays registered, and gets a REMOVED entry for each node and subgroup on this
 * task.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_STATE if called from a controller callback. Nothing is changed.
 */
esp_rmaker_error_t esp_rmaker_controller_disable(void);

/**
 * @brief Put the controller on standby, or make it active again.
 *
 * A device-local switch: the cloud knows nothing about it. A standby controller still reads (node
 * list, config, params) but refuses to set params. The controller is active after enable.
 *
 * @param[in] active false for standby, true for active.
 */
void esp_rmaker_controller_set_active(bool active);

/**
 * @brief Get a copy of the controller's group name.
 *
 * @param[out] out Receives the copy. The caller frees it with free(). Set to NULL on any error.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if @p out is NULL.
 * @return ESP_RMAKER_NOT_FOUND before the first inventory sync, or if the cloud reported no name.
 * @return ESP_RMAKER_NO_MEM if the copy cannot be allocated.
 */
esp_rmaker_error_t esp_rmaker_controller_get_group_name(char **out);

/**
 * @brief Report whether the controller is active, as set by esp_rmaker_controller_set_active().
 */
bool esp_rmaker_controller_is_active(void);

/**
 * @brief Issue a signed GET against the REST data APIs.
 *
 * The calls below cover everything the firmware models. Use this as an escape hatch for any other
 * route the cloud grants a controller, all of them under /v1/groups/{group}/, e.g.
 * "/v1/groups/<g>/nodes/<n>/schedules". The cloud refuses every other method and route.
 *
 * @param[in] path_suffix Path after the API base path, starting with '/'.
 * @param[in] cb          Invoked once with the response body, on the controller task. May be NULL.
 * @param[in] user_ctx    Opaque pointer passed to @p cb.
 *
 * @return ESP_RMAKER_OK when the API answered 2xx.
 * @return ESP_RMAKER_NOT_FOUND on 404: no such resource, or one outside the controller's group.
 * @return ESP_RMAKER_INVALID_STATE on 401 or 403: the cloud does not accept this node as a
 *         controller of that group, e.g. right after a move, or for a route it does not grant.
 * @return ESP_RMAKER_TIMEOUT if the server config has not arrived.
 * @return ESP_RMAKER_FAIL on any other failure.
 */
esp_rmaker_error_t esp_rmaker_controller_api_get(const char *path_suffix, esp_rmaker_controller_data_cb_t cb,
        void *user_ctx);

/* Inventory ******************************************************************/

/**
 * @brief Fetch the group's node list and subgroup membership, and observe its shadows.
 *
 * Calls ``GET /v1/groups/{group_id}`` and rebuilds the node and subgroup lists from the reported
 * ``node_ids`` and ``subgroups``. Each node's shadow name is derived from its subgroup path, so an
 * application never supplies one.
 *
 * The group lists the controller itself, and the inventory leaves that entry out. Every node an
 * application iterates is therefore a node the controller drives.
 *
 * Subscribes each distinct shadow name to the shadow get and update planes, so the cached model
 * tracks the group continuously. Two filters per shadow name share one table with the agent's own,
 * bounded by CONFIG_OSAL_MQTT_SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS.
 *
 * This stops at membership: it reads no node's data model and no node's params. Call
 * esp_rmaker_controller_node_sync() for the nodes an application wants to hold.
 *
 * @note A node handle stays valid while the node stays in the group.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_NOT_FOUND if the node is in no group. Any inventory held is dropped, with its
 *         shadow filters. See also esp_rmaker_controller_api_get().
 * @return ESP_RMAKER_FAIL if a shadow could not be observed. The model still holds what synced,
 *         and the failing filter is logged.
 */
esp_rmaker_error_t esp_rmaker_controller_group_sync(void);

/** @brief First node the controller drives, or 0 when the group is empty or unsynced. */
esp_rmaker_controller_node_t esp_rmaker_controller_node_first(void);

/**
 * @brief Node after @p node, or 0 at the end of the list.
 *
 * @note A loop that spans a rebuild can end early.
 */
esp_rmaker_controller_node_t esp_rmaker_controller_node_next(esp_rmaker_controller_node_t node);

/** @brief Node with this id, or 0 if the group has none. The controller's own id finds none. */
esp_rmaker_controller_node_t esp_rmaker_controller_node_find(const char *node_id);

/**
 * @brief Get a copy of the node id of @p node.
 *
 * @param[in]  node Node to read.
 * @param[out] out  Receives the copy. The caller frees it with free(). Set to NULL on any error.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG for a 0 handle, a handle of another kind, or a NULL @p out.
 * @return ESP_RMAKER_NOT_FOUND for a stale handle.
 * @return ESP_RMAKER_NO_MEM if the copy cannot be allocated.
 */
esp_rmaker_error_t esp_rmaker_controller_node_get_id(esp_rmaker_controller_node_t node, char **out);

/** @brief Report whether @p node sits under @p subgroup_id. */
bool esp_rmaker_controller_node_in_subgroup(esp_rmaker_controller_node_t node, const char *subgroup_id);

/** @brief First subgroup of the controller's group, or 0 if it has none. */
esp_rmaker_controller_subgroup_t esp_rmaker_controller_subgroup_first(void);

/**
 * @brief Subgroup after @p sg, or 0 at the end of the list.
 *
 * @note A loop that spans a rebuild can end early.
 */
esp_rmaker_controller_subgroup_t esp_rmaker_controller_subgroup_next(esp_rmaker_controller_subgroup_t sg);

/**
 * @brief Get a copy of the subgroup id of @p sg.
 *
 * @param[in]  sg Subgroup to read.
 * @param[out] out  Receives the copy. The caller frees it with free(). Set to NULL on any error.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG for a 0 handle, a handle of another kind, or a NULL @p out.
 * @return ESP_RMAKER_NOT_FOUND for a stale handle.
 * @return ESP_RMAKER_NO_MEM if the copy cannot be allocated.
 */
esp_rmaker_error_t esp_rmaker_controller_subgroup_get_id(esp_rmaker_controller_subgroup_t sg, char **out);

/**
 * @brief Get a copy of the subgroup name of @p sg.
 *
 * @param[in]  sg Subgroup to read.
 * @param[out] out  Receives the copy. The caller frees it with free(). Set to NULL on any error.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG for a 0 handle, a handle of another kind, or a NULL @p out.
 * @return ESP_RMAKER_NOT_FOUND for a stale handle, or if the cloud reported no name.
 * @return ESP_RMAKER_NO_MEM if the copy cannot be allocated.
 */
esp_rmaker_error_t esp_rmaker_controller_subgroup_get_name(esp_rmaker_controller_subgroup_t sg, char **out);

/* Config *********************************************************************/

/**
 * @brief Bring one node's cached data model and param values up to date.
 *
 * Reads the node's shadow, and fetches its config only when none is held or the reported
 * ``ncfg_ver`` moved past the tree it holds. A node whose data model did not change therefore
 * costs one shadow get and no REST call. This is what an application calls after
 * esp_rmaker_controller_group_sync(); the two calls below are the steps it runs.
 *
 * @param[in] node Node to bring up to date.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_NOT_SUPPORTED if the node reports a data model other than "default".
 * @return ESP_RMAKER_TIMEOUT if no shadow reply arrived.
 */
esp_rmaker_error_t esp_rmaker_controller_node_sync(esp_rmaker_controller_node_t node);

/**
 * @brief Fetch and parse one node's config into device and param handles.
 *
 * Fetch is explicit so the application controls memory: a group of 30 nodes holds 30 parsed trees
 * only if it asks for them.
 *
 * Calling this on a node whose config is already held matches devices and params by id. Devices
 * and params the new config keeps keep their handles. The handles of the others become stale.
 *
 * @param[in] node Node to fetch.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_NOT_SUPPORTED if the node reports a data model other than "default". No tree
 *         is built.
 */
esp_rmaker_error_t esp_rmaker_controller_node_fetch_config(esp_rmaker_controller_node_t node);

/**
 * @brief Report whether @p node's held config still matches the ``ncfg_ver`` its shadow reports.
 *
 * false when no config is held, and false once a reported ``ncfg_ver`` moves past the held tree.
 * A shadow get records the token, so esp_rmaker_controller_node_refresh_params() followed by this
 * check tells an application whether the config fetch is owed at all.
 */
bool esp_rmaker_controller_node_config_is_current(esp_rmaker_controller_node_t node);

/** @brief Free the parsed device and param tree of @p node. Every handle into it goes stale. */
void esp_rmaker_controller_node_release_config(esp_rmaker_controller_node_t node);

/**
 * @brief Get a copy of the data model of @p node.
 *
 * Use this to say why a node was skipped.
 *
 * @param[in]  node Node to read.
 * @param[out] out  Receives the copy. The caller frees it with free(). Set to NULL on any error.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG for a 0 handle, a handle of another kind, or a NULL @p out.
 * @return ESP_RMAKER_NOT_FOUND for a stale handle, or before the first fetch attempt.
 * @return ESP_RMAKER_NO_MEM if the copy cannot be allocated.
 */
esp_rmaker_error_t esp_rmaker_controller_node_get_data_model(esp_rmaker_controller_node_t node, char **out);

/** @brief Report whether @p node's data model is one this SDK parses. false before a fetch. */
bool esp_rmaker_controller_node_is_supported_data_model(esp_rmaker_controller_node_t node);

/**
 * @brief First device or service of @p node, or 0 if its config is not held.
 *
 * Devices and services share one list, matching esp_rmaker_node_get_first_device() on the node
 * side. esp_rmaker_controller_device_is_service() tells them apart.
 */
esp_rmaker_controller_device_t esp_rmaker_controller_node_device_first(esp_rmaker_controller_node_t node);

/**
 * @brief Device or service after @p dev, or 0 at the end of the list.
 *
 * @note A loop that spans a rebuild can end early.
 */
esp_rmaker_controller_device_t esp_rmaker_controller_device_next(esp_rmaker_controller_device_t dev);

/** @brief Device or service of @p node with this id, or 0. */
esp_rmaker_controller_device_t esp_rmaker_controller_node_device_by_id(esp_rmaker_controller_node_t node,
        const char *id);

/** @brief First device or service of @p node with this type, or 0. */
esp_rmaker_controller_device_t esp_rmaker_controller_node_device_by_type(esp_rmaker_controller_node_t node,
        const char *type);

/**
 * @brief Get a copy of the id of @p dev.
 *
 * @param[in]  dev Device to read.
 * @param[out] out  Receives the copy. The caller frees it with free(). Set to NULL on any error.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG for a 0 handle, a handle of another kind, or a NULL @p out.
 * @return ESP_RMAKER_NOT_FOUND for a stale handle.
 * @return ESP_RMAKER_NO_MEM if the copy cannot be allocated.
 */
esp_rmaker_error_t esp_rmaker_controller_device_get_id(esp_rmaker_controller_device_t dev, char **out);

/**
 * @brief Get a copy of the type of @p dev.
 *
 * @param[in]  dev Device to read.
 * @param[out] out  Receives the copy. The caller frees it with free(). Set to NULL on any error.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG for a 0 handle, a handle of another kind, or a NULL @p out.
 * @return ESP_RMAKER_NOT_FOUND for a stale handle, or if the config reported no type.
 * @return ESP_RMAKER_NO_MEM if the copy cannot be allocated.
 */
esp_rmaker_error_t esp_rmaker_controller_device_get_type(esp_rmaker_controller_device_t dev, char **out);

/** @brief Report whether @p dev is a service rather than a device. */
bool esp_rmaker_controller_device_is_service(esp_rmaker_controller_device_t dev);

/** @brief Node @p dev belongs to, or 0. */
esp_rmaker_controller_node_t esp_rmaker_controller_device_get_node(esp_rmaker_controller_device_t dev);

/**
 * @brief Primary param of @p dev, or 0 if the config named none or it is gone.
 *
 * A hub shows this one param on a device's card, so the card reads as the device's state.
 */
esp_rmaker_controller_param_t esp_rmaker_controller_device_get_primary_param(esp_rmaker_controller_device_t dev);

/**
 * @brief Service of @p node with this type, or 0.
 *
 * The same lookup as esp_rmaker_controller_node_device_by_type(), named so the common case reads
 * right. A service is addressable exactly like a device at both the node and the group level.
 */
esp_rmaker_controller_device_t esp_rmaker_controller_node_service_by_type(esp_rmaker_controller_node_t node,
        const char *type);

/** @brief First param of @p dev, or 0 if it has none. */
esp_rmaker_controller_param_t esp_rmaker_controller_device_param_first(esp_rmaker_controller_device_t dev);

/**
 * @brief Param after @p param, or 0 at the end of the list.
 *
 * @note A loop that spans a rebuild can end early.
 */
esp_rmaker_controller_param_t esp_rmaker_controller_param_next(esp_rmaker_controller_param_t param);

/** @brief Param of @p dev with this id, or 0. */
esp_rmaker_controller_param_t esp_rmaker_controller_device_param_by_id(esp_rmaker_controller_device_t dev,
        const char *id);

/** @brief Param of @p dev with this type, or 0. */
esp_rmaker_controller_param_t esp_rmaker_controller_device_param_by_type(esp_rmaker_controller_device_t dev,
        const char *type);

/**
 * @brief Resolve one param by device type and param type in a single call.
 *
 * @param[in] node        Node to search.
 * @param[in] device_type Device or service type, e.g. ESP_RMAKER_DEVICE_LIGHTBULB.
 * @param[in] param_type  Param type, e.g. ESP_RMAKER_PARAM_POWER.
 *
 * @return The param handle, or 0 if the node has no such device or param.
 */
esp_rmaker_controller_param_t esp_rmaker_controller_node_find_param(esp_rmaker_controller_node_t node,
        const char *device_type, const char *param_type);

/**
 * @brief Get a copy of the id of @p param.
 *
 * @param[in]  param Param to read.
 * @param[out] out  Receives the copy. The caller frees it with free(). Set to NULL on any error.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG for a 0 handle, a handle of another kind, or a NULL @p out.
 * @return ESP_RMAKER_NOT_FOUND for a stale handle.
 * @return ESP_RMAKER_NO_MEM if the copy cannot be allocated.
 */
esp_rmaker_error_t esp_rmaker_controller_param_get_id(esp_rmaker_controller_param_t param, char **out);

/**
 * @brief Get a copy of the type of @p param.
 *
 * @param[in]  param Param to read.
 * @param[out] out  Receives the copy. The caller frees it with free(). Set to NULL on any error.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG for a 0 handle, a handle of another kind, or a NULL @p out.
 * @return ESP_RMAKER_NOT_FOUND for a stale handle, or if the config reported no type.
 * @return ESP_RMAKER_NO_MEM if the copy cannot be allocated.
 */
esp_rmaker_error_t esp_rmaker_controller_param_get_type(esp_rmaker_controller_param_t param, char **out);

/** @brief Value type of @p param, or RMAKER_VAL_TYPE_INVALID if the config reported none. */
esp_rmaker_val_type_t esp_rmaker_controller_param_get_data_type(esp_rmaker_controller_param_t param);

/**
 * @brief UI hint of @p param, or RMAKER_UI_TYPE_NONE if the config reported none.
 *
 * RMAKER_UI_TYPE_UNKNOWN for a ``ui_type`` string this SDK does not map, so a hub can tell an
 * absent hint apart from an unrecognized one.
 */
esp_rmaker_ui_type_t esp_rmaker_controller_param_get_ui_type(esp_rmaker_controller_param_t param);

/** @brief Device or service @p param belongs to, or 0. */
esp_rmaker_controller_device_t esp_rmaker_controller_param_get_device(esp_rmaker_controller_param_t param);

/** @brief Property flags of @p param. */
esp_rmaker_param_property_flags_t esp_rmaker_controller_param_get_props(esp_rmaker_controller_param_t param);

/**
 * @brief Copy the bounds of @p param into @p out.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG for a 0 handle, a handle of another kind, or a NULL @p out.
 * @return ESP_RMAKER_NOT_FOUND for a stale handle, or if the config reported no bounds.
 */
esp_rmaker_error_t esp_rmaker_controller_param_get_bounds(esp_rmaker_controller_param_t param,
        esp_rmaker_param_bounds_t *out);

/* State read *****************************************************************/

/**
 * @brief Seed @p node's param values with a named-shadow get.
 *
 * Blocks until the reply arrives or CONFIG_ESP_RMAKER_NEO_CONTROLLER_SHADOW_GET_TIMEOUT_MS elapses, then
 * applies ``state.reported.params`` into the node's param handles.
 *
 * @note Needed once per node. After that the shadow update subscription established by
 *       esp_rmaker_controller_group_sync() keeps the values current, since a node publishes
 *       only the params that changed.
 *
 * @param[in] node Node to read. Its config must be held.
 *
 * @return ESP_RMAKER_OK on success, ESP_RMAKER_TIMEOUT if no reply arrived.
 * @return ESP_RMAKER_NOT_FOUND if the node is in no group.
 */
esp_rmaker_error_t esp_rmaker_controller_node_refresh_params(esp_rmaker_controller_node_t node);

/**
 * @brief Copy out @p param's cached value.
 *
 * The value is copied rather than returned by pointer, because the shadow update plane mutates the
 * tree from the MQTT task.
 *
 * @note A string, object or array value is allocated for the caller. Release it with
 *       esp_rmaker_val_free().
 *
 * @param[in]  param   Param to read.
 * @param[out] out_val Receives the value.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_NOT_FOUND for a stale handle, or if no value has been seen for this param yet.
 */
esp_rmaker_error_t esp_rmaker_controller_param_get_val(esp_rmaker_controller_param_t param,
        esp_rmaker_param_val_t *out_val);

/** @brief Report the online flag @p node's shadow last reported. */
bool esp_rmaker_controller_node_is_online(esp_rmaker_controller_node_t node);

/* Live push ******************************************************************/

/**
 * @brief Register a callback for every param value the group reports as changed.
 *
 * The subscription itself is established by esp_rmaker_controller_group_sync(); this only attaches
 * the notification. The callback runs on the controller task, so it may call any controller API
 * except esp_rmaker_controller_disable().
 *
 * @param[in] cb   Callback, or NULL to detach.
 * @param[in] priv Opaque pointer passed to @p cb.
 *
 * @return ESP_RMAKER_OK on success.
 */
esp_rmaker_error_t esp_rmaker_controller_watch_params(esp_rmaker_controller_param_cb_t cb, void *priv);

/** @brief Detach the param-change callback. */
esp_rmaker_error_t esp_rmaker_controller_unwatch_params(void);

/**
 * @brief Register the callback for structural changes: nodes, subgroups, devices and params.
 *
 * The first list holds ADDED for each object in the current snapshot. After that, each group sync,
 * config fetch, esp_rmaker_controller_node_release_config() and esp_rmaker_controller_disable()
 * that changes the snapshot delivers one list, in the order they ran. This covers the rebuilds the
 * SDK starts on its own, for example after the cloud moves the controller to another group, or
 * after a node reports a new ``ncfg_ver``.
 *
 * @note Do not call a blocking controller function from @p cb. Post the work to another task.
 *
 * @param[in] cb   Callback. A later call replaces it.
 * @param[in] priv Opaque pointer passed to @p cb.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if @p cb is NULL.
 */
esp_rmaker_error_t esp_rmaker_controller_watch_changes(esp_rmaker_controller_change_cb_t cb, void *priv);

/** @brief Detach the change callback. A list not yet delivered is dropped. */
void esp_rmaker_controller_unwatch_changes(void);

/**
 * @brief Register a callback for every node whose online flag changed.
 *
 * ``online`` is a shadow field rather than a param, so esp_rmaker_controller_watch_params() never
 * reports it. Register this to learn that a node went away without polling for it.
 *
 * @param[in] cb   Callback, or NULL to detach.
 * @param[in] priv Opaque pointer passed to @p cb.
 *
 * @return ESP_RMAKER_OK on success.
 */
esp_rmaker_error_t esp_rmaker_controller_watch_nodes(esp_rmaker_controller_node_cb_t cb, void *priv);

/** @brief Detach the online-change callback. */
esp_rmaker_error_t esp_rmaker_controller_unwatch_nodes(void);

/* State write ****************************************************************/

/**
 * @brief Write one param on one node.
 *
 * Publishes ``{"<device id>":{"<param id>":v}}``, which is how a node addresses its params by
 * id.
 *
 * @param[in] param Param to write.
 * @param[in] val   Value. Copied; a string value is not retained.
 *
 * @return ESP_RMAKER_OK once the publish is accepted.
 * @return ESP_RMAKER_INVALID_ARG if the param's config lacks PROP_FLAG_WRITE, or a string value is
 *         not UTF-8.
 * @return ESP_RMAKER_NO_MEM if the payload does not fit.
 * @return ESP_RMAKER_NOT_FOUND for a stale handle. Nothing is published.
 * @return ESP_RMAKER_INVALID_STATE if the controller is on standby, or MQTT is not connected.
 */
esp_rmaker_error_t esp_rmaker_controller_param_write(esp_rmaker_controller_param_t param,
        esp_rmaker_param_val_t val);

/**
 * @brief Begin a batch of param writes to one node, so they land in one publish.
 *
 * @param[in] node Node to write to.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_STATE if a batch is already open on @p node.
 */
esp_rmaker_error_t esp_rmaker_controller_node_write_begin(esp_rmaker_controller_node_t node);

/**
 * @brief Stage one param write into @p node's open batch.
 *
 * @param[in] node  Node the batch is open on.
 * @param[in] param Param to write. Must belong to @p node.
 * @param[in] val   Value. Copied.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if the param's config lacks PROP_FLAG_WRITE, or it is not a param
 *         of @p node.
 * @return ESP_RMAKER_INVALID_STATE if no batch is open on @p node.
 */
esp_rmaker_error_t esp_rmaker_controller_node_write_add(esp_rmaker_controller_node_t node,
        esp_rmaker_controller_param_t param, esp_rmaker_param_val_t val);

/**
 * @brief Publish @p node's open batch as one message and close it.
 *
 * @param[in] node Node the batch is open on.
 *
 * @return ESP_RMAKER_OK once the publish is accepted.
 * @return ESP_RMAKER_NOT_FOUND if the node or a staged param is stale. The batch is dropped, and
 *         nothing is published.
 * @return ESP_RMAKER_INVALID_ARG if a staged string value is not UTF-8. The batch is dropped.
 * @return ESP_RMAKER_NO_MEM if the staged writes do not fit one payload. The batch is dropped.
 * @return ESP_RMAKER_INVALID_STATE if no batch is open, the controller is on standby, or MQTT is
 *         not connected.
 */
esp_rmaker_error_t esp_rmaker_controller_node_write_commit(esp_rmaker_controller_node_t node);

/** @brief Discard @p node's open batch without publishing. */
void esp_rmaker_controller_node_write_abort(esp_rmaker_controller_node_t node);

/**
 * @brief Write one param on every node of the group, or of one subgroup, in a single publish.
 *
 * Publishes ``{"<device type>":{"params":{"<param type>":v}}}`` on the group or subgroup control
 * topic. This reaches services exactly as it reaches devices, so a hub can set a whole group's
 * timezone in one message.
 *
 * @note This takes bare type strings rather than a param handle, so it cannot check the target
 *       param for PROP_FLAG_WRITE the way esp_rmaker_controller_param_write() does.
 *
 * @param[in] subgroup_id Subgroup to address, or NULL for the whole group.
 * @param[in] device_type Device or service type. Matched exactly; there is no wildcard.
 * @param[in] param_type  Param type.
 * @param[in] val         Value. Copied.
 *
 * @return ESP_RMAKER_OK once the publish is accepted.
 * @return ESP_RMAKER_INVALID_ARG if a type is empty, or a string value is not UTF-8.
 * @return ESP_RMAKER_NO_MEM if the payload does not fit.
 * @return ESP_RMAKER_NOT_FOUND if the node is in no group.
 * @return ESP_RMAKER_INVALID_STATE if the controller is on standby, or MQTT is not connected.
 */
esp_rmaker_error_t esp_rmaker_controller_group_write(const char *subgroup_id, const char *device_type,
        const char *param_type, esp_rmaker_param_val_t val);

/**
 * @brief Begin a batch of group writes, so they land in one publish.
 *
 * A group has no handle, so one batch is open at a time for the whole controller. The target is
 * fixed here rather than per write: every staged write goes to the same topic.
 *
 * @param[in] subgroup_id Subgroup to address, or NULL for the whole group.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_STATE if a batch is already open.
 */
esp_rmaker_error_t esp_rmaker_controller_group_write_begin(const char *subgroup_id);

/**
 * @brief Stage one param write into the open group batch.
 *
 * Stage a write per device type to drive several types from one message: a receiving node applies
 * the types it owns a device of and ignores the rest.
 *
 * @param[in] device_type Device or service type. Matched exactly; there is no wildcard.
 * @param[in] param_type  Param type.
 * @param[in] val         Value. Copied.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_STATE if no batch is open.
 */
esp_rmaker_error_t esp_rmaker_controller_group_write_add(const char *device_type,
        const char *param_type, esp_rmaker_param_val_t val);

/**
 * @brief Publish the open group batch as one message and close it.
 *
 * @note The whole batch shares one payload buffer. A batch too large to fit is refused here, in
 *       full, rather than published truncated.
 *
 * @return ESP_RMAKER_OK once the publish is accepted.
 * @return ESP_RMAKER_NO_MEM if the staged writes do not fit one payload.
 * @return ESP_RMAKER_INVALID_ARG if a staged string value is not UTF-8.
 * @return ESP_RMAKER_INVALID_STATE if no batch is open, the batch is empty, the controller is on
 *         standby, or MQTT is not connected.
 */
esp_rmaker_error_t esp_rmaker_controller_group_write_commit(void);

/** @brief Discard the open group batch without publishing. */
void esp_rmaker_controller_group_write_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_ESP_RMAKER_NEO_CONTROLLER */

#endif /* __ESP_RMAKER_CONTROLLER_H__ */
