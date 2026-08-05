/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file netstatus_posix.c
 * @brief POSIX implementation of the network-connectivity trapdoor.
 *
 * POSIX hosts have no managed provisioning/connectivity - the network is already
 * up - so the trapdoor is a no-op: arm does nothing and trap returns immediately,
 * never blocking on a connectivity event that will not be signalled.
 */

#include "osal_netstatus.h"

int osal_netstatus_arm(void)
{
    return 0;
}

int osal_netstatus_trap(void)
{
    return 0;
}
