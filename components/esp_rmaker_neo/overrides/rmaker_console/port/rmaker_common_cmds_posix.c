/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rmaker_common_cmds_posix.c
 * @brief Portable POSIX replacement for the upstream rmaker_common_cmds.c.
 *
 * The upstream common-commands source depends on ESP-only facilities (lwip sockets, argtable3,
 * heap_trace, NVS, rmaker_system_ctrl / rmaker_time_sync). On POSIX we provide a portable subset
 * implemented over platform-common, so esp_rmaker_common_console_init() behaves the same on both
 * platforms (init the console, then register common commands).
 *
 * Emulated subset: reboot, up-time, local-time, reset-to-factory. The remaining ESP commands
 * (mem-dump, task-dump, cpu-dump, sock-dump, heap-trace, tz-set) are intentionally omitted on POSIX.
 */

#include <stdio.h>
#include <time.h>

#include <esp_console.h>
#include <esp_log.h>

#include "osal_sysctrl.h"
#include "osal_task.h"
#include "osal_ticks.h"
#include "osal_time.h"
#include "osal_storage.h"

#include <esp_rmaker_common_console.h>

/* RainMaker NVS partition label (mirrors rmng-common's RMAKER_NVS_PART_NAME); avoids pulling rmng
 * private headers into this vendored glue. */
#define RMAKER_CONSOLE_POSIX_NVS_PART "nvs"

static const char *TAG = "rmng_console_glue";

static int reboot_handler(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    printf("Rebooting...\n");
    osal_sysctrl_reboot();
    return 0;
}

static int up_time_handler(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    uint32_t uptime_ms = osal_ms_from_ticks(osal_task_get_tick_count());
    printf("Uptime of the device: %u milliseconds\n", (unsigned int) uptime_ms);
    return 0;
}

static int local_time_handler(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    time_t now = osal_get_time(NULL);
    struct tm tm_buf;
    char local_time[64];
    if (localtime_r(&now, &tm_buf) && strftime(local_time, sizeof(local_time), "%a %b %d %H:%M:%S %Y", &tm_buf)) {
        printf("Current local time: %s\n", local_time);
    } else {
        printf("Failed to get local time\n");
        return 1;
    }
    return 0;
}

static int reset_to_factory_handler(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    printf("Resetting to factory defaults...\n");
    /* Erase the RainMaker NVS partition (claim/config data), then reboot - the same effect as the
     * SDK's factory reset, minus app-specific network-credential reset. */
    if (osal_storage_reset(RMAKER_CONSOLE_POSIX_NVS_PART) != OSAL_ERR_OK) {
        printf("Failed to erase NVS partition '%s'\n", RMAKER_CONSOLE_POSIX_NVS_PART);
        return 1;
    }
    osal_sysctrl_reboot();
    return 0;
}

static void register_command(const char *command, const char *help, esp_console_cmd_func_t func)
{
    const esp_console_cmd_t cmd = {
        .command = command,
        .help = help,
        .hint = NULL,
        .func = func,
        .argtable = NULL,
    };
    if (esp_console_cmd_register(&cmd) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register '%s'", command);
    }
}

void esp_rmaker_common_register_commands(void)
{
    register_command("reboot", "Reboot the device", &reboot_handler);
    register_command("up-time", "Get the device up time in milliseconds.", &up_time_handler);
    register_command("local-time", "Get the local time of device.", &local_time_handler);
    register_command("reset-to-factory", "Reset to factory defaults", &reset_to_factory_handler);
}
