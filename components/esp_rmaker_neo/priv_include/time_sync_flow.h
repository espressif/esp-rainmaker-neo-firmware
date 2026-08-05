/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file time_sync_flow.h
 * @brief Selects the SDK time-synchronization flow at compile time.
 *
 * Two mutually exclusive flows:
 *
 * - **Synchronous** (``CONFIG_MBEDTLS_HAVE_TIME_DATE`` set): mbedTLS
 *   validates the server cert's notBefore/notAfter, so a valid wall clock
 *   is a hard prerequisite for the MQTT TLS handshake. ``start_task``
 *   blocks until the clock syncs; schedules then arm inline because time
 *   is already valid.
 *
 * - **Decoupled** (``CONFIG_MBEDTLS_HAVE_TIME_DATE`` unset): never block.
 *   MQTT and all non-wall-clock work proceed immediately; a flat-cadence
 *   poll observes sync later and arms schedules then, while wall-clock
 *   consumers guard on ``osal_timesync_is_synced()`` meanwhile.
 *
 * ``TIME_SYNC_DECOUPLED_FLOW`` is 1 in the decoupled flow and 0 in the
 * synchronous flow. All decoupled-only machinery (the poll retry context,
 * its work fn, the deferred schedule-arming state) is compiled out in the
 * synchronous flow to save flash/RAM. Use only with ``#if`` - never
 * ``#ifdef`` (it is always defined) and never in C expression context.
 */

#ifndef __TIME_SYNC_FLOW_H__
#define __TIME_SYNC_FLOW_H__

#include "sdkconfig.h"

#define TIME_SYNC_DECOUPLED_FLOW (!CONFIG_MBEDTLS_HAVE_TIME_DATE)

#endif /* __TIME_SYNC_FLOW_H__ */
