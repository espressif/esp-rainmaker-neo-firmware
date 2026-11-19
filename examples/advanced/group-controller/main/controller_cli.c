/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
/*
 * Interactive console for the RainMaker Neo controller.
 *
 * Every command is a thin wrapper over the controller's handle API. The SDK owns the group graph,
 * the data model and the wire formats, so this file is argument handling and printing only: no
 * JSON, no topics, and no subgroup argument to pass around.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_console.h>
#include <esp_rmaker_console.h>
#include <esp_rmaker_controller.h>
#include <esp_rmaker_node.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_types.h>

#include "osal_err.h"
#include "osal_log.h"
#include "sdkconfig.h"

#include "controller_cli.h"

static const char *TAG = "rmng_ctrl_cli";

/* Printing helpers ***********************************************************/

#define VALUE_COL 20

typedef esp_rmaker_error_t (*str_getter_t)(esp_rmaker_handle_t handle, char **out);

/* Fetch a copy into *slot, freeing what *slot held. "" when unset or stale. Free *slot when done. */
static const char *fetch(str_getter_t get, esp_rmaker_handle_t handle, char **slot)
{
    free(*slot);
    *slot = NULL;
    get(handle, slot);
    return *slot ? *slot : "";
}

static const char *node_id(esp_rmaker_controller_node_t node, char **slot)
{
    return fetch(esp_rmaker_controller_node_get_id, node, slot);
}

static const char *subgroup_id(esp_rmaker_controller_subgroup_t sg, char **slot)
{
    return fetch(esp_rmaker_controller_subgroup_get_id, sg, slot);
}

static const char *subgroup_name(esp_rmaker_controller_subgroup_t sg, char **slot)
{
    return fetch(esp_rmaker_controller_subgroup_get_name, sg, slot);
}

static const char *device_id(esp_rmaker_controller_device_t dev, char **slot)
{
    return fetch(esp_rmaker_controller_device_get_id, dev, slot);
}

static const char *device_type(esp_rmaker_controller_device_t dev, char **slot)
{
    return fetch(esp_rmaker_controller_device_get_type, dev, slot);
}

static const char *param_id(esp_rmaker_controller_param_t param, char **slot)
{
    return fetch(esp_rmaker_controller_param_get_id, param, slot);
}

static const char *param_type(esp_rmaker_controller_param_t param, char **slot)
{
    return fetch(esp_rmaker_controller_param_get_type, param, slot);
}

static void val_to_str(esp_rmaker_controller_param_t param, char *out, size_t size)
{
    esp_rmaker_param_val_t val = {0};
    if (esp_rmaker_controller_param_get_val(param, &val) != ESP_RMAKER_OK) {
        snprintf(out, size, "<unset>");
        return;
    }
    switch (val.type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        snprintf(out, size, "%s", val.val.b ? "true" : "false");
        break;
    case RMAKER_VAL_TYPE_INTEGER:
        snprintf(out, size, "%d", val.val.i);
        break;
    case RMAKER_VAL_TYPE_INT64:
        snprintf(out, size, "%" PRId64, (int64_t)val.val.i64);
        break;
    case RMAKER_VAL_TYPE_FLOAT:
        snprintf(out, size, "%g", val.val.f);
        break;
    default:
        snprintf(out, size, "%s", val.val.s ? val.val.s : "");
        break;
    }
    esp_rmaker_val_free(&val);
}

/* The subgroup ids @p node sits under, comma-separated, or "-" when it sits under none. */
static void print_subgroup_ids(esp_rmaker_controller_node_t node)
{
    const char *sep = "";
    char *sg_id = NULL;
    for (esp_rmaker_controller_subgroup_t sg = esp_rmaker_controller_subgroup_first(); sg;
            sg = esp_rmaker_controller_subgroup_next(sg)) {
        if (*subgroup_id(sg, &sg_id) && esp_rmaker_controller_node_in_subgroup(node, sg_id)) {
            printf("%s%s", sep, sg_id);
            sep = ", ";
        }
    }
    printf("%s", *sep ? "" : "-");
    free(sg_id);
}

