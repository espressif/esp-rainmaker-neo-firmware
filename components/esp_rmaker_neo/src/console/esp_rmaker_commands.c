/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_commands.c
 * @brief RainMaker Neo SDK built-in console commands.
 *
 * Implemented over the public RainMaker Neo getters so the same commands work on ESP-IDF and POSIX. Each
 * command follows the esp_console handler/registration pattern.
 */

#include <stdio.h>

#include <esp_console.h>
#include <esp_log.h>

#include "esp_rmaker_node.h"
#include "esp_rmaker_system_ctrl.h"
#include "esp_rmaker_console.h"
#include "system_ctrl_internal.h"

#ifdef CONFIG_ESP_RMAKER_ASSISTED_CLAIM
#include "esp_rmaker_credentials.h"
#endif /* CONFIG_ESP_RMAKER_ASSISTED_CLAIM */

#ifdef CONFIG_RMNG_CONSOLE_PARAM_CMDS_ENABLED
#include <stdlib.h>
#include <string.h>

#include "esp_rmaker_val.h"
#include "esp_rmaker_state.h"
#include "esp_rmaker_data_model.h"
#include "data_model_internal.h"
#endif /* CONFIG_RMNG_CONSOLE_PARAM_CMDS_ENABLED */

static const char *TAG = "rmng_console_cmds";

/* Seconds to wait before rebooting after a reset command, so the console response is flushed. */
#define RMAKER_CMD_RESET_REBOOT_S 2

/* Console command exit codes (shell-style: 0 = success, non-zero = failure). Distinct from
 * esp_rmaker_error_t - the REPL prints any non-zero value as the command's error code. */
#define CONSOLE_OK   0
#define CONSOLE_FAIL 1

static int get_node_id_handler(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    char *node_id = esp_rmaker_get_node_id();
    if (!node_id) {
        printf("Node ID not available\n");
        return CONSOLE_FAIL;
    }
    printf("Node ID: %s\n", node_id);
    return CONSOLE_OK;
}

static int node_info_handler(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    const esp_rmaker_node_t *node = esp_rmaker_get_node();
    if (!node) {
        printf("Node not initialized\n");
        return CONSOLE_FAIL;
    }
    esp_rmaker_node_info_t *info = esp_rmaker_node_get_info(node);
    if (!info) {
        printf("Node info not available\n");
        return CONSOLE_FAIL;
    }
    printf("Name       : %s\n", info->name ? info->name : "(null)");
    printf("Type       : %s\n", info->type ? info->type : "(null)");
    printf("FW Version : %s\n", info->fw_version ? info->fw_version : "(null)");
    printf("Model      : %s\n", info->model ? info->model : "(null)");
    return CONSOLE_OK;
}

static int reset_data_handler(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    printf("Resetting RMNG data (node config, local config)...\n");
    esp_rmaker_error_t err = esp_rmaker_system_ctrl_data_reset(0, RMAKER_CMD_RESET_REBOOT_S);
    if (err != ESP_RMAKER_OK) {
        printf("Data reset failed: 0x%x\n", err);
        return CONSOLE_FAIL;
    }
    return CONSOLE_OK;
}

static int reset_network_handler(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    printf("Resetting network credentials...\n");
    /* NULL -> use the application's registered network reset function. */
    esp_rmaker_error_t err = esp_rmaker_system_ctrl_network_reset(0, RMAKER_CMD_RESET_REBOOT_S, NULL);
    if (err != ESP_RMAKER_OK) {
        printf("Network reset failed: 0x%x (no network reset function registered?)\n", err);
        return CONSOLE_FAIL;
    }
    return CONSOLE_OK;
}

static int reset_factory_handler(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    printf("Factory resetting (erases RMNG NVS + network credentials)...\n");
    /* NULL -> use the application's registered network reset function. */
    esp_rmaker_error_t err = esp_rmaker_system_ctrl_factory_reset(0, RMAKER_CMD_RESET_REBOOT_S, NULL);
    if (err != ESP_RMAKER_OK) {
        printf("Factory reset failed: 0x%x\n", err);
        return CONSOLE_FAIL;
    }
    return CONSOLE_OK;
}

