/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_event_group.h
 * @brief Event group primitives (wait/set/clear of event bits).
 */

#ifndef __OSAL_EVENT_GROUP_H__
#define __OSAL_EVENT_GROUP_H__

#include <stdint.h>
#include <stdbool.h>

#include "osal_err.h"
#include "osal_task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *osal_event_group_handle_t;
typedef uint32_t osal_event_group_bits_t;

/**
 * @brief Creates a new event group.
 *
 * @return A handle to the created event group, or NULL if the event group could not be created.
 */
osal_event_group_handle_t osal_event_group_create(void);

/**
 * @brief Deletes an event group.
 *
 * @param[in] event_group A handle to the event group to be deleted.
 */
void osal_event_group_delete(osal_event_group_handle_t event_group);

/**
 * @brief Wait for a combination of bits to be set in an event group.
 *
 * @param[in] event_group The event group in which to wait for the bits.
 * @param[in] bits_to_wait_for The bits to wait for.
 * @param[in] clear_on_exit If true, any bits that were set in the event group that are also set in bits_to_wait_for will be cleared before the function returns.
 * @param[in] wait_for_all_bits If true, the function will wait for all bits in bits_to_wait_for to be set. Otherwise, the function will return when any of the bits in bits_to_wait_for are set.
 * @param[in] ticks_to_wait The time in ticks to wait for the bits to be set.
 *
 * @return The value of the event group bits at the time an exit condition is met (e.g., timeout, some/all bits set).
 */
osal_event_group_bits_t osal_event_group_wait_bits(
    osal_event_group_handle_t event_group,
    const osal_event_group_bits_t bits_to_wait_for,
    const bool clear_on_exit,
    const bool wait_for_all_bits,
    osal_tick_type_t ticks_to_wait
);

/**
 * @brief Set bits in an event group.
 *
 * @param[in] event_group The event group in which to set the bits.
 * @param[in] bits_to_set The bits to set.
 *
 * @return The value of the event group bits after the function returns.
 */
osal_event_group_bits_t osal_event_group_set_bits(
    osal_event_group_handle_t event_group,
    const osal_event_group_bits_t bits_to_set
);

/**
 * @brief Clear bits in an event group.
 *
 * @param[in] event_group The event group in which to clear the bits.
 * @param[in] bits_to_clear The bits to clear.
 *
 * @return The value of the event group bits before the specified bits were cleared.
 */
osal_event_group_bits_t osal_event_group_clear_bits(
    osal_event_group_handle_t event_group,
    const osal_event_group_bits_t bits_to_clear
);

/**
 * @brief Get the current value of the event group bits.
 *
 * @param[in] event_group The event group to query.
 *
 * @return The current value of the event group bits.
 */
osal_event_group_bits_t osal_event_group_get_bits(
    osal_event_group_handle_t event_group
);

/**
 * @brief Atomically set bits in an event group and wait for a combination of bits to be set, then clear the bits that were set.
 *
 * @param[in] event_group The event group in which to sync.
 * @param[in] bits_to_set The bits to set.
 * @param[in] bits_to_wait_for The bits to wait for.
 * @param[in] ticks_to_wait The time in ticks to wait for the bits to be set.
 *
 * @return The value of the event group bits at the time an exit condition is met (e.g., timeout, all bits set).
 */
osal_event_group_bits_t osal_event_group_sync(
    osal_event_group_handle_t event_group,
    const osal_event_group_bits_t bits_to_set,
    const osal_event_group_bits_t bits_to_wait_for,
    osal_tick_type_t ticks_to_wait
);


#ifdef __cplusplus
}
#endif

#endif /* __OSAL_EVENT_GROUP_H__ */
