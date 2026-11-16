/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Groups (0x0004). Included by rm_mirror_port_esp_matter.cpp. */

/* This file is included only when the mapping needs the cluster, so esp-matter must
 * have it compiled in. */
#if !defined(CONFIG_SUPPORT_GROUPS_CLUSTER)
#error "Matter mirror: the mapping needs Groups (0x0004), but CONFIG_SUPPORT_GROUPS_CLUSTER is off."
#endif

#define RM_MIRROR_PORT_HAS_CLUSTER_0004 1
#define RM_CLUSTER_GROUPS 0x0004u

static cluster_t *__cluster_create_groups(endpoint_t *endpoint, uint32_t feature_map)
{
    (void)feature_map;
    cluster::groups::config_t config;
    return cluster::groups::create(endpoint, &config, CLUSTER_FLAG_SERVER);
}
