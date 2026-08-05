/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_runtime_gate.h
 * @brief Runtime gate for ESP RainMaker Neo deferrable work.
 *
 * A single atomic "should I do work?" flag shared across all managers. It is the
 * one source of truth for "is the SDK operational?" so that background work
 * (state reports, timeseries publishes, cloud-event processing, schedule/
 * automation fires) stops being triggered the instant stop/reset begins.
 *
 * Semantics:
 *   - ``true``  = STARTED and operational.
 *   - ``false`` = stopping / stopped / resetting: drop deferrable work.
 *
 * Control-plane tasks (start_task, stop_task, reconnect) do NOT consult it.
 *
 * @note The gate is advisory (an atomic flag, not a lock): a task that reads ``true``
 * one instruction before ``stop()`` flips it ``false`` still proceeds. It
 * narrows the race window to near-zero but does not close it.
 */

#ifndef __ESP_RMAKER_RUNTIME_GATE_H__
#define __ESP_RMAKER_RUNTIME_GATE_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the runtime gate active state.
 *
 * @param[in] active ``true`` when the SDK is fully started and operational;
 *                   ``false`` when stopping/stopped/resetting.
 */
void esp_rmaker_runtime_gate_set_active(bool active);

/**
 * @brief Query whether deferrable work should run.
 *
 * Non-blocking; callable from any task or MQTT callback context.
 *
 * @return ``true`` if the SDK is operational and deferrable work may proceed.
 * @return ``false`` if stopping/stopped/resetting; callers must drop the work.
 */
bool esp_rmaker_should_do_work(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_RUNTIME_GATE_H__ */
