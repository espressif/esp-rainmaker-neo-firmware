/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_event.h
 * @brief POSIX event macros for vendored ESP-IDF components on POSIX.
 */

#ifndef __ESP_EVENT_H__
#define __ESP_EVENT_H__

#include "osal_event_loop.h"
#include "freertos/portmacro.h"

#define ESP_EVENT_DECLARE_BASE(base) OSAL_EVENT_DECLARE_BASE(base)
#define ESP_EVENT_DEFINE_BASE(base) OSAL_EVENT_DEFINE_BASE(base)
#define ESP_EVENT_ANY_ID OSAL_EVENT_ID_ANY

#define esp_event_post(base, id, data, size, ticks) osal_event_post(base, id, data, size, ticks)
#endif /* __ESP_EVENT_H__ */
