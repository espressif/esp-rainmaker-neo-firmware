/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_mqtt_impl.h
 * @brief ESP RainMaker Neo MQTT implementation.
 */

#ifndef __ESP_RMAKER_MQTT_IMPL_H__
#define __ESP_RMAKER_MQTT_IMPL_H__

/* MQTT includes */
#include "osal_mqtt_prototypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Global MQTT implementation.
 *
 * This is put in a common header file for sharing across all components that depend on this component.
 * The main RainMaker Neo component is expected to set this up, after which is can be used by other dependents.
 */
extern osal_mqtt_impl_t esp_rmaker_mqtt_impl;

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_MQTT_IMPL_H__ */
