/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_devices.c
 * @brief Public API for devices added to and removed from a running mirror.
 */

#include "esp_rmaker_matter_mirror.h"

#include "rm_mirror_engine.h"

esp_rmaker_error_t esp_rmaker_matter_mirror_add_device(const esp_rmaker_device_t *device)
{
    return rm_mirror_engine_add_device(device, NULL);
}

esp_rmaker_error_t esp_rmaker_matter_mirror_remove_device(const esp_rmaker_device_t *device)
{
    return rm_mirror_engine_remove_device(device, NULL);
}

esp_rmaker_error_t esp_rmaker_matter_mirror_set_reachable(const esp_rmaker_device_t *device, bool reachable)
{
    return rm_mirror_engine_set_reachable(device, reachable);
}
