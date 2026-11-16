/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Identify (0x0003). Included by rm_mirror_port_esp_matter.cpp. */

/* This file is included only when the mapping needs the cluster, so esp-matter must
 * have it compiled in. */
#if !defined(CONFIG_SUPPORT_IDENTIFY_CLUSTER)
#error "Matter mirror: the mapping needs Identify (0x0003), but CONFIG_SUPPORT_IDENTIFY_CLUSTER is off."
#endif

#define RM_MIRROR_PORT_HAS_CLUSTER_0003 1
#define RM_CLUSTER_IDENTIFY 0x0003u

static cluster_t *__cluster_create_identify(endpoint_t *endpoint, uint32_t feature_map)
{
    (void)feature_map;
    cluster::identify::config_t config;
    config.identify_type = 2; /* VisibleIndicator */
    cluster_t *cluster = cluster::identify::create(endpoint, &config, CLUSTER_FLAG_SERVER);
    if (cluster && !command::get(cluster, chip::app::Clusters::Identify::Commands::TriggerEffect::Id,
                                 COMMAND_FLAG_ACCEPTED)) {
        /* Added by device-type helpers on native lights; legacy identify::create registers Identify alone */
        cluster::identify::command::create_trigger_effect(cluster);
    }
    return cluster;
}
