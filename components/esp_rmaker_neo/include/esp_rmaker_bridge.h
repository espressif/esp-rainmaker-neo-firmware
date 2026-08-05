/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_bridge.h
 * @brief Bridge support for the ESP RainMaker Neo SDK.
 *
 * A bridge node is a standard RainMaker Neo node that, in addition to its own
 * functionality, proxies cloud connectivity for a set of bridged child
 * devices. Children do not have their own MQTT connection or certificate;
 * cloud-side IoT Rules rewrite all child traffic onto a bridge-owned
 * namespace, and the bridge multiplexes that traffic over a single MQTT
 * connection.
 *
 * The SDK is intentionally agnostic to *how* bridging happens on the
 * bridge-side protocol (Zigbee, Matter, BLE-mesh, proprietary RF, etc.).
 * This header exposes only:
 *  - the lifecycle of a child Thing on the cloud (add/remove);
 *  - marking a child reachable/unreachable;
 *  - introspection of child handles.
 *
 * Child param/iparam state reporting and inbound cloud->child param
 * delivery reuse the existing data-model APIs. Create virtual
 * devices / endpoints under the bridge node, attach them to a child
 * handle, then call ::esp_rmaker_state_mark_for_update on their params
 * as you would for self. Inbound writes addressed to the child are
 * applied to the same virtual devices by the SDK's normal param-write
 * dispatch - no per-child callback is exposed.
 *
 * All entry points in this header require ``CONFIG_RMNG_BRIDGE_ENABLED=y``.
 */

#ifndef __ESP_RMAKER_BRIDGE_H__
#define __ESP_RMAKER_BRIDGE_H__

#include "sdkconfig.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

/* Includes *******************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_rmaker_error_types.h"
#include "esp_rmaker_node.h"

/* Public types ***************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for a bridged child device.
 *
 * Returned in the ``RMAKER_EVENT_BRIDGE_CHILD_ADDED`` event payload after
 * a successful ::esp_rmaker_bridge_add_child cloud round-trip, and used
 * as the identifier for all subsequent per-child operations.
 */
typedef struct esp_rmaker_bridge_child *esp_rmaker_bridge_child_handle_t;

/**
 * @brief Event data for ::RMAKER_EVENT_BRIDGE_CHILD_ADDED.
 */
typedef struct {
    esp_rmaker_bridge_child_handle_t child;   /**< Newly-created child handle. */
    const char *child_thing_name;             /**< Cloud-assigned thing name. */
    const char *bridge_local_id;              /**< Identifier supplied by the bridge. */
} esp_rmaker_event_bridge_child_added_t;

/**
 * @brief Event data for ::RMAKER_EVENT_BRIDGE_CHILD_REMOVED.
 */
typedef struct {
    const char *child_thing_name;             /**< Removed child thing name. */
    const char *bridge_local_id;              /**< Identifier supplied by the bridge. */
} esp_rmaker_event_bridge_child_removed_t;

/**
 * @brief Event data for the bridge-child failure events.
 */
typedef struct {
    const char *child_suffix;                 /**< Suffix the caller requested (add) or NULL (remove). */
    const char *bridge_local_id;              /**< Identifier supplied by the bridge. */
    const char *error;                        /**< Error string from the cloud, or "timeout". */
} esp_rmaker_event_bridge_child_failed_t;

/**
 * @brief Event data for ::RMAKER_EVENT_BRIDGE_CHILD_GROUP_INFO_UPDATED.
 */
typedef struct {
    esp_rmaker_bridge_child_handle_t child;   /**< Child whose group info changed. */
    const char *group_info_str;               /**< New group info string (``<primary>[-<sg>-...]``). */
} esp_rmaker_event_bridge_child_group_info_t;

/* Public functions ***********************************************/

/**
 * @brief Request creation of a bridged child Thing on the cloud.
 *
 * Always performs a cloud round-trip. The cloud is authoritative for
 * idempotency: re-issuing this call with a previously-seen
 * ``bridge_local_id`` returns the same child Thing name without creating
 * a duplicate.
 *
 * Asynchronous. On success the SDK posts ``RMAKER_EVENT_BRIDGE_CHILD_ADDED``
 * with the child handle and assigned thing name. On failure or timeout it
 * posts ``RMAKER_EVENT_BRIDGE_CHILD_ADD_FAILED``.
 *
 * @param[in] child_suffix     The desired suffix component of the child's
 *                             thing name (after the ``<parent>--``
 *                             prefix). Must match ``[A-Za-z0-9_]{1,32}``
 *                             - single hyphens not permitted.
 * @param[in] bridge_local_id  The bridge-protocol identifier for this
 *                             child (e.g. Zigbee EUI-64). Used for
 *                             idempotency across bridge reboots.
 *
 * @return ESP_RMAKER_OK if the request was enqueued; the event-loop
 *         event reports the eventual outcome.
 */