#ifdef CONFIG_ESP_RMAKER_ASSISTED_CLAIM
static int clear_claim_data_handler(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    /* The claim credentials define the node's cloud identity, so everything keyed to that identity
     * (node config checksums, local config, per-device values, network credentials) becomes stale
     * once they are gone. Wipe first, erase the credentials last. */
    printf("Factory resetting node data...\n");
    esp_rmaker_error_t reset_err = esp_rmaker_system_ctrl_factory_reset_no_reboot(NULL);
    if (reset_err != ESP_RMAKER_OK) {
        printf("Factory resetting node data reported errors: 0x%x (continuing)\n", reset_err);
    }

    printf("Erasing claim data (node ID, certificate, private key, MQTT host) from the factory partition...\n");
    esp_rmaker_error_t err = esp_rmaker_credentials_erase_claim_data();
    if (err != ESP_RMAKER_OK) {
        printf("Clearing claim data failed: 0x%x\n", err);
    } else {
        printf("Claim data erased, and node factory reset complete. The node will be claimed again after rebooting.\n");
    }

    /* Reboot regardless: the data wipe above already happened, so continuing to run on top of it is
     * worse than rebooting into a re-claim (or, if the erase failed, into the existing claim). */
    esp_rmaker_error_t reboot_err = esp_rmaker_system_ctrl_reboot(RMAKER_CMD_RESET_REBOOT_S);
    if (reboot_err != ESP_RMAKER_OK) {
        printf("Reboot failed: 0x%x\n", reboot_err);
    }
    return (err == ESP_RMAKER_OK && reboot_err == ESP_RMAKER_OK) ? CONSOLE_OK : CONSOLE_FAIL;
}
#endif /* CONFIG_ESP_RMAKER_ASSISTED_CLAIM */

#ifdef CONFIG_RMNG_CONSOLE_PARAM_CMDS_ENABLED
/* Resolve a device + parameter handle from their IDs. Prints a message and returns NULL on failure. */
static esp_rmaker_param_t *resolve_param(const char *device_id, const char *param_id)
{
    esp_rmaker_device_t *device = esp_rmaker_node_get_device_by_id(esp_rmaker_get_node(), device_id);
    if (!device) {
        printf("Device %s not found\n", device_id);
        return NULL;
    }
    esp_rmaker_param_t *param = esp_rmaker_device_get_param_by_id(device, param_id);
    if (!param) {
        printf("Parameter %s not found in device %s\n", param_id, device_id);
        return NULL;
    }
    return param;
}

