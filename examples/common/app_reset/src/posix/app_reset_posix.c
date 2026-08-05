/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_reset_posix.c
 * @brief POSIX hold-to-reset stub. There is no physical button on the host, so there is
 *        never a hold in progress. app_reset_button_register() is ESP-only and therefore
 *        absent here; see app_reset.h.
 */

/* Declarations */
#include "app_reset.h"

bool app_reset_hold_in_progress(void)
{
    return false;
}
