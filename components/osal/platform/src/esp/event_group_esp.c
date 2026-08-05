/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "osal_event_group.h"
#include "osal_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "osal_event_group";

/* NULL handle -> 0 bits (as on POSIX) instead of the configASSERT inside the
 * xEventGroup* calls. Caller address logged so addr2line can find the bad call site. */
#define EVENT_GROUP_NULL_GUARD()                                                           \
    do {                                                                                   \
        if (event_group == NULL) {                                                          \
            OSAL_LOGE(TAG, "%s on NULL event group (caller %p)", __func__,                  \
                      __builtin_return_address(0));                                          \
            return 0;                                                                       \
        }                                                                                   \
    } while (0)

osal_event_group_handle_t osal_event_group_create(void)
{
    return xEventGroupCreate();
}

void osal_event_group_delete(osal_event_group_handle_t event_group)
{
    if (event_group == NULL) {
        return;
    }
    vEventGroupDelete(event_group);
}

osal_event_group_bits_t osal_event_group_wait_bits(
    osal_event_group_handle_t event_group,
    const osal_event_group_bits_t bits_to_wait_for,
    const bool clear_on_exit,
    const bool wait_for_all_bits,
    osal_tick_type_t ticks_to_wait
)
{
    EVENT_GROUP_NULL_GUARD();
    return xEventGroupWaitBits(
               event_group,
               bits_to_wait_for,
               clear_on_exit ? pdTRUE : pdFALSE,
               wait_for_all_bits ? pdTRUE : pdFALSE,
               ticks_to_wait
           );
}

osal_event_group_bits_t osal_event_group_set_bits(
    osal_event_group_handle_t event_group,
    const osal_event_group_bits_t bits_to_set
)
{
    EVENT_GROUP_NULL_GUARD();
    return xEventGroupSetBits(event_group, bits_to_set);
}

osal_event_group_bits_t osal_event_group_clear_bits(
    osal_event_group_handle_t event_group,
    const osal_event_group_bits_t bits_to_clear
)
{
    EVENT_GROUP_NULL_GUARD();
    return xEventGroupClearBits(event_group, bits_to_clear);
}

osal_event_group_bits_t osal_event_group_get_bits(
    osal_event_group_handle_t event_group
)
{
    EVENT_GROUP_NULL_GUARD();
    return xEventGroupGetBits(event_group);
}

osal_event_group_bits_t osal_event_group_sync(
    osal_event_group_handle_t event_group,
    const osal_event_group_bits_t bits_to_set,
    const osal_event_group_bits_t bits_to_wait_for,
    osal_tick_type_t ticks_to_wait
)
{
    EVENT_GROUP_NULL_GUARD();
    return xEventGroupSync(event_group, bits_to_set, bits_to_wait_for, ticks_to_wait);
}
