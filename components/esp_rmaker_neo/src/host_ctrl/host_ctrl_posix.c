/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file host_ctrl_posix.c
 * @brief POSIX implementation of the host control.
 */

#include "esp_rmaker_host_ctrl.h"
#include "sysinfo.h"
#include <stdlib.h>
#include <signal.h>

char *esp_rmaker_host_ctrl_get_target_type(void)
{
    return "posix";
}

void esp_rmaker_host_ctrl_kill(void)
{
    // Kill the process
    exit(SIGKILL);
}
