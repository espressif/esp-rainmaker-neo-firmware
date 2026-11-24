/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file controller_parse.c
 * @brief Pure parsing helpers: the base-URL split, the cloud response bodies and the shadow state.
 *
 * Kept apart from the layers that reach the network, so the unit tests link these without the
 * agent or the signer behind them.
 */

#include <stdio.h>
#include <string.h>

#include "json_parser.h"

#include "osal_log.h"

#include "node_internal.h"

#include "controller_internal.h"

static const char *TAG = "rmng_controller";

/* Reports whether the string @p key holds fits @p size with its NUL. false when @p key is missing. */
static bool __string_fits(jparse_ctx_t *jctx, const char *key, size_t size, bool *present)
{
    int len = 0;
    *present = json_obj_get_strlen(jctx, key, &len) == OS_SUCCESS;
    return *present && len >= 0 && (size_t)len < size;
}

/* Base URL ********************************************************************/

void controller_parse_base_url(const char *url, char *host, size_t host_len,
                               char *base_path, size_t base_path_len)
{
    if (!host || host_len == 0 || !base_path || base_path_len == 0) {
        return;
    }
    host[0] = '\0';
    base_path[0] = '\0';
    if (!url) {
        return;
    }

    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    const char *slash = strchr(p, '/');
    size_t len = slash ? (size_t)(slash - p) : strlen(p);
    if (len >= host_len) {
        len = host_len - 1;
    }
    memcpy(host, p, len);
    host[len] = '\0';

    if (slash) {
        snprintf(base_path, base_path_len, "%s", slash);
        /* Strip a trailing '/' so callers can append "/v1/..." cleanly. */
        size_t bl = strlen(base_path);
        if (bl > 0 && base_path[bl - 1] == '/') {
            base_path[bl - 1] = '\0';
        }
    }
}

/* Expired credentials *********************************************************/

bool controller_is_expired_credentials(const char *body, size_t body_len)
{
    static const char needle[] = "expired";
    const size_t n = sizeof(needle) - 1;
    if (!body) {
        return false;
    }
    for (size_t i = 0; i + n <= body_len; i++) {
        if (memcmp(body + i, needle, n) == 0) {
            return true;
        }
    }
    return false;
}

/* GroupInfo *******************************************************************/

