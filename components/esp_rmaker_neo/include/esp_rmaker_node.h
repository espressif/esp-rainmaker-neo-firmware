/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_node.h
 * @brief Node configuration.
 */

#ifndef __ESP_RMAKER_NODE_CONFIG_H__
#define __ESP_RMAKER_NODE_CONFIG_H__

/* Includes *******************************************************/

/* Standard includes. */
#include <stdint.h>
#include <stddef.h>

/* Error includes. */
#include "esp_rmaker_error_types.h"

/* Value includes. */
#include "esp_rmaker_val.h"

/* Handle includes. */
#include "esp_rmaker_handle.h"

/* Public types *******************************************************/

/**
 * @brief ESP RainMaker Neo Node information
 */
typedef struct {
    /** Name of the Node */
    char *name;
    /** Type of the Node */
    char *type;
    /** Firmware Version (Optional). If not set, PROJECT_VER is used as default (recommended)*/
    char *fw_version;
    /** Model (Optional). If not set, PROJECT_NAME is used as default (recommended)*/
    char *model;

    /* Not in use for now */
    // /** Subtype (Optional). */
    // char *subtype;
    // /** An array of digests read from efuse. Should be freed after use*/
    // char **secure_boot_digest;
} esp_rmaker_node_info_t;

/**
 * @brief ESP RainMaker Neo Attribute
 */
struct esp_rmaker_attr {
    /** Name of the Attribute */
    char *name;
    /** Value of the Attribute */
    char *value;
    /** Next attribute */
    struct esp_rmaker_attr *next;
};
/** ESP RainMaker Neo attribute */
typedef struct esp_rmaker_attr esp_rmaker_attr_t;

/**
 * @brief ESP RainMaker Neo tag
 */
struct esp_rmaker_tag {
    /** Name of the tag */
    char *name;
    /** Value of the tag */
    char *value;
    /** Next tag */
    struct esp_rmaker_tag *next;
    /** Flags */
    uint8_t flags; /* esp_rmaker_signal_flags_t */
};
/** ESP RainMaker Neo tag */
typedef struct esp_rmaker_tag esp_rmaker_tag_t;

/** Signal flags */
typedef enum {
    RMAKER_SIGNAL_FLAG_VALUE_CHANGE = (1 << 0),
    RMAKER_SIGNAL_FLAG_VALUE_NOTIFY = (1 << 1),
    RMAKER_SIGNAL_FLAG_VALUE_ALL = RMAKER_SIGNAL_FLAG_VALUE_CHANGE | RMAKER_SIGNAL_FLAG_VALUE_NOTIFY,
} esp_rmaker_signal_flags_t;

/**  ESP RainMaker Neo Node Handle */
typedef esp_rmaker_handle_t esp_rmaker_node_t;

/* Public function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/* --- Node --- */

/**
 * @brief Get a handle to the Node
 *
 * This API returns handle to a node created using esp_rmaker_node_init().
 *
 * @return Node handle on success.
 * @return NULL in case of failure.
 */
const esp_rmaker_node_t *esp_rmaker_get_node(void);

/**
 * @brief Get the node ID (synonymous with AWS Thing Name / MQTT client ID)
 *
 * This API returns the node ID (synonymous with AWS Thing Name / MQTT client ID)
 *
 * @return Node ID on success. If not NULL, the caller must free the returned string using free().
 * @return NULL in case of failure.
 */
char *esp_rmaker_get_node_id(void);

/**
 * @brief Get Node Info
 *
 * Returns pointer to the node info as configured during initialisation.
 *
 * @param[in] node Node handle.
 *
 * @return Pointer to the node info on success.
 * @return NULL in case of failure.
 */
esp_rmaker_node_info_t *esp_rmaker_node_get_info(const esp_rmaker_node_t *node);

/**
 * @brief Populate a freshly initialised node with the supplied info.
 *
 * Allocates ``node->info`` and duplicates each of the four fields
 * (``name`` / ``type`` / ``fw_version`` / ``model``) from ``info``.
 * Reserved tags (`name`, `type`, `fw_version`, `model`) are automatically
 * added/updated to mirror the info.
 *
 * Used for child nodes created by the bridge: the caller passes a stack
 * ::esp_rmaker_node_info_t describing the child. The self node is
 * normally filled via a higher-level internal create API.
 *
 * Fails with ::ESP_RMAKER_INVALID_STATE if the node already has info
 * allocated (e.g., this function has already been called once for that node).
 *
 * @param[in] node Node handle.
 * @param[in] info Source info struct. The struct itself is not retained.
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_node_fill_with_info(const esp_rmaker_node_t *node, const esp_rmaker_node_info_t *info);

/**
 * @brief Report the node configuration to the cloud (self Thing).
 *
 * Builds the self-ctx slice of the node config, hashes it, and publishes
 * ``setNodeConfig`` only if the checksum differs from the value persisted
 * in NVS. For the per-child counterpart see
 * ``esp_rmaker_report_node_config_for_child()``.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_report_node_config(void);


/**
 * @brief Add Node attribute
 *
 * Adds a new attribute as the metadata for the node. For the sake of simplicity,
 * only string values are allowed.
 *
 * @param[in] node Node handle.
 * @param[in] attr_name Name of the attribute.
 * @param[in] val Value for the attribute.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_node_add_attribute(const esp_rmaker_node_t *node, const char *attr_name, const char *val);

/**
 * @brief Add a tag to a node
 *
 * @note Tags are reported to the indexed shadow to facilitate searching.
 * @note If the tag already exists, its value will be overwritten.
 * @note The tag value is not reported to the cloud. If you require simultaneous adding and reporting, use esp_rmaker_node_update_tag().
 *
 * @param[in] node Node handle.
 * @param[in] tag_name Name of the tag. The reserved names `name`, `type`, `fw_version` and
 *                     `model` are rejected; they mirror ::esp_rmaker_node_info_t and are
 *                     maintained by the SDK.
 * @param[in] tag_value Value of the tag.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if the tag name is reserved, or if any argument is NULL.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_node_add_tag(const esp_rmaker_node_t *node, const char *tag_name, const char *tag_value);

/**
 * @brief Update a tag of a node
 *
 * @note Tags are reported to the indexed shadow to facilitate searching.
 * @note The tag value is reported to the cloud. If the tag already exists with the same value,
 *       nothing is reported.
 *
 * @param[in] node Node handle.
 * @param[in] tag_name Name of the tag. The reserved names `name`, `type`, `fw_version` and
 *                     `model` are rejected; they mirror ::esp_rmaker_node_info_t and are
 *                     maintained by the SDK.
 * @param[in] tag_value Value of the tag.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if the tag name is reserved, or if any argument is NULL.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_node_update_tag(const esp_rmaker_node_t *node, const char *tag_name, const char *tag_value);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_NODE_CONFIG_H__ */
