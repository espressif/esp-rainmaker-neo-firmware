/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file app_entry_posix.c
 * @brief POSIX entry point. Runs the example, then blocks until a termination
 *        signal arrives and tears the SDK down cleanly.
 *
 * The teardown is generic: it stops the agent, disables the optional local
 * service and deinitialises the node resolved via esp_rmaker_get_node(), so the
 * example itself does not need to expose anything for shutdown.
 */

/* Standard includes */
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

/* App entry includes */
#include "app_entry.h"

/* RMNG includes */
#include "esp_rmaker_core.h"
#include "esp_rmaker_node.h"

/* Platform includes */
#include "posix_exit_codes.h"

/* Configuration includes */
#include "sdkconfig.h"

/* Tag for logging */
static const char *TAG = "app_entry";

/**
 * @brief Tear the SDK down: stop the agent, disable the optional local service
 *        and deinitialise the node.
 *
 * Weak default: an example whose shutdown differs (e.g. a remote-control node)
 * may provide a strong app_teardown() to override this.
 */
__attribute__((weak)) void app_teardown(void)
{
    /* Stop the SDK */
    if (esp_rmaker_stop() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to stop SDK");
    }

    /* Disable optional local HTTP service */
#if CONFIG_ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE
    if (esp_rmaker_chal_resp_service_disable() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to disable on-network challenge response service");
    }
#else /* !CONFIG_ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE */
    if (esp_rmaker_local_ctrl_service_disable() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to disable local control service");
    }
#endif /* !CONFIG_ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE */

    /* Deinitialise the SDK */
    if (esp_rmaker_node_deinit(esp_rmaker_get_node()) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to deinitialise SDK");
    }

    OSAL_LOGI(TAG, "Exiting");
}

/**
 * @brief Termination signal handler: tear down and exit successfully.
 */
static void __on_signal(int signal)
{
    (void) signal;
    app_teardown();
    /* exit() (not _exit()) so atexit/destructor handlers run - notably the
     * gcov flush that writes .gcda coverage for instrumented POSIX builds. */
    exit(POSIX_EXIT_SUCCESS);
}

int main(void)
{
    /* Run the portable example startup. */
    if (app_run() != OSAL_ERR_OK) {
        return POSIX_EXIT_FAILURE;
    }

    /* Block until a termination signal, then tear down. */
    signal(SIGHUP, __on_signal);
    signal(SIGINT, __on_signal);
    signal(SIGABRT, __on_signal);
    signal(SIGTERM, __on_signal);
    pause();

    /* Fallback if pause() ever returns. */
    app_teardown();
    return POSIX_EXIT_SUCCESS;
}