esp_rmaker_error_t esp_rmaker_bridge_add_child(
    const char *child_suffix,
    const char *bridge_local_id);

/**
 * @brief Request removal of a previously-added bridged child.
 *
 * Asynchronous. The SDK posts ``RMAKER_EVENT_BRIDGE_CHILD_REMOVED``
 * **optimistically**, immediately after the ``removeChild`` cloud
 * publish succeeds - before the cloud's ``bridgeAck`` arrives. The
 * subsequent ``bridgeAck`` is correlated by ``request_id``: on success
 * it is informational only; on non-success (cloud-side failure) the SDK
 * additionally posts ``RMAKER_EVENT_BRIDGE_CHILD_REMOVE_FAILED``.
 *
 * Consequence: if the cloud fails the remove, the application sees both
 * ``REMOVED`` (local teardown already happened) and ``REMOVE_FAILED``
 * (cloud disagrees). The application is responsible for reconciling -
 * e.g. re-issuing ``add_child`` if the child must remain registered.
 *
 * After the ``REMOVED`` event fires, the handle is no longer valid.
 *
 * @param[in] child  Handle returned in the ``_ADDED`` event.
 *
 * @return ESP_RMAKER_OK if the request was enqueued.
 */
esp_rmaker_error_t esp_rmaker_bridge_remove_child(
    esp_rmaker_bridge_child_handle_t child);

/**
 * @brief Mark a bridged child reachable or unreachable on the
 *        bridge-side network.
 *
 * Queues an iparams shadow update for the child with
 * ``{state:{reported:{online: <online>}}}``. Use this when the child is
 * still attached (do not call ::esp_rmaker_bridge_remove_child for
 * transient unreachability).
 *
 * @param[in] child   Child handle.
 * @param[in] online  True if reachable, false otherwise.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_bridge_child_mark_online(
    esp_rmaker_bridge_child_handle_t child,
    bool online);

/**
 * @brief Get the cloud-assigned thing name for a child.
 *
 * @param[in] child  Child handle.
 *
 * @return Pointer to a null-terminated string owned by the SDK, or NULL
 *         if ``child`` is invalid. The string is valid until the child
 *         is removed.
 */
const char *esp_rmaker_bridge_child_thing_name(
    esp_rmaker_bridge_child_handle_t child);

/**
 * @brief Get the bridge-protocol identifier for a child.
 *
 * @param[in] child  Child handle.
 *
 * @return Pointer to the bridge_local_id string the child was registered
 *         with, owned by the SDK; or NULL if ``child`` is invalid.
 */
const char *esp_rmaker_bridge_child_bridge_local_id(
    esp_rmaker_bridge_child_handle_t child);

/**
 * @brief Report the per-child node configuration to the cloud.
 *
 * Builds the slice of the node config that belongs to ``child``, hashes it, and
 * publishes ``setNodeConfig`` on the child's MQTT topic only if the
 * checksum differs from the value persisted in the per-child NVS record.
 *
 * @param[in] child Child handle (must be READY).
 * @return ESP_RMAKER_OK on success.
 */
esp_rmaker_error_t esp_rmaker_report_node_config_for_child(
    esp_rmaker_bridge_child_handle_t child);

/**
 * @brief Get the per-child node handle for ``child``.
 *
 * Each bridge child owns its own ::esp_rmaker_node_t.
 * Use this handle with the standard node helpers to populate the child
 * after ``RMAKER_EVENT_BRIDGE_CHILD_ADDED``.
 *
 * The returned handle is stable for the slot's lifetime; freeing the
 * child invalidates the contents but the pointer itself is reused on the
 * next slot allocation.
 *
 * @param[in] child Child handle returned in the ``_ADDED`` event.
 * @return Node handle, or NULL if ``child`` is NULL.
 */
esp_rmaker_node_t *esp_rmaker_bridge_child_node(
    esp_rmaker_bridge_child_handle_t child);

/**
 * @brief Commit the current set of virtual devices/endpoints attached
 *        to ``child`` and (re-)publish its node configuration to the
 *        cloud.
 *
 * Call after attaching the child's virtual devices. The SDK builds the
 * per-child node config slice, hashes it,
 * and if the checksum differs from the per-child NVS record publishes
 * ``setNodeConfig`` on the child's MQTT topic.
 *
 * @param[in] child  Child handle (must be ``READY``).
 * @return ESP_RMAKER_OK on enqueue; the eventual publish outcome is
 *         signalled via the standard cloud-event ack callback.
 */
esp_rmaker_error_t esp_rmaker_bridge_child_commit_devices(
    esp_rmaker_bridge_child_handle_t child);

#ifdef __cplusplus
}
#endif


#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

#endif /* __ESP_RMAKER_BRIDGE_H__ */
