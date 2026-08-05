/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_netstatus.h
 * @brief One-shot network-connectivity trapdoor.
 *
 * Lets a caller block until the device first obtains network connectivity (an IP
 * address). Modelled as a one-shot trapdoor with two steps:
 *
 * @code
 *     osal_netstatus_arm();    // as early as possible at boot
 *     ...
 *     osal_netstatus_trap();   // block until connected, then tear down
 * @endcode
 *
 * The latch only ever flips from "not connected" to "connected"; later
 * disconnects are ignored (steady-state reconnection is the transport's job).
 *
 * This is a platform-agnostic API. On platforms without managed connectivity -
 * e.g. POSIX hosts, where the network is already up - arm is a no-op and trap
 * returns immediately, so callers never block on connectivity that will not be
 * signalled.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arm the connectivity trapdoor.
 *
 * Registers the handlers that latch "connected" the first time an IP address is
 * obtained. Call as early as possible at boot - before the network stack can
 * connect - so the first connectivity event is never missed. Idempotent.
 *
 * @return 0 on success, non-zero on failure.
 */
int osal_netstatus_arm(void);

/**
 * @brief Wait for connectivity, then disarm.
 *
 * Blocks indefinitely until the trapdoor has latched (an IP address has been
 * obtained, or the device was already connected when armed), then unregisters the
 * handlers and releases resources. Must be paired with a prior osal_netstatus_arm().
 *
 * @return 0 once connected and disarmed, non-zero on failure.
 */
int osal_netstatus_trap(void);

#ifdef __cplusplus
}
#endif
