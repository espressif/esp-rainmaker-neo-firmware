/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_rmaker_runtime_gate.h"

#include <stdatomic.h>

/* Default false: nothing runs until the SDK is fully started. seq_cst so the
 * flip is immediately visible across tasks and MQTT callbacks. */
static atomic_bool s_rmaker_active = false;

void esp_rmaker_runtime_gate_set_active(bool active)
{
    atomic_store(&s_rmaker_active, active);
}

bool esp_rmaker_should_do_work(void)
{
    return atomic_load(&s_rmaker_active);
}
