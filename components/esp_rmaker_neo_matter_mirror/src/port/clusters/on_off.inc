/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* OnOff (0x0006). Included by rm_mirror_port_esp_matter.cpp. */

/* This file is included only when the mapping needs the cluster, so esp-matter must
 * have it compiled in. */
#if !defined(CONFIG_SUPPORT_ON_OFF_CLUSTER)
#error "Matter mirror: the mapping needs OnOff (0x0006), but CONFIG_SUPPORT_ON_OFF_CLUSTER is off."
#endif

#define RM_MIRROR_PORT_HAS_CLUSTER_0006 1
#define RM_CLUSTER_ON_OFF 0x0006u

/* OnOff's level-control effect callback lives in the LevelControl server.
 * Unreachable: the caller checks the endpoint for LevelControl first. */
#if !CONFIG_SUPPORT_LEVEL_CONTROL_CLUSTER
void emberAfOnOffClusterLevelControlEffectCallback(chip::EndpointId endpoint, bool newValue)
{
    (void)endpoint;
    (void)newValue;
}
#endif

static cluster_t *__cluster_create_on_off(endpoint_t *endpoint, uint32_t feature_map)
{
    (void)feature_map;
    cluster::on_off::config_t config;
    cluster_t *cluster = cluster::on_off::create(endpoint, &config, CLUSTER_FLAG_SERVER);
    if (cluster) {
        /* On and Toggle are mandatory, but the legacy data model creates Off alone and leaves
         * the rest to the caller - without them the cluster answers UNSUPPORTED_COMMAND, to
         * controllers and to the mirror's own write_as invokes alike. Skipped when the data
         * model already registered them. */
        if (!command::get(cluster, chip::app::Clusters::OnOff::Commands::On::Id, COMMAND_FLAG_ACCEPTED)) {
            cluster::on_off::command::create_on(cluster);
        }
        if (!command::get(cluster, chip::app::Clusters::OnOff::Commands::Toggle::Id, COMMAND_FLAG_ACCEPTED)) {
            cluster::on_off::command::create_toggle(cluster);
        }
        /* Lighting feature is mandatory for light device types */
        cluster::on_off::feature::lighting::config_t lighting_config;
        lighting_config.start_up_on_off = nullable<uint8_t>();
        cluster::on_off::feature::lighting::add(cluster, &lighting_config);
    }
    return cluster;
}
