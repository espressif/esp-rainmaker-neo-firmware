/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file rm_mirror_table.c
 * @brief Mapping table lookups and device-type rule matching.
 */

#include <string.h>

#include "rm_mirror_engine.h"

const rm_mirror_capability_t *rm_mirror_table_find_capability(const char *param_type)
{
    if (!param_type) {
        return NULL;
    }
    for (size_t i = 0; i < rm_mirror_n_capabilities; i++) {
        if (strcmp(rm_mirror_capabilities[i].param_type, param_type) == 0) {
            return &rm_mirror_capabilities[i];
        }
    }
    return NULL;
}

const rm_mirror_write_cmd_t *rm_mirror_table_find_write_cmd(const rm_mirror_capability_t *cap,
        int32_t matter_val)
{
    if (!cap || !cap->write_cmds) {
        return NULL;
    }
    const rm_mirror_write_cmd_t *unconditional = NULL;
    for (size_t i = 0; i < cap->n_write_cmds; i++) {
        const rm_mirror_write_cmd_t *cmd = &cap->write_cmds[i];
        if (!cmd->has_when) {
            unconditional = cmd;
        } else if (cmd->when == matter_val) {
            return cmd;
        }
    }
    return unconditional;
}

const rm_mirror_devtype_rule_t *rm_mirror_table_match_rule(const char *device_type,
        rm_mirror_param_present_fn present, void *ctx)
{
    if (!device_type || !present) {
        return NULL;
    }
    for (size_t i = 0; i < rm_mirror_n_device_entries; i++) {
        const rm_mirror_device_entry_t *entry = &rm_mirror_device_entries[i];
        if (strcmp(entry->device_type, device_type) != 0) {
            continue;
        }
        /* Rules are ordered most-specific-first: first full subset match wins */
        for (size_t r = 0; r < entry->n_rules; r++) {
            const rm_mirror_devtype_rule_t *rule = &entry->rules[r];
            bool all_present = true;
            for (size_t p = 0; p < rule->n_params; p++) {
                if (!present(rule->param_types[p], ctx)) {
                    all_present = false;
                    break;
                }
            }
            if (all_present) {
                return rule;
            }
        }
        return NULL;
    }
    return NULL;
}
