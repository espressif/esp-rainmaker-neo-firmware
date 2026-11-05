/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_mqtt_core_test_hooks.h
 * @brief Internal hooks of the coreMQTT implementation for the OSAL MQTT unit tests
 */

#ifndef OSAL_MQTT_CORE_TEST_HOOKS_H
#define OSAL_MQTT_CORE_TEST_HOOKS_H

#include <stdint.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_OSAL_MQTT_TEST_HOOKS
/**
 * @brief Number of agent command-loop entries since the last init().
 */
uint32_t osal_mqtt_core_test_get_command_loop_entries( void );
#endif /* CONFIG_OSAL_MQTT_TEST_HOOKS */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSAL_MQTT_CORE_TEST_HOOKS_H */