static esp_rmaker_error_t __parse_subgroups(jparse_ctx_t *jctx,
        const controller_group_info_cbs_t *cbs, void *ctx)
{
    int count = 0;
    if (json_obj_get_array(jctx, "subgroups", &count) != OS_SUCCESS) {
        /* A group with no subgroups is the common case, not an error. */
        return ESP_RMAKER_OK;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    for (int i = 0; i < count && err == ESP_RMAKER_OK; i++) {
        if (json_arr_get_object(jctx, (uint32_t)i) != OS_SUCCESS) {
            continue;
        }
        char sg_id[RMAKER_SUBGROUP_BUFFER_SIZE] = {0};
        char sg_name[CONTROLLER_SUBGROUP_NAME_MAX_LEN] = {0};
        if (json_obj_get_string(jctx, "subgroup_id", sg_id, sizeof(sg_id)) != OS_SUCCESS) {
            json_arr_leave_object(jctx);
            continue;
        }
        bool named = false;
        bool fits = __string_fits(jctx, "subgroup_name", sizeof(sg_name), &named);
        if (fits) {
            json_obj_get_string(jctx, "subgroup_name", sg_name, sizeof(sg_name));
        }
        /* The members still count, because the shadow name follows the subgroup path. */
        if (named && !fits) {
            OSAL_LOGW(TAG, "Subgroup '%s' has a name that does not fit; skipping the subgroup", sg_id);
        } else if (cbs->subgroup) {
            err = cbs->subgroup(ctx, sg_id, sg_name);
        }

        int members = 0;
        if (err == ESP_RMAKER_OK && cbs->member &&
                json_obj_get_array(jctx, "node_ids", &members) == OS_SUCCESS) {
            for (int j = 0; j < members && err == ESP_RMAKER_OK; j++) {
                char node_id[RMAKER_THING_NAME_BUFFER_SIZE];
                if (json_arr_get_string(jctx, (uint32_t)j, node_id, sizeof(node_id)) != OS_SUCCESS) {
                    continue;
                }
                err = cbs->member(ctx, sg_id, node_id);
            }
            json_obj_leave_array(jctx);
        }
        json_arr_leave_object(jctx);
    }
    json_obj_leave_array(jctx);
    return err;
}

esp_rmaker_error_t controller_parse_group_info(const char *body, size_t body_len,
        const controller_group_info_cbs_t *cbs, void *ctx)
{
    if (!body || !cbs) {
        return ESP_RMAKER_INVALID_ARG;
    }

    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, body, (int)body_len) != OS_SUCCESS) {
        return ESP_RMAKER_INVALID_ARG;
    }
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    char group_name[CONTROLLER_GROUP_NAME_MAX_LEN];
    bool named = false;
    if (__string_fits(&jctx, "group_name", sizeof(group_name), &named)) {
        json_obj_get_string(&jctx, "group_name", group_name, sizeof(group_name));
        err = cbs->group ? cbs->group(ctx, group_name) : ESP_RMAKER_OK;
    } else if (named) {
        OSAL_LOGW(TAG, "The group name does not fit; keeping it empty");
    }
    if (err != ESP_RMAKER_OK) {
        json_parse_end(&jctx);
        return err;
    }

    int count = 0;
    if (json_obj_get_array(&jctx, "node_ids", &count) != OS_SUCCESS) {
        json_parse_end(&jctx);
        return ESP_RMAKER_NOT_FOUND;
    }

    for (int i = 0; i < count && err == ESP_RMAKER_OK; i++) {
        char node_id[RMAKER_THING_NAME_BUFFER_SIZE];
        /* A non-string entry is skipped rather than failing the whole listing. */
        if (json_arr_get_string(&jctx, (uint32_t)i, node_id, sizeof(node_id)) != OS_SUCCESS) {
            continue;
        }
        if (cbs->node) {
            err = cbs->node(ctx, node_id);
        }
    }
    json_obj_leave_array(&jctx);

    if (err == ESP_RMAKER_OK) {
        err = __parse_subgroups(&jctx, cbs, ctx);
    }
    json_parse_end(&jctx);
    return err;
}

/* Node config *****************************************************************/

esp_rmaker_val_type_t controller_parse_data_type(const char *data_type)
{
    if (!data_type) {
        return RMAKER_VAL_TYPE_INVALID;
    }
    if (strcmp(data_type, "bool") == 0) {
        return RMAKER_VAL_TYPE_BOOLEAN;
    }
    if (strcmp(data_type, "int") == 0) {
        return RMAKER_VAL_TYPE_INTEGER;
    }
    if (strcmp(data_type, "int64") == 0) {
        return RMAKER_VAL_TYPE_INT64;
    }
    if (strcmp(data_type, "float") == 0) {
        return RMAKER_VAL_TYPE_FLOAT;
    }
    if (strcmp(data_type, "string") == 0) {
        return RMAKER_VAL_TYPE_STRING;
    }
    if (strcmp(data_type, "object") == 0) {
        return RMAKER_VAL_TYPE_OBJECT;
    }
    if (strcmp(data_type, "array") == 0) {
        return RMAKER_VAL_TYPE_ARRAY;
    }
    return RMAKER_VAL_TYPE_INVALID;
}