/* The group as one row per node. */
static void print_group_table(void)
{
    /* Columns size to their longest entry, since node ids range from 22-char base62 to 36-char UUIDs. */
    int nodes = 0, subgroups = 0;
    int node_col = (int)strlen("NODE"), sg_col = 0;
    char *id = NULL;
    char *sg_id = NULL;
    char *name = NULL;
    for (esp_rmaker_controller_node_t n = esp_rmaker_controller_node_first(); n;
            n = esp_rmaker_controller_node_next(n)) {
        int len = (int)strlen(node_id(n, &id));
        node_col = len > node_col ? len : node_col;
        nodes++;
    }
    for (esp_rmaker_controller_subgroup_t sg = esp_rmaker_controller_subgroup_first(); sg;
            sg = esp_rmaker_controller_subgroup_next(sg)) {
        int len = (int)strlen(subgroup_id(sg, &sg_id));
        sg_col = len > sg_col ? len : sg_col;
        subgroups++;
    }
    char *group_id = NULL;
    (void)esp_rmaker_get_stored_group_id(&group_id);
    printf("Group %s - %d node(s), %d subgroup(s)\n\n", group_id ? group_id : "(none)", nodes, subgroups);
    free(group_id);

    printf("  %-*s  %-8s  %s\n", node_col, "NODE", "STATE", "SUBGROUPS");
    for (esp_rmaker_controller_node_t n = esp_rmaker_controller_node_first(); n;
            n = esp_rmaker_controller_node_next(n)) {
        printf("  %-*s  %-8s  ", node_col, node_id(n, &id),
               esp_rmaker_controller_node_is_online(n) ? "online" : "offline");
        print_subgroup_ids(n);
        printf("\n");
    }

    if (subgroups) {
        printf("\n");
        for (esp_rmaker_controller_subgroup_t sg = esp_rmaker_controller_subgroup_first(); sg;
                sg = esp_rmaker_controller_subgroup_next(sg)) {
            printf("  %-*s  %s\n", sg_col, subgroup_id(sg, &sg_id), subgroup_name(sg, &name));
        }
    }
    free(id);
    free(sg_id);
    free(name);
}

/* Width of the param type column, so the values of a node line up under each other. */
static int type_col(esp_rmaker_controller_node_t node)
{
    int col = 0;
    char *type = NULL;
    for (esp_rmaker_controller_device_t d = esp_rmaker_controller_node_device_first(node); d;
            d = esp_rmaker_controller_device_next(d)) {
        for (esp_rmaker_controller_param_t pm = esp_rmaker_controller_device_param_first(d); pm;
                pm = esp_rmaker_controller_param_next(pm)) {
            int len = (int)strlen(param_type(pm, &type));
            col = len > col ? len : col;
        }
    }
    free(type);
    return col;
}

/* One node's devices and services, with a param per line. The param type leads each line because
 * that is what a group write addresses; the quoted name is what 'set' takes. */
static void print_node_state(esp_rmaker_controller_node_t node)
{
    char *id = NULL;
    printf("%s  %-7s  ", node_id(node, &id),
           esp_rmaker_controller_node_is_online(node) ? "online" : "offline");
    free(id);
    print_subgroup_ids(node);
    printf("\n");
    if (!esp_rmaker_controller_node_device_first(node)) {
        char *model = NULL;
        esp_rmaker_controller_node_get_data_model(node, &model);
        printf("  no config held (data model '%s')\n", model ? model : "unknown");
        free(model);
        return;
    }
    char *type = NULL;
    char *obj_id = NULL;
    int col = type_col(node);
    for (esp_rmaker_controller_device_t d = esp_rmaker_controller_node_device_first(node); d;
            d = esp_rmaker_controller_device_next(d)) {
        printf("  %s [%s]%s\n", device_id(d, &obj_id), device_type(d, &type),
               esp_rmaker_controller_device_is_service(d) ? " service" : "");
        for (esp_rmaker_controller_param_t pm = esp_rmaker_controller_device_param_first(d); pm;
                pm = esp_rmaker_controller_param_next(pm)) {
            char value[64];
            val_to_str(pm, value, sizeof(value));
            printf("    %-*s = %-*s \"%s\"%s\n",
                   col, param_type(pm, &type),
                   VALUE_COL, value, param_id(pm, &obj_id),
                   (esp_rmaker_controller_param_get_props(pm) & PROP_FLAG_WRITE) ? "" : " ro");
        }
    }
    free(type);
    free(obj_id);
}

/* Value parsing **************************************************************/

/* Build a value of the param's own data type out of the argument the user typed. */
static bool parse_val(esp_rmaker_controller_param_t param, const char *text, esp_rmaker_param_val_t *out)
{
    switch (esp_rmaker_controller_param_get_data_type(param)) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        *out = esp_rmaker_bool(strcmp(text, "true") == 0 || strcmp(text, "1") == 0);
        return true;
    case RMAKER_VAL_TYPE_INTEGER:
        *out = esp_rmaker_int(atoi(text));
        return true;
    case RMAKER_VAL_TYPE_INT64:
        *out = esp_rmaker_int64(strtoll(text, NULL, 10));
        return true;
    case RMAKER_VAL_TYPE_FLOAT:
        *out = esp_rmaker_float(strtof(text, NULL));
        return true;
    case RMAKER_VAL_TYPE_STRING:
        *out = esp_rmaker_str(text);
        return true;
    default:
        printf("Unsupported param data type\n");
        return false;
    }
}

