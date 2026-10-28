/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file server_config.h
 * @brief Internal side of esp_rmaker_server_config.h: the cache the getServerConfig
 *        response handler fills.
 */

#ifndef __CLOUD_SERVER_CONFIG_H__
#define __CLOUD_SERVER_CONFIG_H__

/* Includes **********************************************************************/

/* Standard includes */
#include <stdbool.h>
#include <stddef.h>

/* JSON includes */
#include "json_parser.h"

/* Error types includes */
#include "esp_rmaker_error_types.h"

/* Public API */
#include "esp_rmaker_server_config.h"

/* Configuration includes */
#include "sdkconfig.h"

#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG

/** One level or key in the getServerConfig request tree. */
typedef struct esp_rmaker_server_config_node {
    const char *name;                                       /**< Level or key name. */
    const struct esp_rmaker_server_config_node *children;   /**< Nodes in a level; NULL for a key. */
    size_t num_children;                                    /**< Number of nodes in a level. */
    size_t key;                                             /**< Value index of a key. */
} esp_rmaker_server_config_node_t;

/** Fixed storage for the value of one key. */
typedef struct {
    char *buf;      /**< Value buffer. */
    size_t size;    /**< Size of @p buf, NUL included. */
    bool stored;    /**< @p buf holds a received value. */
} esp_rmaker_server_config_value_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the cache. Idempotent. Called from the cloud manager's init.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_server_config_init(void);

/**
 * @brief Reset the cache and wake readers blocked in esp_rmaker_server_config_get(). The sync objects are kept,
 *        since an application task may still be waiting on them.
 */
void esp_rmaker_server_config_deinit(void);

/**
 * @brief Build a request object: each level becomes an object, each key a null member.
 * @param[in] nodes Top-level nodes of the tree.
 * @param[in] num_nodes Number of top-level nodes.
 * @param[out] buf Output buffer, or NULL with @p buf_size 0 to get the size. Set to "" if too small.
 * @param[in] buf_size Size of @p buf.
 * @return The required size, NUL included, or -1 for bad arguments.
 */
int esp_rmaker_server_config_build_request(const esp_rmaker_server_config_node_t *nodes, size_t num_nodes, char *buf,
        size_t buf_size);

/**
 * @brief The "getServerConfig" request object for the node's keys, built once.
 * @return Static JSON object string, or NULL if it could not be built.
 */
const char *esp_rmaker_server_config_request_data(void);

/**
 * @brief Read the string value of each key in the tree from @p jctx into @p values.
 * @note Missing keys, non-string values and values that do not fit are skipped. @p jctx is at the same object on return.
 * @param[in] values Value storage, indexed by the node key.
 */
void esp_rmaker_server_config_parse_tree(jparse_ctx_t *jctx, const esp_rmaker_server_config_node_t *nodes,
        size_t num_nodes, esp_rmaker_server_config_value_t *values);

/**
 * @brief Store the requested values from a getServerConfig response.
 * @param[in] jctx Parser context inside the "getServerConfig" object.
 * @return true if the response carried at least one requested key; false for an error or empty reply.
 */
bool esp_rmaker_server_config_parse(jparse_ctx_t *jctx);

/**
 * @brief Mark the response as received, releasing any esp_rmaker_server_config_get() waiters.
 */
void esp_rmaker_server_config_mark_received(void);

/**
 * @brief Whether the getServerConfig response has been received.
 */
bool esp_rmaker_server_config_is_received(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG */

#endif /* __CLOUD_SERVER_CONFIG_H__ */
