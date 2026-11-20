/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_timesync_test_hooks.h
 * @brief Internal hooks of the timesync implementation for the unit tests
 */

#ifndef OSAL_TIMESYNC_TEST_HOOKS_H
#define OSAL_TIMESYNC_TEST_HOOKS_H

#include <stdbool.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_OSAL_TIMESYNC_TEST_HOOKS
/**
 * @brief Set the SNTP state that osal_timesync_sntp_has_synced() reports.
 */
void osal_timesync_test_set_sntp_synced(bool synced);
#endif /* CONFIG_OSAL_TIMESYNC_TEST_HOOKS */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OSAL_TIMESYNC_TEST_HOOKS_H */
