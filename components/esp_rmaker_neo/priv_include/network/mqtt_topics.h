/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mqtt_topics.h
 * @brief MQTT topics used for RainMaker Neo
 */

#ifndef __MQTT_TOPICS_H__
#define __MQTT_TOPICS_H__

/* Standard includes */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "sdkconfig.h"

#include "esp_rmaker_error_types.h"

/* Buffer size for MQTT topics */
#define MQTT_TOPIC_BUFFER_SIZE 150

/* Topic ctx ******************************************************************/

/**
 * @brief Per-Thing topic-builder ops.
 *
 * Each callback writes the relevant string for the Thing referenced by
 * ``priv`` into the caller-provided buffer and returns the number of
 * bytes written (excluding the NUL terminator), or ``-1`` on failure /
 * insufficient buffer.
 *
 * Implementations are responsible for any allocation/free internal to
 * resolving the string (e.g. self ctx duplicates from credentials/NVS
 * and frees the temp); the caller only sees the populated buffer.
 */
typedef struct esp_rmaker_topic_ops {
    int (*write_thing_name)(void *priv, char *buf, size_t buf_size);
    int (*write_group_info_str)(void *priv, char *buf, size_t buf_size);
} esp_rmaker_topic_ops_t;

/**
 * @brief Per-Thing topic ctx - a (ops, priv) pair with a validity flag.
 *
 * @note Topic contexts MUST be backed by static storage with stable
 *       addresses for the entire program lifetime (the self ctx, or a
 *       fixed slot pool owned by the emitter). Heap-allocated contexts
 *       are unsafe: state and timeseries pipelines hold pointers to the
 *       ctx across publish cycles and have no way to learn about a free.
 *
 *       Owners signal teardown by clearing the slot's ``valid`` flag
 *       (typically aliased to the slot's "in use" bool). Downstream
 *       consumers (state ctx list, timeseries queue) check
 *       ::esp_rmaker_topic_ctx_is_valid before dereferencing ``ops`` or
 *       ``priv``; entries pointing to a now-invalid ctx are dropped on
 *       the next publish cycle.
 *
 *       ``valid == NULL`` means "always valid" - used by the self ctx,
 *       which has program lifetime.
 *
 * Identity is the ctx pointer itself: consumers compare pointers, not
 * field-wise tuples. Each Thing must therefore have exactly one stable
 * ctx address.
 */
typedef struct esp_rmaker_topic_ctx {
    const esp_rmaker_topic_ops_t *ops;
    void *priv;
    const bool *valid;
} esp_rmaker_topic_ctx_t;

/**
 * @brief Test whether a topic ctx is still safe to dereference.
 *
 * Returns ``false`` for NULL ctxs, ctxs missing ops, or ctxs whose
 * owner has invalidated the slot. A ``valid`` pointer of NULL is
 * treated as "always valid" (self ctx).
 */
static inline bool esp_rmaker_topic_ctx_is_valid(const esp_rmaker_topic_ctx_t *ctx)
{
    return ctx != NULL && ctx->ops != NULL && (ctx->valid == NULL || *ctx->valid);
}

/** Default self ops - reads name from credentials, group info from NVS. */
extern const esp_rmaker_topic_ops_t esp_rmaker_topic_ops_self;

/** Singleton self topic ctx. Pass this (or its address) wherever a
 *  topic_ctx is required for the bridge / device's own Thing. */
extern const esp_rmaker_topic_ctx_t esp_rmaker_topic_ctx_self;

/**
 * @brief Resolve the cloud thing name a topic ctx points at, for logging.
 *
 * Writes ``"<unknown>"`` if ``ctx`` is NULL / invalid / cannot resolve.
 * NUL-terminates ``buf``. Returns bytes written (excluding NUL).
 */
int esp_rmaker_topic_ctx_resolve_thing_name(const esp_rmaker_topic_ctx_t *ctx, char *buf, size_t buf_size);

/* MQTT topic function types ******************************************************/

/**
 * @brief Function type for MQTT topic functions
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
typedef int (*esp_rmaker_mqtt_topic_fn_t)(char *buffer, size_t buffer_size);

/* Function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the 'to cloud' MQTT topic for the given Thing.
 * @param[in] ctx Topic ctx identifying the target Thing.
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_to_cloud(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size);

/**
 * @brief Get the 'from cloud' MQTT topic for the node
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_from_cloud(char *buffer, size_t buffer_size);

/**
 * @brief Get the MQTT topic to subscribe to for parameter updates from external sources
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_params_to_node(char *buffer, size_t buffer_size);

/**
 * @brief Get the named (params) shadow update topic for the given Thing.
 * @param[in] ctx Topic ctx identifying the Thing
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length on success, -1 on failure.
 */
int esp_rmaker_mqtt_topic_params_named_shadow_update(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size);