esp_rmaker_ui_type_t controller_parse_ui_type(const char *ui_type)
{
    if (!ui_type) {
        return RMAKER_UI_TYPE_NONE;
    }
    if (strcmp(ui_type, ESP_RMAKER_UI_TOGGLE) == 0) {
        return RMAKER_UI_TYPE_TOGGLE;
    }
    if (strcmp(ui_type, ESP_RMAKER_UI_SLIDER) == 0) {
        return RMAKER_UI_TYPE_SLIDER;
    }
    if (strcmp(ui_type, ESP_RMAKER_UI_DROPDOWN) == 0) {
        return RMAKER_UI_TYPE_DROPDOWN;
    }
    if (strcmp(ui_type, ESP_RMAKER_UI_TEXT) == 0) {
        return RMAKER_UI_TYPE_TEXT;
    }
    if (strcmp(ui_type, ESP_RMAKER_UI_HUE_SLIDER) == 0) {
        return RMAKER_UI_TYPE_HUE_SLIDER;
    }
    if (strcmp(ui_type, ESP_RMAKER_UI_HUE_CIRCLE) == 0) {
        return RMAKER_UI_TYPE_HUE_CIRCLE;
    }
    if (strcmp(ui_type, ESP_RMAKER_UI_PUSHBUTTON) == 0) {
        return RMAKER_UI_TYPE_PUSHBUTTON;
    }
    if (strcmp(ui_type, ESP_RMAKER_UI_TRIGGER) == 0) {
        return RMAKER_UI_TYPE_TRIGGER;
    }
    if (strcmp(ui_type, ESP_RMAKER_UI_HIDDEN) == 0) {
        return RMAKER_UI_TYPE_HIDDEN;
    }
    if (strcmp(ui_type, ESP_RMAKER_UI_QR_SCAN) == 0) {
        return RMAKER_UI_TYPE_QR_SCAN;
    }
    return RMAKER_UI_TYPE_UNKNOWN;
}

uint8_t controller_parse_property_flag(const char *property)
{
    if (!property) {
        return 0;
    }
    if (strcmp(property, "read") == 0) {
        return PROP_FLAG_READ;
    }
    if (strcmp(property, "write") == 0) {
        return PROP_FLAG_WRITE;
    }
    if (strcmp(property, "time_series") == 0) {
        return PROP_FLAG_TIME_SERIES;
    }
    if (strcmp(property, "simple_time_series") == 0) {
        return PROP_FLAG_TIME_SERIES;
    }
    if (strcmp(property, "ts_cumulative") == 0) {
        return PROP_FLAG_TS_CUMULATIVE;
    }
    if (strcmp(property, "indexed") == 0) {
        return PROP_FLAG_INDEXED;
    }
    if (strcmp(property, "persist") == 0) {
        return PROP_FLAG_PERSIST;
    }
    return 0;
}

esp_rmaker_error_t controller_parse_data_model(const char *body, size_t body_len,
        char *out, size_t out_len)
{
    if (!body || !out || out_len == 0) {
        return ESP_RMAKER_INVALID_ARG;
    }
    out[0] = '\0';
    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, body, (int)body_len) != OS_SUCCESS) {
        return ESP_RMAKER_INVALID_ARG;
    }
    /* A body with no data_model predates the field; treat it as the default model. */
    if (json_obj_get_string(&jctx, "data_model", out, (int)out_len) != OS_SUCCESS) {
        snprintf(out, out_len, "%s", CONTROLLER_DATA_MODEL_DEFAULT);
    }
    json_parse_end(&jctx);
    return ESP_RMAKER_OK;
}

/* Read an id, and the legacy "name" only when "id" is missing. An id that does not fit is skipped. */
static bool __read_id(jparse_ctx_t *jctx, char *out, size_t out_len)
{
    bool present = false;
    const char *key = "id";
    bool fits = __string_fits(jctx, key, out_len, &present);
    if (!present) {
        key = "name";
        fits = __string_fits(jctx, key, out_len, &present);
    }
    if (present && !fits) {
        OSAL_LOGW(TAG, "Skipping a device or param whose %s does not fit %d bytes", key, (int)out_len);
    }
    return fits && json_obj_get_string(jctx, key, out, (int)out_len) == OS_SUCCESS;
}

/* Read an optional string field into @p out. false when the field is present but does not fit. */
static bool __read_optional(jparse_ctx_t *jctx, const char *key, char *out, size_t out_len, bool *present)
{
    if (!__string_fits(jctx, key, out_len, present)) {
        return !*present;
    }
    return json_obj_get_string(jctx, key, out, (int)out_len) == OS_SUCCESS;
}