/* Commands *******************************************************************/

/* group: print the group the controller acts on, as the cloud's group info last set it. */
static int cmd_group(int argc, char **argv)
{
    char *group_id = NULL;
    char *subgroups = NULL;
    esp_rmaker_error_t err = esp_rmaker_get_stored_group_id(&group_id);
    if (err == ESP_RMAKER_NOT_FOUND) {
        printf("group: none - add this node to a group\n");
        return 1;
    }
    if (err != ESP_RMAKER_OK) {
        printf("group: failed (%s)\n", osal_err_strerror(err));
        return 1;
    }
    (void)esp_rmaker_get_stored_subgroups(&subgroups);
    printf("  group_id:  %s\n  subgroups: %s\n  state:     %s\n", group_id,
           (subgroups && subgroups[0]) ? subgroups : "(none)",
           esp_rmaker_controller_is_active() ? "active" : "standby");
    free(group_id);
    free(subgroups);
    return 0;
}

/* standby [on|off]: the device-local standby switch; on standby, set commands are refused. */
static int cmd_standby(int argc, char **argv)
{
    if (argc >= 2) {
        esp_rmaker_controller_set_active(strcmp(argv[1], "on") != 0);
    }
    printf("standby: %s\n", esp_rmaker_controller_is_active() ? "off" : "on");
    return 0;
}

/* sync: rebuild the group graph, hold every node's data model, and seed its state. */
static int cmd_sync(int argc, char **argv)
{
    esp_rmaker_error_t err = esp_rmaker_controller_group_sync();
    if (err != ESP_RMAKER_OK) {
        printf("sync: failed (%s)\n", osal_err_strerror(err));
        return 1;
    }
    for (esp_rmaker_controller_node_t n = esp_rmaker_controller_node_first(); n;
            n = esp_rmaker_controller_node_next(n)) {
        /* An unsupported data model is reported per node by print_node_state(). */
        err = esp_rmaker_controller_node_sync(n);
        if (err != ESP_RMAKER_OK && err != ESP_RMAKER_NOT_SUPPORTED) {
            char *id = NULL;
            printf("sync: %s failed (%s)\n", node_id(n, &id), osal_err_strerror(err));
            free(id);
        }
    }

    print_group_table();
    return 0;
}

/* nodes: reprint the table, with no call to the cloud. */
static int cmd_nodes(int argc, char **argv)
{
    print_group_table();
    return 0;
}

/* state <node_id>: the params of one node. */
static int cmd_state(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: state <node_id>\n");
        return 1;
    }
    esp_rmaker_controller_node_t node = esp_rmaker_controller_node_find(argv[1]);
    if (!node) {
        printf("state: no node '%s'. Run 'sync' first.\n", argv[1]);
        return 1;
    }
    print_node_state(node);
    return 0;
}

/* grouppower <on|off>: the payoff. The group control topic addresses a device type, so this walks
 * the cached data models for every type that holds a Power param, then writes each one. */
#define GROUPPOWER_TYPE_MAX 16

/* Add a copy of @p type to @p types unless it is already there. Returns the new count. */
static int type_list_add(char **types, int count, const char *type)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(types[i], type) == 0) {
            return count;
        }
    }
    if (count == GROUPPOWER_TYPE_MAX) {
        printf("grouppower: more than %d device types, '%s' left out\n", GROUPPOWER_TYPE_MAX, type);
        return count;
    }
    types[count] = strdup(type);
    return types[count] ? count + 1 : count;
}

static bool device_has_power(esp_rmaker_controller_device_t dev)
{
    return esp_rmaker_controller_device_param_by_type(dev, ESP_RMAKER_PARAM_POWER) != 0;
}

/* Every distinct device type in the group that holds a Power param. */
static int collect_power_types(char **types)
{
    int count = 0;
    char *type = NULL;
    for (esp_rmaker_controller_node_t n = esp_rmaker_controller_node_first(); n;
            n = esp_rmaker_controller_node_next(n)) {
        for (esp_rmaker_controller_device_t d = esp_rmaker_controller_node_device_first(n); d;
                d = esp_rmaker_controller_device_next(d)) {
            if (*device_type(d, &type) && device_has_power(d)) {
                count = type_list_add(types, count, type);
            }
        }
    }
    free(type);
    return count;
}

