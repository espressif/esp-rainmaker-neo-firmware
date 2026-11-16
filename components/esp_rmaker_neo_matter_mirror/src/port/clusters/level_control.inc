/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* LevelControl (0x0008). Included by rm_mirror_port_esp_matter.cpp. */

/* This file is included only when the mapping needs the cluster, so esp-matter must
 * have it compiled in. */
#if !defined(CONFIG_SUPPORT_LEVEL_CONTROL_CLUSTER)
#error "Matter mirror: the mapping needs LevelControl (0x0008), but CONFIG_SUPPORT_LEVEL_CONTROL_CLUSTER is off."
#endif

#define RM_MIRROR_PORT_HAS_CLUSTER_0008 1
#define RM_CLUSTER_LEVEL_CONTROL 0x0008u

/* CHIP compiles LevelControl's coupled-color-temp path whatever the build holds, but the callback
 * it lands on lives in the ColorControl server. Unreachable: the caller checks the endpoint first. */
#if !CONFIG_SUPPORT_COLOR_CONTROL_CLUSTER
void emberAfPluginLevelControlCoupledColorTempChangeCallback(chip::EndpointId endpoint)
{
    (void)endpoint;
}
#endif

static cluster_t *__cluster_create_level_control(endpoint_t *endpoint, uint32_t feature_map)
{
    (void)feature_map;
    cluster::level_control::config_t config;
    cluster_t *cluster = cluster::level_control::create(endpoint, &config, CLUSTER_FLAG_SERVER);
    if (cluster) {
        cluster::level_control::feature::on_off::add(cluster);
        cluster::level_control::feature::lighting::config_t lighting_config;
        lighting_config.start_up_current_level = nullable<uint8_t>();
        cluster::level_control::feature::lighting::add(cluster, &lighting_config);
    }
    return cluster;
}
