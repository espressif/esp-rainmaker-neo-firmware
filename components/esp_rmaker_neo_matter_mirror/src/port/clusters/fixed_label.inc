/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* FixedLabel (0x0040). Included by rm_mirror_port_esp_matter.cpp. */

/* This file is included only when the mapping needs the cluster, so esp-matter must
 * have it compiled in. */
#if !defined(CONFIG_SUPPORT_FIXED_LABEL_CLUSTER)
#error "Matter mirror: the mapping needs FixedLabel (0x0040), but CONFIG_SUPPORT_FIXED_LABEL_CLUSTER is off."
#endif

#define RM_MIRROR_PORT_HAS_CLUSTER_0040 1
#define RM_CLUSTER_FIXED_LABEL 0x0040u

static cluster_t *__cluster_create_fixed_label(endpoint_t *endpoint, uint32_t feature_map)
{
    (void)feature_map;
    cluster::fixed_label::config_t config;
    return cluster::fixed_label::create(endpoint, &config, CLUSTER_FLAG_SERVER);
}