static int set_param_handler(int argc, char **argv)
{
    if (argc != 4) {
        printf("Invalid Usage.\n");
        printf("Usage: set-param <device_id> <param_id> <value>\n");
        printf("  Note: This command invokes device callbacks (simulates real parameter changes)\n");
        return CONSOLE_FAIL;
    }

    const char *device_id = argv[1];
    const char *param_id = argv[2];
    const char *value_str = argv[3];

    esp_rmaker_param_t *param = resolve_param(device_id, param_id);
    if (!param) {
        return CONSOLE_FAIL;
    }

    esp_rmaker_param_val_t *val = esp_rmaker_param_get_val(param);
    if (!val) {
        printf("Failed to get parameter value\n");
        return CONSOLE_FAIL;
    }

    /* Build a set-params JSON payload {"<device_id>":{"<param_id>":<value>}} and feed it through the
     * standard state-change path so registered write callbacks are invoked, just like a cloud set. */
    char json_str[1024];
    int json_len = 0;

    switch (val->type) {
    case RMAKER_VAL_TYPE_BOOLEAN: {
        bool b = (strcmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0);
        json_len = snprintf(json_str, sizeof(json_str), "{\"%s\":{\"%s\":%s}}",
                            device_id, param_id, b ? "true" : "false");
        break;
    }
    case RMAKER_VAL_TYPE_INTEGER:
        json_len = snprintf(json_str, sizeof(json_str), "{\"%s\":{\"%s\":%d}}",
                            device_id, param_id, atoi(value_str));
        break;
    case RMAKER_VAL_TYPE_FLOAT:
        json_len = snprintf(json_str, sizeof(json_str), "{\"%s\":{\"%s\":%f}}",
                            device_id, param_id, (float) atof(value_str));
        break;
    case RMAKER_VAL_TYPE_STRING:
        json_len = snprintf(json_str, sizeof(json_str), "{\"%s\":{\"%s\":\"%s\"}}",
                            device_id, param_id, value_str);
        break;
    case RMAKER_VAL_TYPE_OBJECT:
    case RMAKER_VAL_TYPE_ARRAY:
        /* JSON objects/arrays are embedded directly without quotes */
        json_len = snprintf(json_str, sizeof(json_str), "{\"%s\":{\"%s\":%s}}",
                            device_id, param_id, value_str);
        break;
    default:
        printf("Unsupported value type\n");
        return CONSOLE_FAIL;
    }

    if (json_len <= 0 || json_len >= (int) sizeof(json_str)) {
        printf("Failed to create JSON for callback (len=%d, max=%d)\n", json_len, (int) sizeof(json_str));
        return CONSOLE_FAIL;
    }

    esp_rmaker_error_t err = data_model_state_handle_update_payload_json(
                                 esp_rmaker_get_node(), json_str, json_len, ESP_RMAKER_REQ_SRC_FIRMWARE);
    if (err != ESP_RMAKER_OK) {
        printf("Callback failed for %s.%s: 0x%x\n", device_id, param_id, err);
        return CONSOLE_FAIL;
    }

    printf("Successfully set %s.%s to %s with callback\n", device_id, param_id, value_str);
    return CONSOLE_OK;
}

static int update_param_handler(int argc, char **argv)
{
    if (argc != 4) {
        printf("Invalid Usage.\n");
        printf("Usage: update-param <device_id> <param_id> <value>\n");
        printf("  Note: This command only updates the value without invoking callbacks\n");
        return CONSOLE_FAIL;
    }

    const char *device_id = argv[1];
    const char *param_id = argv[2];
    const char *value_str = argv[3];

    esp_rmaker_param_t *param = resolve_param(device_id, param_id);
    if (!param) {
        return CONSOLE_FAIL;
    }

    esp_rmaker_param_val_t *val = esp_rmaker_param_get_val(param);
    if (!val) {
        printf("Failed to get parameter value\n");
        return CONSOLE_FAIL;
    }

    esp_rmaker_param_val_t new_val;
    new_val.type = val->type;

    switch (val->type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        new_val.val.b = (strcmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0);
        break;
    case RMAKER_VAL_TYPE_INTEGER:
        new_val.val.i = atoi(value_str);
        break;
    case RMAKER_VAL_TYPE_FLOAT:
        new_val.val.f = (float) atof(value_str);
        break;
    case RMAKER_VAL_TYPE_STRING:
    case RMAKER_VAL_TYPE_OBJECT:
    case RMAKER_VAL_TYPE_ARRAY:
        new_val.val.s = strdup(value_str);  /* copy to avoid lifetime issues */
        if (!new_val.val.s) {
            printf("Failed to allocate memory for value\n");
            return CONSOLE_FAIL;
        }
        break;
    default:
        printf("Unsupported value type\n");
        return CONSOLE_FAIL;
    }

    /* Update the parameter value without invoking write callbacks. */
    esp_rmaker_error_t err = esp_rmaker_param_update_and_report(param, new_val);

    if ((val->type == RMAKER_VAL_TYPE_STRING || val->type == RMAKER_VAL_TYPE_OBJECT ||
            val->type == RMAKER_VAL_TYPE_ARRAY) && new_val.val.s) {
        free(new_val.val.s);
    }

    if (err != ESP_RMAKER_OK) {
        printf("Failed to update parameter value: 0x%x\n", err);
        return CONSOLE_FAIL;
    }

    printf("Successfully updated %s.%s to %s (no callback)\n", device_id, param_id, value_str);
    return CONSOLE_OK;
}

