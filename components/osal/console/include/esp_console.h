/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_console.h
 * @brief POSIX shim of the ESP-IDF esp_console API.
 *
 * Provides the subset of the esp_console interface used by the RainMaker Neo SDK and by vendored
 * ESP-IDF components (e.g. rmaker_console) so that the same `esp_console_*` code compiles and runs
 * unchanged on POSIX. The implementation is backed by a stdin REPL thread (see esp_console_shim.c).
 *
 * This header is only on the include path for POSIX builds; on ESP-IDF the native esp_console.h is
 * used instead.
 */

#pragma once

#include <stddef.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Console command handler: classic argc/argv signature, matching ESP-IDF. */
typedef int (*esp_console_cmd_func_t)(int argc, char **argv);

/** @brief A single console command. Mirrors the ESP-IDF layout (argtable kept for source compat). */
typedef struct {
    const char *command;            /*!< Command name (first token on the line) */
    const char *help;               /*!< Help text shown by the `help` command */
    const char *hint;               /*!< Argument hint (may be NULL) */
    esp_console_cmd_func_t func;     /*!< Command handler */
    void *argtable;                 /*!< Unused on POSIX; accepted for source compatibility */
} esp_console_cmd_t;

/** @brief Console configuration. Only the line/arg limits are honoured on POSIX. */
typedef struct {
    size_t max_cmdline_length;       /*!< Maximum command line length in bytes */
    size_t max_cmdline_args;         /*!< Maximum number of arguments per command line */
    int hint_color;                  /*!< Unused on POSIX */
    int hint_bold;                   /*!< Unused on POSIX */
} esp_console_config_t;

#define ESP_CONSOLE_CONFIG_DEFAULT()  \
    {                                 \
        .max_cmdline_length = 256,    \
        .max_cmdline_args = 8,        \
        .hint_color = 0,              \
        .hint_bold = 0,               \
    }

/** @brief REPL configuration. Fields accepted for source compatibility; mostly ignored on POSIX. */
typedef struct {
    unsigned int max_history_len;    /*!< Unused on POSIX */
    const char *history_save_path;   /*!< Unused on POSIX */
    unsigned int task_stack_size;    /*!< REPL thread stack size (bytes) */
    unsigned int task_priority;      /*!< REPL thread priority */
    const char *prompt;              /*!< Prompt string (NULL -> default) */
    size_t max_cmdline_length;       /*!< Maximum command line length in bytes */
} esp_console_repl_config_t;

#define ESP_CONSOLE_REPL_CONFIG_DEFAULT()  \
    {                                      \
        .max_history_len = 0,              \
        .history_save_path = NULL,         \
        .task_stack_size = 8192,           \
        .task_priority = 5,                \
        .prompt = NULL,                    \
        .max_cmdline_length = 256,         \
    }

/** @brief UART REPL device configuration. Accepted and ignored on POSIX (stdin is always used). */
typedef struct {
    int channel;
    int baud_rate;
    int tx_gpio_num;
    int rx_gpio_num;
} esp_console_dev_uart_config_t;

#define ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT()  \
    {                                          \
        .channel = 0,                          \
        .baud_rate = 115200,                   \
        .tx_gpio_num = -1,                     \
        .rx_gpio_num = -1,                     \
    }

/** @brief Opaque REPL handle. */
typedef struct esp_console_repl_s esp_console_repl_t;

/**
 * @brief Initialize the console command registry.
 *
 * @param[in] config Console configuration (line/arg limits). May be NULL for defaults.
 * @return ESP_OK on success.
 */
esp_err_t esp_console_init(const esp_console_config_t *config);

/**
 * @brief De-initialize the console and free the command registry.
 *
 * @return ESP_OK on success.
 */
esp_err_t esp_console_deinit(void);

/**
 * @brief Register the built-in `help` command which lists all registered commands.
 *
 * @return ESP_OK on success.
 */
esp_err_t esp_console_register_help_command(void);

/**
 * @brief Register a console command.
 *
 * @param[in] cmd Command definition. The struct is copied; the strings it points to must outlive it.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG / ESP_ERR_NO_MEM on failure.
 */
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd);

/**
 * @brief Tokenize and dispatch a single command line.
 *
 * @param[in]  cmdline Command line to run.
 * @param[out] cmd_ret Set to the command handler's return value (or 1 if not found).
 * @return ESP_OK if a command was dispatched, ESP_ERR_NOT_FOUND if unknown command, error otherwise.
 */
esp_err_t esp_console_run(const char *cmdline, int *cmd_ret);

/**
 * @brief Create a REPL backed by stdin (the UART device config is ignored on POSIX).
 *
 * @param[in]  dev_config  Accepted for source compatibility; ignored.
 * @param[in]  repl_config REPL configuration.
 * @param[out] ret_repl    Receives the REPL handle.
 * @return ESP_OK on success.
 */
esp_err_t esp_console_new_repl_uart(const esp_console_dev_uart_config_t *dev_config,
                                    const esp_console_repl_config_t *repl_config,
                                    esp_console_repl_t **ret_repl);

/**
 * @brief Start the REPL: spawns a thread that reads lines from stdin and dispatches them.
 *
 * @param[in] repl REPL handle from esp_console_new_repl_uart().
 * @return ESP_OK on success.
 */
esp_err_t esp_console_start_repl(esp_console_repl_t *repl);

#ifdef __cplusplus
}
#endif
