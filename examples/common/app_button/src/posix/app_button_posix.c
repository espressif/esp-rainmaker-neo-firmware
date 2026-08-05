/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_button_posix.c
 * @brief POSIX stub for the app_button interface. There is no physical button
 *        on the host, so init succeeds and the callbacks are simply never
 *        invoked. This lets the portable example configure a button
 *        unconditionally.
 */

/* Declarations */
#include "app_button.h"

/* Standard includes */
#include <stddef.h>

osal_err_t app_button_init(app_button_config_t *config)
{
    if (config == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    return OSAL_ERR_OK;
}
