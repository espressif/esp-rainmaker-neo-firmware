/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file migration_test_stubs_apple.c
 * @brief Stubs for the migration tests on macOS, which has no linker --wrap.
 */

#include "osal_err.h"

extern volatile int g_test_migration_rollback_count;

osal_err_t osal_ota_mark_app_invalid_rollback_and_reboot(void)
{
    g_test_migration_rollback_count++;
    return OSAL_ERR_OK;
}