static void __parse_properties(jparse_ctx_t *jctx, ctrl_param_t *param)
{
    int count = 0;
    param->prop_flags = 0;
    if (json_obj_get_array(jctx, "properties", &count) != OS_SUCCESS) {
        return;
    }
    for (int i = 0; i < count; i++) {
        char property[24];
        if (json_arr_get_string(jctx, (uint32_t)i, property, sizeof(property)) == OS_SUCCESS) {
            param->prop_flags |= controller_parse_property_flag(property);
        }
    }
    json_obj_leave_array(jctx);
}

static void __parse_bounds(jparse_ctx_t *jctx, ctrl_param_t *param)
{
    /* Only numeric params carry bounds, so nothing here allocates a value. */
    if (param->data_type != RMAKER_VAL_TYPE_INTEGER && param->data_type != RMAKER_VAL_TYPE_INT64 &&
            param->data_type != RMAKER_VAL_TYPE_FLOAT) {
        controller_model_param_set_bounds(param, NULL);
        return;
    }
    if (json_obj_get_object(jctx, "bounds") != OS_SUCCESS) {
        controller_model_param_set_bounds(param, NULL);
        return;
    }
    esp_rmaker_param_bounds_t bounds = {0};
    bool got = false;
    if (esp_rmaker_parse_val_from_object(jctx, "min", param->data_type, &bounds.min) == ESP_RMAKER_OK) {
        got = true;
    }
    if (esp_rmaker_parse_val_from_object(jctx, "max", param->data_type, &bounds.max) == ESP_RMAKER_OK) {
        got = true;
    }
    if (esp_rmaker_parse_val_from_object(jctx, "step", param->data_type, &bounds.step) == ESP_RMAKER_OK) {
        got = true;
    }
    json_obj_leave_object(jctx);
    controller_model_param_set_bounds(param, got ? &bounds : NULL);
}

static bool __str_equal(const char *a, const char *b)
{
    return (!a || !b) ? a == b : strcmp(a, b) == 0;
}

static bool __val_equal(const esp_rmaker_param_val_t *a, const esp_rmaker_param_val_t *b)
{
    if (a->type != b->type) {
        return false;
    }
    switch (a->type) {
    case RMAKER_VAL_TYPE_INTEGER:
        return a->val.i == b->val.i;
    case RMAKER_VAL_TYPE_INT64:
        return a->val.i64 == b->val.i64;
    case RMAKER_VAL_TYPE_FLOAT:
        return a->val.f == b->val.f;
    default:
        return true;
    }
}

static bool __bounds_equal(const esp_rmaker_param_bounds_t *a, const esp_rmaker_param_bounds_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return __val_equal(&a->min, &b->min) && __val_equal(&a->max, &b->max) && __val_equal(&a->step, &b->step);
}

/* The config fields of a param, kept to tell whether a re-fetch changed them. */
typedef struct {
    esp_rmaker_val_type_t data_type;
    esp_rmaker_ui_type_t ui_type;
    uint8_t prop_flags;
    bool has_bounds;
    esp_rmaker_param_bounds_t bounds;
} param_fields_t;

static void __save_fields(const ctrl_param_t *param, param_fields_t *out)
{
    out->data_type = param->data_type;
    out->ui_type = param->ui_type;
    out->prop_flags = param->prop_flags;
    out->has_bounds = param->bounds != NULL;
    if (out->has_bounds) {
        out->bounds = *param->bounds;
    }
}

static bool __fields_changed(const ctrl_param_t *param, const param_fields_t *before)
{
    return param->data_type != before->data_type || param->ui_type != before->ui_type ||
           param->prop_flags != before->prop_flags ||
           !__bounds_equal(param->bounds, before->has_bounds ? &before->bounds : NULL);
}

/* Take the survivor for @p id from @p old, or allocate a new param and report it ADDED. */
static ctrl_param_t *__place_param(ctrl_device_t *dev, ctrl_param_t **old, const char *id, bool *survivor,
                                   ctrl_changes_t *changes)
{
    ctrl_param_t *param = controller_model_param_take(old, id);
    *survivor = param != NULL;
    if (param) {
        controller_model_param_append(dev, param);
        return param;
    }
    param = controller_model_param_add(dev, id);
    if (param) {
        controller_changes_add(changes, ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_PARAM,
                               param->obj.handle, dev->obj.handle);
    }
    return param;
}