/**
 * @brief Get the named (params) shadow delete topic for the given Thing.
 * @param[in] ctx Topic ctx identifying the Thing
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_params_named_shadow_delete(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size);

/**
 * @brief Get the indexed (iparams) shadow update topic for the given Thing.
 * @param[in] ctx Topic ctx identifying the Thing
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_params_indexed_shadow_update(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size);

/**
 * @brief Get the timeseries report topic for the given Thing.
 * @param[in] ctx Topic ctx identifying the Thing
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_timeseries_report(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size);

/**
 * @brief Get the MQTT topic for the direct notification
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_notify(char *buffer, size_t buffer_size);

/**
 * @brief Get the direct-notification MQTT topic for the Thing identified by
 *        ``ctx``. ``esp_rmaker_mqtt_topic_notify`` is the self wrapper.
 * @param[in] ctx Topic ctx identifying the target Thing.
 * @param[out] buffer Buffer to store the topic.
 * @param[in] buffer_size Size of the buffer.
 * @return length written on success, -1 on failure.
 */
int esp_rmaker_mqtt_topic_notify_for_node(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size);

/**
 * @brief Get the group control broadcast MQTT topic (all devices in the group).
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @param[in] primary Primary group ID (must not be NULL or empty)
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_group_control_broadcast(char *buffer, size_t buffer_size, const char *primary);

/**
 * @brief Get the group control subgroup MQTT topic (all devices in one subgroup).
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @param[in] primary Primary group ID (must not be NULL or empty)
 * @param[in] subgroup Subgroup ID (must not be NULL or empty)
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_group_control_subgroup(char *buffer, size_t buffer_size, const char *primary, const char *subgroup);

/**
 * @brief Extract the subgroup segment from an inbound group control topic.
 *
 * Accepts either of:
 *   - ``rainmaker/nodes/groups/<primary>/control`` (broadcast) -> writes ``""``.
 *   - ``rainmaker/nodes/groups/<primary>/subgroups/<sg>/control`` -> writes ``<sg>``.
 *
 * The primary segment is not returned.
 *
 * @param[in] topic Inbound MQTT topic (need not be NUL-terminated).
 * @param[in] topic_len Length of @p topic in bytes.
 * @param[out] sg_buf NUL-terminated subgroup, or ``""`` for broadcast.
 * @param[in] sg_buf_size Size of @p sg_buf. Must be at least
 *                        ``RMAKER_SUBGROUP_BUFFER_SIZE``.
 *
 * @return ESP_RMAKER_OK on success; ESP_RMAKER_INVALID_ARG on malformed
 *         topic or oversized subgroup.
 */
esp_rmaker_error_t esp_rmaker_mqtt_topic_parse_group_control_subgroup(
    const char *topic, size_t topic_len,
    char *sg_buf, size_t sg_buf_size);

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

/**
 * @brief Get the group control subgroup MQTT topic with a wildcard subgroup segment.
 *
 * Used in bridge mode to subscribe to every subgroup's control traffic
 * with a single subscription (bounded subscription count, independent of
 * subgroup membership of the bridge itself or its children).
 *
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @param[in] primary Primary group ID (must not be NULL or empty)
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_group_control_subgroup_wildcard(char *buffer, size_t buffer_size, const char *primary);

/**
 * @brief Get the bridge `to_cloud` MQTT topic for this bridge.
 *
 * Resolves to ``rainmaker/bridges/<self>/to_cloud`` (the bridge control
 * plane). The publisher clientid is enforced by the cloud-side IoT Rule
 * to equal ``<self>``.
 *
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_bridges_to_cloud(char *buffer, size_t buffer_size);

/**
 * @brief Get the MQTT topic *filter* for cloud->child `from_cloud` traffic
 *        addressed to any child of this bridge.
 *
 * Resolves to ``rainmaker/bridges/<self>/children/+/from_cloud``.
 *
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_bridges_children_from_cloud_filter(char *buffer, size_t buffer_size);

/**
 * @brief Get the MQTT topic *filter* for cloud->child unicast params
 *        traffic addressed to any child of this bridge across any shadow
 *        name variant.
 *
 * Resolves to ``rainmaker/bridges/<self>/children/+/user/+/params``.
 *
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_bridges_children_params_filter(char *buffer, size_t buffer_size);

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

/* --- Generic operations ---*/

/**
 * @brief Append '/accepted' to the provided MQTT topic
 * @param[in] topic_fn The function to write the original topic into the buffer
 * @param[out] buffer Pointer to the buffer to store the topic
 * @param[in] buffer_size The size of the buffer
 * @return length written to buffer on success. -1 on failure.
 */
int esp_rmaker_mqtt_topic_append_accepted(esp_rmaker_mqtt_topic_fn_t topic_fn, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* __MQTT_TOPICS_H__ */
