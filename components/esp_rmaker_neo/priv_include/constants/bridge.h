/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bridge.h
 * @brief Bridge-protocol size constants (request_id, child_suffix,
 *        bridge_local_id).
 *
 * Centralised so the bridge-events publisher, the bridgeAck dispatcher
 * (request_id-correlated pending table), and the public API surface all
 * agree on lengths. Bridge identity overlaps with generic Thing-name
 * identity but adds its own protocol-level identifiers.
 */

#ifndef __CONSTANTS_BRIDGE_H__
#define __CONSTANTS_BRIDGE_H__

#include <assert.h>

/* Request ID correlator *********************************************************/

/**
 * @brief Length of the bridge-protocol ``request_id`` correlator
 *        (excluding NUL), in hex characters.
 *
 * Used in addChild / removeChild outbound payloads and matched against
 * the ``bridgeAck`` payload's ``request_id`` field on the inbound path.
 * Must be even because the generator fills half as many random bytes
 * and writes one hex pair per byte.
 */
#define RMAKER_BRIDGE_REQUEST_ID_LEN         8

/**
 * @brief Buffer size for a NUL-terminated ``request_id``.
 */
#define RMAKER_BRIDGE_REQUEST_ID_BUF_SIZE    (RMAKER_BRIDGE_REQUEST_ID_LEN + 1)

_Static_assert((RMAKER_BRIDGE_REQUEST_ID_LEN % 2) == 0,
               "RMAKER_BRIDGE_REQUEST_ID_LEN must be even (hex pairs)");

/* Child suffix ******************************************************************/

/**
 * @brief Maximum length of the caller-supplied ``child_suffix``
 *        (excluding NUL). Matches the public regex ``[A-Za-z0-9_]{1,32}``
 *        documented on ::esp_rmaker_bridge_add_child.
 */
#define RMAKER_BRIDGE_CHILD_SUFFIX_LEN_MAX   32

/**
 * @brief Buffer size for a NUL-terminated child suffix.
 */
#define RMAKER_BRIDGE_CHILD_SUFFIX_BUF_SIZE  (RMAKER_BRIDGE_CHILD_SUFFIX_LEN_MAX + 1)

/* Bridge-local identifier *******************************************************/

/**
 * @brief Maximum length of the bridge-vendor ``bridge_local_id``
 *        (excluding NUL). Sized generously for vendor schemes such as
 *        Zigbee EUI-64 (16 hex chars) plus a vendor prefix.
 */
#define RMAKER_BRIDGE_LOCAL_ID_LEN_MAX       64

/**
 * @brief Buffer size for a NUL-terminated ``bridge_local_id``.
 */
#define RMAKER_BRIDGE_LOCAL_ID_BUF_SIZE      (RMAKER_BRIDGE_LOCAL_ID_LEN_MAX + 1)

#endif /* __CONSTANTS_BRIDGE_H__ */
