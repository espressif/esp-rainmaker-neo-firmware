/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file nvs.h
 * @brief NVS constants
 */

#ifndef __CONSTANTS_NVS_H__
#define __CONSTANTS_NVS_H__

/* Common constants **************************************************************/

#include "constants/esp_rmaker_nvs_common.h"

/* Node configuration **************************************************************/

#define RMAKER_NVS_CHECKSUM_NAMESPACE           "chksum"
#define RMAKER_NVS_CHECKSUM_KEY_NODE_CONFIG     "node_cfg"
#define RMAKER_NVS_CHECKSUM_KEY_NODE_TAGS       "node_tags"

/* Local configuration **************************************************************/

#define RMAKER_NVS_LOCAL_CONFIG_NAMESPACE              "local_config"
#define RMAKER_NVS_LOCAL_CONFIG_KEY_GROUP_INFO_STRING  "grp_info"
#define RMAKER_NVS_LOCAL_CONFIG_KEY_ALEXA_EN           "alexa_en"
#define RMAKER_NVS_LOCAL_CONFIG_KEY_GVA_EN             "gva_en"
#define RMAKER_NVS_LOCAL_CONFIG_KEY_ST_EN              "st_en"
#define RMAKER_NVS_LOCAL_CONFIG_KEY_SCHED_VER          "sched_ver"
#define RMAKER_NVS_LOCAL_CONFIG_KEY_SCHED_DETAILS      "sched_det"
#define RMAKER_NVS_LOCAL_CONFIG_KEY_TRIGGER_VER        "trg_ver"
#define RMAKER_NVS_LOCAL_CONFIG_KEY_TRIGGER_DETAILS    "trg_det"

/* Bridge children **************************************************************/

/**
 * @brief NVS namespace for the bridge children registry.
 *
 * Each entry under this namespace is keyed by the child's
 * ``bridge_local_id`` and stores a compact record (thing name and group
 * info string). Cleared only on explicit remove_child or factory reset -
 * the bridge's own group migration does NOT touch this namespace.
 */
#define RMAKER_NVS_BRIDGE_CHILDREN_NAMESPACE           "rmng_bridge"

/**
 * @brief Namespace for per-child automation trigger details (variable-length
 * JSON blob). Separate from the packed per-child record namespace because the
 * trigger JSON is unbounded. Keyed by the child's SHA-256-derived NVS key.
 * (15 chars - at the NVS namespace-name limit.)
 */
#define RMAKER_NVS_BRIDGE_TRIGGERS_NAMESPACE           "bridge_triggers"

/**
 * @brief Namespace for per-child schedule details (variable-length JSON
 * string). Separate from the packed per-child record namespace because the
 * schedule JSON is unbounded, and separate from the trigger namespace so the
 * two services can be erased independently. Keyed by the child's
 * SHA-256-derived NVS key.
 *
 * The schedule service owns its own persistence (rather than relying on
 * esp_schedule's NVS) because esp_schedule stores only the trigger config,
 * not the action payload a fired schedule has to apply.
 */
#define RMAKER_NVS_BRIDGE_SCHEDS_NAMESPACE             "bridge_scheds"

/**
 * @brief Maximum length of an NVS key (excluding NUL). Matches the
 * ESP-IDF NVS limit.
 */
#define RMAKER_NVS_KEY_LEN_MAX                         15

#endif /* __CONSTANTS_NVS_H__ */
