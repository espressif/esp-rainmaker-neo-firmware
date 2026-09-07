/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* ColorControl (0x0300). Included by rm_mirror_port_esp_matter.cpp.
 *
 * Accumulating: FeatureMap bits arrive across calls (CT from the cct param, HS from
 * hue/saturation, XY from the composite), so this merges into an existing
 * cluster instead of returning early. */

/* This file is included only when the mapping needs the cluster, so esp-matter must
 * have it compiled in. */
#if !defined(CONFIG_SUPPORT_COLOR_CONTROL_CLUSTER)
#error "Matter mirror: the mapping needs ColorControl (0x0300), but CONFIG_SUPPORT_COLOR_CONTROL_CLUSTER is off."
#endif

#define RM_MIRROR_PORT_HAS_CLUSTER_0300 1
#define RM_CLUSTER_COLOR_CONTROL 0x0300u

/* ColorControl feature lead attributes, probed to keep the feature merge idempotent */
#define RM_ATTR_CURRENT_HUE       0x0000u
#define RM_ATTR_CURRENT_X         0x0003u
#define RM_ATTR_COLOR_TEMP_MIREDS 0x0007u

static inline bool __color_feature(uint32_t feature_map, chip::app::Clusters::ColorControl::Feature bit)
{
    return (feature_map & chip::to_underlying(bit)) != 0;
}

static cluster_t *__cluster_create_color_control(endpoint_t *endpoint, uint32_t feature_map)
{
    using chip::app::Clusters::ColorControl::Feature;
    cluster_t *cluster = cluster::get(endpoint, RM_CLUSTER_COLOR_CONTROL);
    if (!cluster) {
        cluster::color_control::config_t config;
        /* Initial ColorMode/EnhancedColorMode from the first requester; with a
         * light-mode param the engine seeds the real value right after, and
         * CHIP's server maintains it on commands at runtime. */
        uint8_t mode = __color_feature(feature_map, Feature::kColorTemperature) ? 2 : 0;
        config.color_mode = mode;
        config.enhanced_color_mode = mode;
        cluster = cluster::color_control::create(endpoint, &config, CLUSTER_FLAG_SERVER);
        if (cluster) {
            /* Transition progress/abort, added by device-type helpers on native
             * lights: RemainingTime is CHIP-server-managed (volatile, unmapped -
             * inbound updates are ignored by the binding lookup); StopMoveStep
             * halts an in-flight Move/Step. */
            cluster::color_control::attribute::create_remaining_time(cluster, 0);
            if (!command::get(cluster, chip::app::Clusters::ColorControl::Commands::StopMoveStep::Id,
                              COMMAND_FLAG_ACCEPTED)) {
                cluster::color_control::command::create_stop_move_step(cluster);
            }
        }
    }
    if (!cluster) {
        return NULL;
    }

    if (__color_feature(feature_map, Feature::kColorTemperature) && !attribute::get(cluster, RM_ATTR_COLOR_TEMP_MIREDS)) {
        cluster::color_control::feature::color_temperature::config_t ct_config;
        ct_config.start_up_color_temperature_mireds = nullable<uint16_t>();
        cluster::color_control::feature::color_temperature::add(cluster, &ct_config);
    }
    if (__color_feature(feature_map, Feature::kHueAndSaturation) && !attribute::get(cluster, RM_ATTR_CURRENT_HUE)) {
        cluster::color_control::feature::hue_saturation::config_t hs_config;
        cluster::color_control::feature::hue_saturation::add(cluster, &hs_config);
    }
    if (__color_feature(feature_map, Feature::kXy) && !attribute::get(cluster, RM_ATTR_CURRENT_X)) {
        cluster::color_control::feature::xy::config_t xy_config;
        cluster::color_control::feature::xy::add(cluster, &xy_config);
    }
    return cluster;
}
