/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file prov_console_cmd.h
 * @brief Provisioning console command, built only when APP_NETWORK_PROV_CONSOLE_CMD is enabled.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the provisioning console command for the configured network protocol.
 *
 * @note Registers `wifi-prov` on Wi-Fi builds and `thread-prov` on Thread builds. Call after the
 *       console has been initialised.
 */
void app_network_register_prov_command(void);

#ifdef __cplusplus
}
#endif