/* Free a detached param list, and report each param REMOVED. */
static void __drop_params(ctrl_param_t *old, ctrl_device_t *dev, ctrl_changes_t *changes)
{
    for (ctrl_param_t *param = old; param; param = param->next) {
        controller_changes_add(changes, ESP_RMAKER_CONTROLLER_REMOVED, ESP_RMAKER_CONTROLLER_KIND_PARAM,
                               param->obj.handle, dev->obj.handle);
    }
    controller_model_free_param_list(old);
}

static esp_rmaker_error_t __parse_params(jparse_ctx_t *jctx, ctrl_device_t *dev, ctrl_changes_t *changes)
{
    ctrl_param_t *old = dev->params;
    dev->params = NULL;
    int count = 0;
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    if (json_obj_get_array(jctx, "params", &count) != OS_SUCCESS) {
        count = -1;
    }
    for (int i = 0; i < count && err == ESP_RMAKER_OK; i++) {
        if (json_arr_get_object(jctx, (uint32_t)i) != OS_SUCCESS) {
            continue;
        }
        char id[CONTROLLER_CFG_ID_MAX_LEN];
        char type[CONTROLLER_CFG_TYPE_MAX_LEN];
        bool has_type = false;
        if (!__read_id(jctx, id, sizeof(id))) {
            json_arr_leave_object(jctx);
            continue;
        }
        if (!__read_optional(jctx, "type", type, sizeof(type), &has_type)) {
            OSAL_LOGW(TAG, "Skipping param '%s': its type does not fit", id);
            json_arr_leave_object(jctx);
            continue;
        }
        if (controller_model_param_by_id(dev, id)) {
            OSAL_LOGW(TAG, "Skipping a second param '%s'", id);
            json_arr_leave_object(jctx);
            continue;
        }
        bool survivor = false;
        ctrl_param_t *param = __place_param(dev, &old, id, &survivor, changes);
        if (!param) {
            json_arr_leave_object(jctx);
            err = ESP_RMAKER_NO_MEM;
            break;
        }

        param_fields_t before;
        __save_fields(param, &before);
        bool type_changed = !__str_equal(param->type, has_type ? type : NULL);
        controller_model_param_set_type(param, has_type ? type : NULL);
        char data_type[16];
        if (json_obj_get_string(jctx, "data_type", data_type, sizeof(data_type)) == OS_SUCCESS) {
            param->data_type = controller_parse_data_type(data_type);
        } else {
            param->data_type = RMAKER_VAL_TYPE_INVALID;
        }
        char ui_type[CONTROLLER_CFG_TYPE_MAX_LEN];
        bool has_ui_type = false;
        if (!__read_optional(jctx, "ui_type", ui_type, sizeof(ui_type), &has_ui_type)) {
            param->ui_type = RMAKER_UI_TYPE_UNKNOWN;
        } else {
            param->ui_type = has_ui_type ? controller_parse_ui_type(ui_type) : RMAKER_UI_TYPE_NONE;
        }
        __parse_properties(jctx, param);
        __parse_bounds(jctx, param);
        if (survivor && (type_changed || __fields_changed(param, &before))) {
            controller_changes_add(changes, ESP_RMAKER_CONTROLLER_UPDATED, ESP_RMAKER_CONTROLLER_KIND_PARAM,
                                   param->obj.handle, dev->obj.handle);
        }
        json_arr_leave_object(jctx);
    }
    if (count >= 0) {
        json_obj_leave_array(jctx);
    }
    __drop_params(old, dev, changes);
    return err;
}

/* Take the survivor for @p id from @p old, or allocate a new device and report it ADDED. */
static ctrl_device_t *__place_device(ctrl_node_t *node, ctrl_device_t **old, const char *id, bool *survivor,
                                     ctrl_changes_t *changes)
{
    ctrl_device_t *dev = controller_model_device_take(old, id);
    *survivor = dev != NULL;
    if (dev) {
        controller_model_device_append(node, dev);
        return dev;
    }
    dev = controller_model_device_add(node, id);
    if (dev) {
        controller_changes_add(changes, ESP_RMAKER_CONTROLLER_ADDED, ESP_RMAKER_CONTROLLER_KIND_DEVICE,
                               dev->obj.handle, node->obj.handle);
    }
    return dev;
}

