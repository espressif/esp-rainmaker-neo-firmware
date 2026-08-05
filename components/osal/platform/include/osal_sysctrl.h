/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_sysctrl.h
 * @brief Platform common system control interface for system control events (e.g., factory reset, reboot)
 */

#ifndef OSAL_SYSCONTROL_H
#define OSAL_SYSCONTROL_H

#include "osal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reboot the system
 * @return OSAL_ERR_OK on success, otherwise error code.
 */
osal_err_t osal_sysctrl_reboot(void);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_SYSCONTROL_H */
