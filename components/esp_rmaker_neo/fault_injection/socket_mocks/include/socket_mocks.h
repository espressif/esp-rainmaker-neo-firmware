/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file socket_mocks.h
 * @brief Socket mocks for the network.
 */

#ifndef __SOCKET_MOCKS_H__
#define __SOCKET_MOCKS_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Force all socket operations (connect, send, recv) to fail.
 * This is done by caching the original socket implementation and installing a stub socket implementation that always returns failure.
 * @note This is a global function and will affect all sockets in the system.
 *
 * @param[in] enable_fault Whether to enable the fault.
 */
void socket_mock_force_failure(bool enable_fault);

#ifdef __cplusplus
}
#endif

#endif /* __SOCKET_MOCKS_H__ */
