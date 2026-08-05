/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file sysinfo.h
 * @brief System information for the host control.
 */

#ifndef __ESP_RMAKER_HOST_CTRL_SYSINFO_H__
#define __ESP_RMAKER_HOST_CTRL_SYSINFO_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the target type of the current instance.
 * @return The target type of the current instance.
 */
char *esp_rmaker_host_ctrl_get_target_type(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_HOST_CTRL_SYSINFO_H__ */
