/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the interactive UART console and register the controller commands:
 *        group, standby, sync, nodes, state, grouppower, set, groupset, watch, raw.
 */
void controller_cli_init(void);

#ifdef __cplusplus
}
#endif