static int get_param_handler(int argc, char **argv)
{
    if (argc != 3) {
        printf("Invalid Usage.\n");
        printf("Usage: get-param <device_id> <param_id>\n");
        return CONSOLE_FAIL;
    }

    const char *device_id = argv[1];
    const char *param_id = argv[2];

    esp_rmaker_param_t *param = resolve_param(device_id, param_id);
    if (!param) {
        return CONSOLE_FAIL;
    }

    esp_rmaker_param_val_t *val = esp_rmaker_param_get_val(param);
    if (!val) {
        printf("Failed to get parameter value\n");
        return CONSOLE_FAIL;
    }

    switch (val->type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        printf("%s.%s = %s\n", device_id, param_id, val->val.b ? "true" : "false");
        break;
    case RMAKER_VAL_TYPE_INTEGER:
        printf("%s.%s = %d\n", device_id, param_id, val->val.i);
        break;
    case RMAKER_VAL_TYPE_FLOAT:
        printf("%s.%s = %f\n", device_id, param_id, val->val.f);
        break;
    case RMAKER_VAL_TYPE_STRING:
    case RMAKER_VAL_TYPE_OBJECT:
    case RMAKER_VAL_TYPE_ARRAY:
        printf("%s.%s = %s\n", device_id, param_id, val->val.s ? val->val.s : "(null)");
        break;
    default:
        printf("Unsupported value type\n");
        return CONSOLE_FAIL;
    }

    return CONSOLE_OK;
}
#endif /* CONFIG_RMNG_CONSOLE_PARAM_CMDS_ENABLED */

static void register_command(const char *command, const char *help, esp_console_cmd_func_t func)
{
    const esp_console_cmd_t cmd = {
        .command = command,
        .help = help,
        .hint = NULL,
        .func = func,
        .argtable = NULL,
    };
    ESP_LOGI(TAG, "Registering command: %s", command);
    esp_err_t err = esp_console_cmd_register(&cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register command '%s': 0x%x", command, err);
    }
}

void esp_rmaker_register_commands(void)
{
    register_command("get-node-id", "Get the Node ID for this device", &get_node_id_handler);
    register_command("node-info", "Print node name, type, firmware version and model", &node_info_handler);
    register_command("reset-data", "Erase RMNG data namespaces (keeps network creds) and reboot", &reset_data_handler);
    register_command("reset-network", "Reset network credentials and reboot", &reset_network_handler);

    /* Override the common console's "reset-to-factory" (registered by esp_rmaker_common_console_init,
     * which runs before this) so factory reset goes through esp_rmaker_system_ctrl_factory_reset, like
     * the other reset commands above. Re-registering the same name replaces the handler in place on
     * both ESP-IDF (esp_console_cmd_register dedups by command name) and the POSIX shim, so no
     * deregister is needed -- esp_console_cmd_deregister is unavailable before ESP-IDF v5.4. */
    register_command("reset-to-factory", "Factory reset (erase RMNG NVS + network creds) and reboot", &reset_factory_handler);

#ifdef CONFIG_ESP_RMAKER_ASSISTED_CLAIM
    register_command("clear-claim-data", "Erase claim data (node ID, cert, key, MQTT host) from the factory partition and reboot", &clear_claim_data_handler);
#endif /* CONFIG_ESP_RMAKER_ASSISTED_CLAIM */
#ifdef CONFIG_RMNG_CONSOLE_PARAM_CMDS_ENABLED
    register_command("set-param", "Set device parameter value with callback. Usage: set-param <device_id> <param_id> <value>", &set_param_handler);
    register_command("update-param", "Update device parameter value without callback. Usage: update-param <device_id> <param_id> <value>", &update_param_handler);
    register_command("get-param", "Get device parameter value. Usage: get-param <device_id> <param_id>", &get_param_handler);
#endif /* CONFIG_RMNG_CONSOLE_PARAM_CMDS_ENABLED */
}
