/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* FreeRTOS task compatibility header for vendored ESP-IDF sources on POSIX. See FreeRTOS.h. */
#pragma once

#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "osal_task.h"

#define vTaskDelay(ticks) osal_task_delay(ticks)

/* Only the calling task can be suspended (task == NULL), and nothing resumes it. */
static inline void vTaskSuspend(void *task)
{
    (void) task;
    for (;;) {
        pause();
    }
}