static esp_rmaker_error_t __parse_device_array(jparse_ctx_t *jctx, ctrl_node_t *node, ctrl_device_t **old,
        const char *key, bool is_service, ctrl_changes_t *changes)
{
    int count = 0;
    if (json_obj_get_array(jctx, key, &count) != OS_SUCCESS) {
        return ESP_RMAKER_OK;
    }
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    for (int i = 0; i < count && err == ESP_RMAKER_OK; i++) {
        if (json_arr_get_object(jctx, (uint32_t)i) != OS_SUCCESS) {
            continue;
        }
        char id[CONTROLLER_CFG_ID_MAX_LEN];
        char type[CONTROLLER_CFG_TYPE_MAX_LEN];
        char primary[CONTROLLER_CFG_ID_MAX_LEN];
        bool has_type = false;
        bool has_primary = false;
        if (!__read_id(jctx, id, sizeof(id))) {
            json_arr_leave_object(jctx);
            continue;
        }
        if (!__read_optional(jctx, "type", type, sizeof(type), &has_type) ||
                !__read_optional(jctx, "primary", primary, sizeof(primary), &has_primary)) {
            OSAL_LOGW(TAG, "Skipping device '%s': its type or primary does not fit", id);
            json_arr_leave_object(jctx);
            continue;
        }
        if (controller_model_device_by_id(node, id)) {
            OSAL_LOGW(TAG, "Skipping a second device '%s'", id);
            json_arr_leave_object(jctx);
            continue;
        }
        bool survivor = false;
        ctrl_device_t *dev = __place_device(node, old, id, &survivor, changes);
        if (!dev) {
            json_arr_leave_object(jctx);
            err = ESP_RMAKER_NO_MEM;
            break;
        }
        bool updated = survivor && (dev->is_service != is_service || !__str_equal(dev->type, has_type ? type : NULL) ||
                                    !__str_equal(dev->primary, has_primary ? primary : NULL));
        if (updated) {
            controller_changes_add(changes, ESP_RMAKER_CONTROLLER_UPDATED, ESP_RMAKER_CONTROLLER_KIND_DEVICE,
                                   dev->obj.handle, node->obj.handle);
        }
        dev->is_service = is_service;
        controller_model_device_set_type(dev, has_type ? type : NULL);
        controller_model_device_set_primary(dev, has_primary ? primary : NULL);
        err = __parse_params(jctx, dev, changes);
        json_arr_leave_object(jctx);
    }
    json_obj_leave_array(jctx);
    return err;
}

esp_rmaker_error_t controller_parse_node_config(ctrl_node_t *node, const char *body, size_t body_len)
{
    if (!node || !body) {
        return ESP_RMAKER_INVALID_ARG;
    }
    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, body, (int)body_len) != OS_SUCCESS) {
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Survivors move from the old list to the new one, and what stays behind is freed. */
    ctrl_changes_t changes;
    ctrl_changes_t *collect = controller_changes_begin(&changes);
    ctrl_device_t *old = node->devices;
    node->devices = NULL;
    esp_rmaker_error_t err = __parse_device_array(&jctx, node, &old, "devices", false, collect);
    if (err == ESP_RMAKER_OK) {
        err = __parse_device_array(&jctx, node, &old, "services", true, collect);
    }
    json_parse_end(&jctx);
    /* A dropped device takes its params with it, so they get no entries of their own. */
    for (ctrl_device_t *dev = old; dev; dev = dev->next) {
        controller_changes_add(collect, ESP_RMAKER_CONTROLLER_REMOVED, ESP_RMAKER_CONTROLLER_KIND_DEVICE,
                               dev->obj.handle, node->obj.handle);
    }
    controller_model_free_device_list(old);
    if (err == ESP_RMAKER_OK) {
        node->config_held = true;
        node->config_dirty = false;
    }
    controller_changes_commit(collect);
    return err;
}

/* Reported shadow state *******************************************************/

