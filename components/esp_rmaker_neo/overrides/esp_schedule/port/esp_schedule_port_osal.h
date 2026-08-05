/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_schedule_port_osal.h
 * @brief osal-backed port for the esp_schedule component.
 *
 * esp_schedule reaches timers, wall-clock time, the heap and the log only
 * through the function pointers in ``esp_schedule.h``. This port routes
 * them at osal, which is what lets firmware tests drive schedule timers from the
 * virtual scheduler instead of the wall clock - the same reason the previous
 * link-time ``--wrap`` layer existed, but as a supported extension point.
 *
 * Used on ESP-IDF as well as POSIX: passing this port to
 * ``esp_schedule_init_with_config`` in place of ``esp_schedule_init`` also keeps
 * the component's built-in ESP-IDF port (and its FreeRTOS timer, nvs_flash and
 * esp_log dependencies) out of the link entirely.
 *
 * Storage operations are deliberately left NULL: the schedule service persists
 * the details JSON itself, so esp_schedule runs with no persistence of its own.
 */

#ifndef __ESP_SCHEDULE_PORT_OSAL_H__
#define __ESP_SCHEDULE_PORT_OSAL_H__

#include "esp_schedule.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The osal-backed port configuration.
 *
 * Pass to ``esp_schedule_init_with_config``. Storage ops are all NULL, so
 * persistence is unavailable whatever ``enable_nvs`` says.
 *
 * @return Pointer to a static config; valid for the lifetime of the program.
 */
const esp_schedule_port_config_t *esp_schedule_port_osal_get(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_SCHEDULE_PORT_OSAL_H__ */
