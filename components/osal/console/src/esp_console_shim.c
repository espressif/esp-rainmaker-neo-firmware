/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_console_shim.c
 * @brief POSIX implementation of the esp_console API shim.
 *
 * Maintains a small command registry and drives it from a stdin REPL thread, so that esp_console
 * based code (RainMaker Neo SDK + vendored components) runs unchanged on POSIX.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_console.h>
#include <esp_log.h>
#include "osal_task.h"

static const char *TAG = "osal_console";

#define ESP_CONSOLE_POSIX_DEFAULT_LINE_LEN 256
#define ESP_CONSOLE_POSIX_DEFAULT_MAX_ARGS 8
#define ESP_CONSOLE_POSIX_DEFAULT_PROMPT   ">> "

static esp_console_cmd_t *s_cmds;        /* growable array of registered commands */
static size_t s_cmd_count;
static size_t s_cmd_capacity;

static size_t s_max_line_len = ESP_CONSOLE_POSIX_DEFAULT_LINE_LEN;
static size_t s_max_args = ESP_CONSOLE_POSIX_DEFAULT_MAX_ARGS;

/* Single REPL instance; the handle is opaque to callers. */
struct esp_console_repl_s {
    char prompt[32];
    size_t line_len;
    uint32_t stack_size;
    uint32_t priority;
    osal_task_handle_t task;
};
static struct esp_console_repl_s s_repl;

static const esp_console_cmd_t *find_cmd(const char *name)
{
    for (size_t i = 0; i < s_cmd_count; i++) {
        if (strcmp(s_cmds[i].command, name) == 0) {
            return &s_cmds[i];
        }
    }
    return NULL;
}

static int help_command(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    /* Match the ESP-IDF `help` listing format: "<command> <hint>", help text indented two spaces,
     * commands separated by a blank line. */
    for (size_t i = 0; i < s_cmd_count; i++) {
        printf("%s %s\n", s_cmds[i].command, s_cmds[i].hint ? s_cmds[i].hint : "");
        if (s_cmds[i].help && s_cmds[i].help[0]) {
            printf("  %s\n", s_cmds[i].help);
        }
        printf("\n");
    }
    return 0;
}

esp_err_t esp_console_init(const esp_console_config_t *config)
{
    if (config) {
        if (config->max_cmdline_length) {
            s_max_line_len = config->max_cmdline_length;
        }
        if (config->max_cmdline_args) {
            s_max_args = config->max_cmdline_args;
        }
    }
    /* Fresh registry. */
    free(s_cmds);
    s_cmds = NULL;
    s_cmd_count = 0;
    s_cmd_capacity = 0;
    return ESP_OK;
}

esp_err_t esp_console_deinit(void)
{
    free(s_cmds);
    s_cmds = NULL;
    s_cmd_count = 0;
    s_cmd_capacity = 0;
    return ESP_OK;
}

esp_err_t esp_console_register_help_command(void)
{
    const esp_console_cmd_t cmd = {
        .command = "help",
        .help = "List all registered commands",
        .hint = NULL,
        .func = &help_command,
        .argtable = NULL,
    };
    return esp_console_cmd_register(&cmd);
}

esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd)
{
    if (!cmd || !cmd->command || !cmd->func) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Replace an existing command with the same name (matches ESP-IDF behaviour). */
    for (size_t i = 0; i < s_cmd_count; i++) {
        if (strcmp(s_cmds[i].command, cmd->command) == 0) {
            s_cmds[i] = *cmd;
            return ESP_OK;
        }
    }
    if (s_cmd_count == s_cmd_capacity) {
        size_t new_cap = s_cmd_capacity ? s_cmd_capacity * 2 : 16;
        esp_console_cmd_t *grown = realloc(s_cmds, new_cap * sizeof(*s_cmds));
        if (!grown) {
            return ESP_ERR_NO_MEM;
        }
        s_cmds = grown;
        s_cmd_capacity = new_cap;
    }
    s_cmds[s_cmd_count++] = *cmd;
    return ESP_OK;
}