/* Record the change token without interpreting it: the docs disagree on whether it is a hex string
 * or a Unix timestamp, and equality is all the SDK needs. */
static bool __read_ncfg_ver(jparse_ctx_t *jctx, ctrl_node_t *node)
{
    char token[CONTROLLER_NCFG_VER_MAX_LEN];
    if (json_obj_get_string(jctx, "ncfg_ver", token, sizeof(token)) != OS_SUCCESS) {
        int64_t as_int = 0;
        if (json_obj_get_int64(jctx, "ncfg_ver", &as_int) != OS_SUCCESS) {
            return false;
        }
        snprintf(token, sizeof(token), "%lld", (long long)as_int);
    }
    if (strcmp(node->ncfg_ver, token) == 0) {
        return false;
    }
    bool was_set = node->ncfg_ver[0] != '\0';
    snprintf(node->ncfg_ver, sizeof(node->ncfg_ver), "%s", token);
    /* The first token seen is a baseline, not a change. */
    return was_set;
}

static bool __apply_param(jparse_ctx_t *jctx, ctrl_param_t *param)
{
    esp_rmaker_param_val_t val = {0};
    if (esp_rmaker_parse_val_from_object(jctx, param->id, param->data_type, &val) != ESP_RMAKER_OK) {
        return false;
    }
    if (param->val_valid && esp_rmaker_val_compare(&param->val, &val, RMAKER_VAL_COMPARE_EQ) == ESP_RMAKER_OK) {
        esp_rmaker_val_free(&val);
        return false;
    }
    if (param->val_valid) {
        esp_rmaker_val_free(&param->val);
    }
    param->val = val;
    param->val_valid = true;
    return true;
}

esp_rmaker_error_t controller_parse_reported_state(ctrl_node_t *node, const char *body, size_t body_len,
        controller_state_delta_t *delta)
{
    if (!node || !body) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (delta) {
        delta->num_changed = 0;
        delta->ncfg_ver_moved = false;
        delta->online_moved = false;
    }

    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, body, (int)body_len) != OS_SUCCESS) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (json_obj_get_object(&jctx, "state") != OS_SUCCESS) {
        json_parse_end(&jctx);
        return ESP_RMAKER_NOT_FOUND;
    }
    /* Only the reported plane is read. The same topic carries desired-state writes, which are a
     * request rather than the node's state. */
    if (json_obj_get_object(&jctx, "reported") != OS_SUCCESS) {
        json_obj_leave_object(&jctx);
        json_parse_end(&jctx);
        return ESP_RMAKER_NOT_FOUND;
    }

    bool online = false;
    if (json_obj_get_bool(&jctx, "online", &online) == OS_SUCCESS) {
        if (online != node->online && delta) {
            delta->online_moved = true;
        }
        node->online = online;
    }
    bool moved = __read_ncfg_ver(&jctx, node);
    if (moved) {
        node->config_dirty = true;
    }
    if (delta) {
        delta->ncfg_ver_moved = moved;
    }

    size_t count = 0;
    if (json_obj_get_object(&jctx, "params") == OS_SUCCESS) {
        /* The parser cannot enumerate keys, so the cached tree names what to look for. A param the
         * payload does not mention keeps its value, which is what makes a delta merge safe. */
        for (ctrl_device_t *dev = node->devices; dev; dev = dev->next) {
            if (!dev->id) {
                continue;
            }
            if (json_obj_get_object(&jctx, dev->id) != OS_SUCCESS) {
                continue;
            }
            for (ctrl_param_t *param = dev->params; param; param = param->next) {
                if (!param->id) {
                    continue;
                }
                if (!__apply_param(&jctx, param)) {
                    continue;
                }
                count++;
                if (delta && delta->on_param) {
                    delta->on_param(delta->ctx, param);
                }
            }
            json_obj_leave_object(&jctx);
        }
        json_obj_leave_object(&jctx);
    }
    json_obj_leave_object(&jctx);
    json_obj_leave_object(&jctx);
    json_parse_end(&jctx);

    if (delta) {
        delta->num_changed = count;
    }
    return ESP_RMAKER_OK;
}
