/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bridge_child_nvs.h
 * @brief Per-child NVS persistence for bridged children.
 *
 * One entry per child under ``RMAKER_NVS_BRIDGE_CHILDREN_NAMESPACE``,
 * keyed by the child handle's ``bridge_local_id`` truncated to the NVS
 * key length cap. The entry is a versioned packed binary struct; reads
 * that see a wrong on-disk version or unexpected size erase the entry
 * (invalidated -> treated as absent).
 *
 * Only present when ``CONFIG_RMNG_BRIDGE_ENABLED``.
 */

#ifndef __BRIDGE_CHILD_NVS_H__
#define __BRIDGE_CHILD_NVS_H__

#include "sdkconfig.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "constants/identity.h"
#include "esp_rmaker_bridge.h"
#include "esp_rmaker_error_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief On-disk format version. Bump whenever the layout of
 *        ::bridge_child_nvs_record_t changes; existing entries on the
 *        wrong version are erased on next read.
 */
#define BRIDGE_CHILD_NVS_VERSION   2u

/**
 * @brief Length of the SHA-256 node-config / node-tags checksum.
 */
#define BRIDGE_CHILD_NCFG_CHECKSUM_LEN 32

/**
 * @brief Per-child persistent record.
 *
 * Fields are unset-sentinel where reasonable so a freshly initialised
 * record (all zeros except ``nvs_version``) reflects "no data yet".
 */
typedef struct __attribute__((packed))
{
    uint8_t  nvs_version;                                          /**< Always first; equals ::BRIDGE_CHILD_NVS_VERSION on a current entry. */
    uint8_t  ncfg_checksum_set;                                    /**< 0 if ncfg_checksum is unset, 1 if populated. */
    uint8_t  tags_checksum_set;                                    /**< 0 if tags_checksum is unset, 1 if populated. */
    uint8_t  reserved;
    int32_t  sched_ver;                                            /**< Cached schedule version, -1 if unset. */
    int32_t  trigger_ver;                                          /**< Cached trigger version, -1 if unset. */
    uint8_t  ncfg_checksum[BRIDGE_CHILD_NCFG_CHECKSUM_LEN];        /**< SHA-256 of last accepted node config; also the reported ncfg_ver change-token. */
    uint8_t  tags_checksum[BRIDGE_CHILD_NCFG_CHECKSUM_LEN];        /**< SHA-256 of last published node tags. */
} bridge_child_nvs_record_t;

/**
 * @brief Initialise the per-child NVS subsystem (mutex creation). Idempotent.
 */
esp_rmaker_error_t bridge_child_nvs_init(void);

/**
 * @brief Load the record for the given child handle.
 *
 * On a stored entry whose on-disk version or size does not match the
 * current build, the entry is erased and ``ESP_RMAKER_NOT_FOUND`` is
 * returned (treated as absent).
 *
 * @param[in]  child Child handle (must be non-NULL with a populated bridge_local_id).
 * @param[out] out   Populated on success.
 * @return ESP_RMAKER_OK, ESP_RMAKER_NOT_FOUND, or another error.
 */
esp_rmaker_error_t bridge_child_nvs_load(esp_rmaker_bridge_child_handle_t child, bridge_child_nvs_record_t *out);

/**
 * @brief Persist the given record for the given child handle.
 *
 * Writes the on-disk version field unconditionally before writing.
 * Caller is responsible for atomicity if multiple fields change at once;
 * use ::bridge_child_nvs_load to read-modify-write under the module mutex.
 */
esp_rmaker_error_t bridge_child_nvs_store(esp_rmaker_bridge_child_handle_t child, const bridge_child_nvs_record_t *in);

/**
 * @brief Erase the per-child entry. Used on remove_child and on
 *        version/size mismatch detection in ::bridge_child_nvs_load.
 */
esp_rmaker_error_t bridge_child_nvs_erase(esp_rmaker_bridge_child_handle_t child);

/* Atomic RMW helpers ***********************************************************/

/**
 * @brief Atomically update ``sched_ver`` for a child. Creates the entry
 *        if absent (all other fields left as their unset sentinels).
 */
esp_rmaker_error_t bridge_child_nvs_set_sched_ver(esp_rmaker_bridge_child_handle_t child, int32_t sched_ver);

/**
 * @brief Atomically update ``trigger_ver`` for a child.
 */
esp_rmaker_error_t bridge_child_nvs_set_trigger_ver(esp_rmaker_bridge_child_handle_t child, int32_t trigger_ver);

/**
 * @brief Atomically update ``ncfg_checksum`` for a child. The checksum
 *        doubles as the reported ncfg_ver change-token.
 */
esp_rmaker_error_t bridge_child_nvs_set_node_config(esp_rmaker_bridge_child_handle_t child,
        const uint8_t checksum[BRIDGE_CHILD_NCFG_CHECKSUM_LEN]);

/**
 * @brief Atomically update ``tags_checksum`` for a child.
 */
esp_rmaker_error_t bridge_child_nvs_set_node_tags(esp_rmaker_bridge_child_handle_t child,
        const uint8_t checksum[BRIDGE_CHILD_NCFG_CHECKSUM_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

#endif /* __BRIDGE_CHILD_NVS_H__ */
