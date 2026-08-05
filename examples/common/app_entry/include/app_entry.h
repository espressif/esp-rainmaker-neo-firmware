/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_entry.h
 * @brief Platform entry-point shim for examples.
 *
 * Each example implements only a portable @c app_run() (top-to-bottom startup
 * logic). This component owns the actual platform entry point:
 *  - ESP-IDF: @c void @c app_main(void) calls @c app_run() once; the device
 *    keeps running on FreeRTOS tasks.
 *  - POSIX:   @c int @c main(void) calls @c app_run(); on failure it exits with
 *    @c POSIX_EXIT_FAILURE, otherwise it waits for a termination signal, tears
 *    the SDK down and exits with @c POSIX_EXIT_SUCCESS. The teardown is generic
 *    (it resolves the node via @c esp_rmaker_get_node()), so it lives in the
 *    POSIX backend and needs nothing from the example.
 *
 * This keeps the example source free of @c app_main / @c main / signal / pause /
 * teardown boilerplate and free of OS `#ifdef`.
 */

#ifndef __APP_ENTRY_H__
#define __APP_ENTRY_H__

/* Platform includes */
#include "osal_err.h"
#include "osal_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Portable example startup. Implemented by the example.
 *
 * @return OSAL_ERR_OK on success; any other value signals a startup failure.
 */
osal_err_t app_run(void);

/**
 * @brief Optional example teardown, run on POSIX before the process exits.
 *
 * Weakly defined by the POSIX backend to perform the generic SDK teardown
 * (stop the agent, disable the optional local service, deinitialise the node).
 * An example whose shutdown differs (e.g. a remote-control node) may provide a
 * strong definition to override it. Never called on ESP-IDF.
 */
void app_teardown(void);

/**
 * @brief Log and return early when @p expr does not evaluate to @c OSAL_ERR_OK.
 *
 * Replaces the ESP-IDF early @c return; and the POSIX @c return
 * @c POSIX_EXIT_FAILURE; idioms with a single portable check. The enclosing
 * function must return @c osal_err_t (i.e. @c app_run()) and a @c TAG must be in
 * scope for logging.
 */
#define APP_RETURN_ON_ERR(expr, msg)                                    \
    do {                                                                \
        osal_err_t __err_rc = (expr);                                     \
        if (__err_rc != OSAL_ERR_OK) {                                    \
            OSAL_LOGE(TAG, "%s (%s)", (msg),                        \
                          osal_err_strerror(__err_rc));                   \
            return __err_rc;                                            \
        }                                                               \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* __APP_ENTRY_H__ */