esp_err_t esp_console_run(const char *cmdline, int *cmd_ret)
{
    if (cmd_ret) {
        *cmd_ret = 0;
    }
    if (!cmdline) {
        return ESP_ERR_INVALID_ARG;
    }

    char *line = strdup(cmdline);
    if (!line) {
        return ESP_ERR_NO_MEM;
    }

    /* argv gets one extra slot for the trailing NULL terminator. */
    char **argv = calloc(s_max_args + 1, sizeof(*argv));
    if (!argv) {
        free(line);
        return ESP_ERR_NO_MEM;
    }

    int argc = 0;
    char *saveptr = NULL;
    for (char *tok = strtok_r(line, " \t\r\n", &saveptr);
            tok && (size_t) argc < s_max_args;
            tok = strtok_r(NULL, " \t\r\n", &saveptr)) {
        argv[argc++] = tok;
    }

    esp_err_t ret = ESP_OK;
    if (argc == 0) {
        /* Empty line: nothing to do. */
    } else {
        const esp_console_cmd_t *cmd = find_cmd(argv[0]);
        if (!cmd) {
            printf("Unrecognized command: %s\n", argv[0]);
            ret = ESP_ERR_NOT_FOUND;
            if (cmd_ret) {
                *cmd_ret = 1;
            }
        } else {
            int rc = cmd->func(argc, argv);
            if (cmd_ret) {
                *cmd_ret = rc;
            }
        }
    }

    free(argv);
    free(line);
    return ret;
}

static void repl_task(void *arg)
{
    struct esp_console_repl_s *repl = (struct esp_console_repl_s *) arg;
    char *line = malloc(repl->line_len);
    if (!line) {
        ESP_LOGE(TAG, "REPL: out of memory");
        osal_task_delete(NULL);
        return;
    }

    while (1) {
        fputs(repl->prompt, stdout);
        fflush(stdout);
        if (!fgets(line, (int) repl->line_len, stdin)) {
            /* EOF or read error: stop the REPL. */
            break;
        }
        int cmd_ret = 0;
        esp_console_run(line, &cmd_ret);
        if (cmd_ret != 0) {
            printf("Command returned non-zero error code: %d\n", cmd_ret);
        }
    }

    free(line);
    ESP_LOGI(TAG, "REPL stopped");
    osal_task_delete(NULL);
}

esp_err_t esp_console_new_repl_uart(const esp_console_dev_uart_config_t *dev_config,
                                    const esp_console_repl_config_t *repl_config,
                                    esp_console_repl_t **ret_repl)
{
    (void) dev_config; /* stdin is always used on POSIX */
    if (!ret_repl) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *prompt = ESP_CONSOLE_POSIX_DEFAULT_PROMPT;
    s_repl.line_len = s_max_line_len;
    s_repl.stack_size = 8192;
    s_repl.priority = 5;
    if (repl_config) {
        if (repl_config->prompt) {
            prompt = repl_config->prompt;
        }
        if (repl_config->max_cmdline_length) {
            s_repl.line_len = repl_config->max_cmdline_length;
        }
        if (repl_config->task_stack_size) {
            s_repl.stack_size = repl_config->task_stack_size;
        }
        if (repl_config->task_priority) {
            s_repl.priority = repl_config->task_priority;
        }
    }
    snprintf(s_repl.prompt, sizeof(s_repl.prompt), "%s", prompt);
    s_repl.task = NULL;

    /* On ESP-IDF, esp_console_new_repl_uart() sets up the console (incl. the built-in help command)
     * internally. Mirror that here so `help` is available regardless of caller ordering. */
    esp_console_register_help_command();

    *ret_repl = &s_repl;
    return ESP_OK;
}

esp_err_t esp_console_start_repl(esp_console_repl_t *repl)
{
    if (!repl) {
        return ESP_ERR_INVALID_ARG;
    }
    osal_err_t err = osal_task_create(&repl_task, "console_repl", repl->stack_size, repl,
                                      repl->priority, &repl->task);
    if (err != OSAL_ERR_OK) {
        ESP_LOGE(TAG, "Failed to start REPL task");
        return err;
    }
    return ESP_OK;
}