static int cmd_grouppower(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: grouppower <on|off>\n");
        return 1;
    }
    bool on = strcmp(argv[1], "on") == 0;

    char *types[GROUPPOWER_TYPE_MAX];
    int count = collect_power_types(types);
    if (!count) {
        printf("grouppower: no device type in the group holds a Power param. Run 'sync' first.\n");
        return 1;
    }

    int failed = 0;
    for (int i = 0; i < count; i++) {
        esp_rmaker_error_t err = esp_rmaker_controller_group_write(NULL, types[i],
                                 ESP_RMAKER_PARAM_POWER, esp_rmaker_bool(on));
        if (err != ESP_RMAKER_OK) {
            printf("grouppower: %s failed (%s)\n", types[i], osal_err_strerror(err));
            failed++;
            continue;
        }
        printf("grouppower %s: published to %s\n", argv[1], types[i]);
    }
    for (int i = 0; i < count; i++) {
        free(types[i]);
    }
    return failed ? 1 : 0;
}

/* set <node_id> <device> <param> <value>: one param on one node. */
static int cmd_set(int argc, char **argv)
{
    if (argc < 5) {
        printf("usage: set <node_id> <device_name> <param_name> <value>\n");
        return 1;
    }
    esp_rmaker_controller_node_t node = esp_rmaker_controller_node_find(argv[1]);
    if (!node) {
        printf("set: no node '%s'\n", argv[1]);
        return 1;
    }
    esp_rmaker_controller_device_t dev = esp_rmaker_controller_node_device_by_id(node, argv[2]);
    if (!dev) {
        printf("set: node '%s' has no device '%s'. Run 'sync' first.\n", argv[1], argv[2]);
        return 1;
    }
    esp_rmaker_controller_param_t param = esp_rmaker_controller_device_param_by_id(dev, argv[3]);
    if (!param) {
        printf("set: device '%s' has no param '%s'\n", argv[2], argv[3]);
        return 1;
    }
    esp_rmaker_param_val_t val;
    if (!parse_val(param, argv[4], &val)) {
        return 1;
    }
    esp_rmaker_error_t err = esp_rmaker_controller_param_write(param, val);
    if (err != ESP_RMAKER_OK) {
        printf("set: failed (%s)\n", osal_err_strerror(err));
        return 1;
    }
    printf("set: published to %s\n", argv[1]);
    return 0;
}

/* groupset <device_type> <param_type> <value> [subgroup_id]: one publish for the whole group. */
static int cmd_groupset(int argc, char **argv)
{
    if (argc < 4) {
        printf("usage: groupset <device_type> <param_type> <value> [subgroup_id]\n");
        return 1;
    }
    /* No param handle here, so the value type comes from the argument's shape. A group write takes
     * bare type strings and cannot check the target param. */
    esp_rmaker_param_val_t val;
    if (strcmp(argv[3], "true") == 0 || strcmp(argv[3], "false") == 0) {
        val = esp_rmaker_bool(strcmp(argv[3], "true") == 0);
    } else if (argv[3][0] >= '0' && argv[3][0] <= '9') {
        val = esp_rmaker_int(atoi(argv[3]));
    } else {
        val = esp_rmaker_str(argv[3]);
    }
    const char *subgroup = argc > 4 ? argv[4] : NULL;
    esp_rmaker_error_t err = esp_rmaker_controller_group_write(subgroup, argv[1], argv[2], val);
    if (err != ESP_RMAKER_OK) {
        printf("groupset: failed (%s)\n", osal_err_strerror(err));
        return 1;
    }
    printf("groupset: published to %s\n", subgroup ? subgroup : "the whole group");
    return 0;
}

/* watch: print the value and structural changes the SDK applies. The cache follows the group
 * either way, because the subscription belongs to 'sync'; this only attaches the printing. */

static void on_param_change(esp_rmaker_controller_param_t param, void *priv)
{
    char value[64];
    char *id = NULL;
    char *dev_id = NULL;
    char *par_id = NULL;
    val_to_str(param, value, sizeof(value));
    esp_rmaker_controller_device_t dev = esp_rmaker_controller_param_get_device(param);
    esp_rmaker_controller_node_t node = esp_rmaker_controller_device_get_node(dev);
    printf("[watch] %s: %s.%s = %s\n", node ? node_id(node, &id) : "?", dev ? device_id(dev, &dev_id) : "?",
           param_id(param, &par_id), value);
    free(id);
    free(dev_id);
    free(par_id);
}

