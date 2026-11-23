/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_hooks.h
 * @brief What the rest of the SDK tells the controller about.
 *
 * Kept free of the controller's own types, so a caller in another layer does not pull them in.
 */

#ifndef __CONTROLLER_HOOKS_H__
#define __CONTROLLER_HOOKS_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Report that the cloud answered getGroupInfo for this node.
 *
 * The controller's inventory is keyed on the group, so a migration invalidates every shadow name
 * it derived. Queues a re-sync that rebuilds the inventory from the new group and drops the
 * filters the old group needed. A re-sync that fails is retried on the next getGroupInfo, so a
 * migration that lands while MQTT is down still recovers.
 *
 * @note Never blocks. Call from any task. Does nothing until the application has synced once.
 *
 * @param[in] migrated True when the group info changed from the one the node held.
 */
void controller_group_notify_group_info(bool migrated);

#ifdef __cplusplus
}
#endif

#endif /* __CONTROLLER_HOOKS_H__ */
