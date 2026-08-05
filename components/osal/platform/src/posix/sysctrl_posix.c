/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file sysctrl_posix.c
 * @brief POSIX system control implementation.
 */

/* Includes **************************************************************/

/* Declarations */
#include "osal_sysctrl.h"

/* Exit codes */
#include "posix_exit_codes.h"

/* Logging */
#include "osal_log.h"

/* Standard includes */
#include <stdlib.h>

/* Constants **************************************************************/

/** Tag for logging */
static const char *TAG = "osal_sysctrl";

/* Function definitions **********************************************************/

osal_err_t osal_sysctrl_reboot(void)
{
    /* Exit the process with reboot code */
    OSAL_LOGW(TAG, "POSIX system is exiting with reboot code. Please start the process again manually.");
    exit(POSIX_EXIT_REBOOT);
    return OSAL_ERR_OK;
}