/* Print one change. A REMOVED handle is stale, so it prints as its number. */
static void print_change(const esp_rmaker_controller_change_t *c)
{
    static const char *const types[] = { "added", "updated", "removed" };
    static const char *const kinds[] = { "node", "subgroup", "device", "param" };
    char *id = NULL;
    switch (c->kind) {
    case ESP_RMAKER_CONTROLLER_KIND_NODE:
        node_id(c->handle, &id);
        break;
    case ESP_RMAKER_CONTROLLER_KIND_SUBGROUP:
        subgroup_id(c->handle, &id);
        break;
    case ESP_RMAKER_CONTROLLER_KIND_DEVICE:
        device_id(c->handle, &id);
        break;
    case ESP_RMAKER_CONTROLLER_KIND_PARAM:
        param_id(c->handle, &id);
        break;
    }
    if (id) {
        printf("[watch] %s %s %s\n", types[c->type], kinds[c->kind], id);
    } else {
        printf("[watch] %s %s #%08x\n", types[c->type], kinds[c->kind], (unsigned)c->handle);
    }
    free(id);
}

static void on_changes(const esp_rmaker_controller_change_t *changes, size_t count, void *priv)
{
    for (size_t i = 0; i < count; i++) {
        print_change(&changes[i]);
    }
}

static int cmd_watch(int argc, char **argv)
{
    bool off = argc > 1 && strcmp(argv[1], "off") == 0;
    if (off) {
        esp_rmaker_controller_unwatch_params();
        esp_rmaker_controller_unwatch_changes();
        printf("watch off - changes still apply to the cache, they are not printed\n");
        return 0;
    }
    esp_rmaker_controller_watch_params(on_param_change, NULL);
    esp_rmaker_controller_watch_changes(on_changes, NULL);
    printf("watch on - changes are printed as they arrive\n");
    return 0;
}

/* raw <path_suffix>: the escape hatch, for debugging. */
static esp_rmaker_error_t print_body(void *ctx, const char *data, size_t len)
{
    (void)ctx;
    printf("%.*s\n", (int)len, data ? data : "");
    return ESP_RMAKER_OK;
}

static int cmd_raw(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: raw <path_suffix>\n");
        return 1;
    }
    esp_rmaker_error_t err = esp_rmaker_controller_api_get(argv[1], print_body, NULL);
    if (err != ESP_RMAKER_OK) {
        printf("raw: failed (%s)\n", osal_err_strerror(err));
        return 1;
    }
    return 0;
}

static void register_cmd(const char *cmd, const char *help, esp_console_cmd_func_t func)
{
    const esp_console_cmd_t c = { .command = cmd, .help = help, .hint = NULL, .func = func };
    esp_console_cmd_register(&c);
}

void controller_cli_init(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "group-controller>";

#ifdef ESP_PLATFORM
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t repl_err = esp_console_new_repl_uart(&uart_config, &repl_config, &repl);
#else
    /* The POSIX console shim reads stdin. */
    esp_err_t repl_err = esp_console_new_repl_stdio(&repl_config, &repl);
#endif
    if (repl_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to create console REPL");
        return;
    }

    esp_console_register_help_command();
#if CONFIG_ESP_RMAKER_NEO_CONSOLE_ENABLE
    /* The SDK console has no line editing and a 255-char line limit, so this example runs its own
     * REPL and registers only the SDK built-ins. */
    esp_rmaker_register_commands();
#endif
    register_cmd("group",    "Print the controller's group, subgroups and standby state", cmd_group);
    register_cmd("standby",  "standby [on|off] - local standby: refuse the write commands", cmd_standby);
    register_cmd("sync",     "Fetch the group graph and every node's data model, then print it", cmd_sync);
    register_cmd("nodes",    "Reprint the cached group as one row per node, with no cloud call", cmd_nodes);
    register_cmd("state",    "state <node_id> - print one node's cached params", cmd_state);
    register_cmd("grouppower", "grouppower <on|off> - power every device type in the group", cmd_grouppower);
    register_cmd("set",      "set <node_id> <device> <param> <value> - write one param", cmd_set);
    register_cmd("groupset", "groupset <device_type> <param_type> <value> [subgroup_id]", cmd_groupset);
    register_cmd("watch",    "watch [off] - print the changes the SDK applies. The cache updates either way", cmd_watch);
    register_cmd("raw",      "raw <path_suffix> - signed GET on the REST data APIs", cmd_raw);

    esp_console_start_repl(repl);
}
