/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mock_bootloader_posix.c
 * @brief Mock bootloader implementation for POSIX.
 */

#include "osal_ota_posix_config.h"
#include "osal_ota_posix_shared.h"

/* Standard includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#define bootloader_log(fmt, ...) printf("============= Bootloader: " fmt "\n", ##__VA_ARGS__)

/* Current child process PID */
static pid_t g_child_pid = 0;

/**
 * @brief Wait for the child process to exit
 *
 * @return exit code of the child process, -1 if not found/exited cleanly
 */
static int wait_for_child_exit(void)
{
    int status;
    waitpid(g_child_pid, &status, 0);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

/**
 * @brief Signal handler for terminating signals
 *
 * @param[in] signum signal number
 */
static void signal_handler(int signum)
{
    if (g_child_pid == 0) {
        bootloader_log("No child process to forward signal to. Exiting bootloader");
        exit(POSIX_EXIT_SUCCESS);
        return;
    }
    bootloader_log("Waiting for child process: %d to exit", g_child_pid);
    int code = wait_for_child_exit();
    exit(code); // use the child process's exit code to exit the bootloader
}

/**
 * @brief Get the next target to boot
 *
 * @return path to the next target, NULL if not found
 */
static const char *get_next_target(void)
{
    static char next_target[512];
    char label[OSAL_OTA_POSIX_MAX_LABEL_LEN];
    uint8_t boot_idx;
    osal_err_t rc = osal_ota_posix_config_get_boot_partition(&boot_idx);
    if (rc != OSAL_ERR_OK) {
        // Default to partition 0
        boot_idx = 0;
    }

    if (osal_ota_build_partition_path(boot_idx, next_target, sizeof(next_target)) != OSAL_ERR_OK) {
        return NULL;
    }

    /* Check if the file exists */
    if (access(next_target, F_OK) == -1) {
        // attempt to rollback to last valid
        uint8_t last_valid_idx;
        rc = osal_ota_posix_config_get_last_valid_partition(&last_valid_idx);
        if (rc != OSAL_ERR_OK) {
            return NULL;
        }
        if (last_valid_idx != boot_idx) {
            osal_ota_posix_config_set_boot_partition(last_valid_idx);
        }

        // build the path to the last valid partition
        if (osal_ota_build_partition_path(last_valid_idx, next_target, sizeof(next_target)) != OSAL_ERR_OK) {
            return NULL;
        }

        // check if the file exists
        if (access(next_target, F_OK) == -1) {
            return NULL;
        }

        bootloader_log("Could not find config boot partition. Rolled back to last valid partition: %s", next_target);
    }

    return next_target;
}

/**
 * @brief Main function
 */
int main(int argc, char **argv)
{
    bootloader_log("Starting bootloader");
    int code = POSIX_EXIT_SUCCESS;

    /* Loop forever, relaunching the bootloader on each iteration */
    for (;;) {
        /* Fork a new process to launch the next target */
        g_child_pid = fork();

        /* Child process */
        if (g_child_pid == 0) {
            // Launch the next target
            const char *target = get_next_target();
            if (!target) {
                code = POSIX_EXIT_FAILURE;
                break;
            }
            char *new_argv[argc + 1];
            new_argv[0] = (char *)target;
            for (int i = 1; i < argc; i++) {
                new_argv[i] = argv[i];
            }
            new_argv[argc] = NULL;

            bootloader_log("Executing target: %s", target);

            execv(target, new_argv);
            _exit(POSIX_EXIT_FAILURE);  // exec error
        }

        if (g_child_pid < 0) {
            return POSIX_EXIT_FAILURE;
        }

        /* Capture terminating signals */
        signal(SIGHUP, signal_handler);
        signal(SIGINT, signal_handler);
        signal(SIGABRT, signal_handler);
        signal(SIGTERM, signal_handler);

        /* Wait for the child process to finish */
        code = wait_for_child_exit();
        if (code == POSIX_EXIT_REBOOT) {
            bootloader_log("Rebooting system");
            continue;
        }
        break;
    }

    bootloader_log("Bootloader exiting with code: %d", code);
    return code;
}
