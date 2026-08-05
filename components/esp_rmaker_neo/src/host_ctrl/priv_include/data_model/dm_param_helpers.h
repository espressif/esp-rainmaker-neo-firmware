/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dm_param_helpers.h
 * @brief Data model param helpers shared between the self handlers
 *        and the bridge per-child handlers.
 *
 * Built as part of the data model (see esp_rmaker_neo/sources.cmake) -
 * these helpers depend on the data model's ``PROP_FLAG_*`` bits and on
 * ``_esp_rmaker_param_t``.
 */

#ifndef __HOST_CTRL_PRIVATE_DATA_MODEL_PARAM_HELPERS_H__
#define __HOST_CTRL_PRIVATE_DATA_MODEL_PARAM_HELPERS_H__

#include <stddef.h>
#include <stdint.h>

#include "esp_rmaker_data_model.h"
#include "esp_rmaker_error_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse a sequence of property characters into a
 *        bitmap of ``PROP_FLAG_*`` flags. Invalid characters are logged
 *        and skipped.
 *
 * @param[in] start Pointer to the first character.
 * @param[in] end   Pointer past the last character.
 */
uint8_t esp_rmaker_host_ctrl_param_prop_flags_from_chars(const char *start, const char *end);

/**
 * @brief Format a property bitmap as ASCII characters into the given
 *        buffer.
 *
 * If ``buf`` is NULL, returns the number of characters that would be
 * written (i.e. popcount of the bitmap). Otherwise writes up to
 * ``buf_size`` characters and returns the remaining capacity, or -1 on
 * failure.
 */
int esp_rmaker_host_ctrl_format_param_props(uint8_t flags, char *buf, size_t buf_size);

/**
 * @brief Default device write callback shared by the self and bridge
 *        add-param paths. Logs the call and forwards to
 *        ::esp_rmaker_param_update.
 */
esp_rmaker_error_t esp_rmaker_host_ctrl_device_write_cb(
    const esp_rmaker_device_t *device,
    const esp_rmaker_param_t *param,
    const esp_rmaker_param_val_t val,
    void *priv_data,
    esp_rmaker_write_ctx_t *ctx);

/**
 * @brief Callback to resolve the target node for an add-param request.
 *
 * Invoked once at the top of the add-param flow, before any device is
 * created or looked up. Return the node the device should live on. The
 * bridge path returns the child's slot-embedded node; the self path
 * passes NULL for the hook (the handler defaults to the self node).
 *
 * Return NULL to abort the add-param flow (the SDK will respond with
 * an error code on the wire).
 */
typedef esp_rmaker_node_t *(*esp_rmaker_host_ctrl_resolve_node_t)(void *priv);

/**
 * @brief Wire-level add_param handler. Parses the add_param
 *        payload, creates the device (with the default write callback)
 *        if it does not already exist, creates the param, and sends the
 *        response over the host control channel.
 */
void esp_rmaker_host_ctrl_handle_add_param(
    uint8_t *payload, size_t payload_length);

/**
 * @brief Same as ::esp_rmaker_host_ctrl_handle_add_param but resolves
 *        the target node via ``resolve_node(priv)`` before device lookup /
 *        creation. Used by the bridge to route adds onto a child's node.
 */
void esp_rmaker_host_ctrl_handle_add_param_with_hook(
    uint8_t *payload, size_t payload_length,
    esp_rmaker_host_ctrl_resolve_node_t resolve_node,
    void *resolve_node_priv);

/**
 * @brief Wire-level update_param handler. Payload format:
 *        ``<device_id>|<param_id>|<typed_value>|``.
 */
void esp_rmaker_host_ctrl_handle_update_param(
    uint8_t *payload, size_t payload_length);

/**
 * @brief Same as ::esp_rmaker_host_ctrl_handle_update_param but
 *        resolves the target node via ``resolve_node(priv)``. Used by
 *        the bridge to route updates onto a child's node.
 */
void esp_rmaker_host_ctrl_handle_update_param_with_hook(
    uint8_t *payload, size_t payload_length,
    esp_rmaker_host_ctrl_resolve_node_t resolve_node,
    void *resolve_node_priv);

/**
 * @brief Wire-level get_param handler. Payload format:
 *        ``<device_id>|<param_id>|``. Response payload:
 *        ``<typed_value>|<properties>``.
 */
void esp_rmaker_host_ctrl_handle_get_param(
    uint8_t *payload, size_t payload_length);

/**
 * @brief Same as ::esp_rmaker_host_ctrl_handle_get_param but
 *        resolves the target node via ``resolve_node(priv)``. Used by
 *        the bridge to route gets onto a child's node.
 */
void esp_rmaker_host_ctrl_handle_get_param_with_hook(
    uint8_t *payload, size_t payload_length,
    esp_rmaker_host_ctrl_resolve_node_t resolve_node,
    void *resolve_node_priv);

#ifdef __cplusplus
}
#endif

#endif /* __HOST_CTRL_PRIVATE_DATA_MODEL_PARAM_HELPERS_H__ */
