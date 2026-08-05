/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mqtt_topics.c
 * @brief MQTT topics used for RainMaker Neo
 */

/* Declarations */
#include "network/mqtt_topics.h"
#include "local_config.h"
#include "constants/identity.h"
#include "sdkconfig.h"

#include <string.h>

#define ESP_RMAKER_MQTT_USE_BASIC_INGEST CONFIG_ESP_RMAKER_MQTT_USE_BASIC_INGEST

/* Private function declarations *******************************************************/

static int __format_with_thing_name(char *buffer, size_t buffer_size, const char *format);

static int __format_with_thing_name_and_group_info(char *buffer, size_t buffer_size, const char *format);

/* Default self ops + ctx ****************************************************/

static int __self_write_thing_name(void *priv, char *buf, size_t buf_size)
{
    (void)priv;
    char *thing_name = NULL;
    esp_rmaker_error_t err = esp_rmaker_credentials_get_thing_name(&thing_name);
    if (err != ESP_RMAKER_OK || !thing_name) {
        return -1;
    }
    int written = snprintf(buf, buf_size, "%s", thing_name);
    free(thing_name);
    return (written > 0 && (size_t)written < buf_size) ? written : -1;
}

static int __self_write_group_info_str(void *priv, char *buf, size_t buf_size)
{
    (void)priv;
    char *group_info_str = esp_rmaker_local_config_get_group_info_str();
    if (group_info_str == NULL) {
        return -1;
    }
    int written = snprintf(buf, buf_size, "%s", group_info_str);
    free(group_info_str);
    return (written > 0 && (size_t)written < buf_size) ? written : -1;
}

const esp_rmaker_topic_ops_t esp_rmaker_topic_ops_self = {
    .write_thing_name = __self_write_thing_name,
    .write_group_info_str = __self_write_group_info_str,
};

const esp_rmaker_topic_ctx_t esp_rmaker_topic_ctx_self = {
    .ops = &esp_rmaker_topic_ops_self,
    .priv = NULL,
    .valid = NULL, /* always valid - program-lifetime singleton */
};

int esp_rmaker_topic_ctx_resolve_thing_name(const esp_rmaker_topic_ctx_t *ctx, char *buf, size_t buf_size)
{
    static const char kUnknown[] = "<unknown>";
    if (!buf || buf_size == 0) {
        return 0;
    }
    if (esp_rmaker_topic_ctx_is_valid(ctx) && ctx->ops->write_thing_name) {
        int written = ctx->ops->write_thing_name(ctx->priv, buf, buf_size);
        if (written > 0 && (size_t)written < buf_size) {
            return written;
        }
    }
    size_t n = sizeof(kUnknown) - 1;
    if (n >= buf_size) {
        n = buf_size - 1;
    }
    for (size_t i = 0; i < n; i++) {
        buf[i] = kUnknown[i];
    }
    buf[n] = '\0';
    return (int)n;
}

/* Private function definitions *******************************************************/

/* Resolve thing_name (and optionally group_info_str) for the given ctx,
 * write into the supplied scratch buffers, then snprintf into the
 * caller's buffer. Sizes come from constants/identity.h. */

static int __format_with_ctx_thing(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size, const char *format)
{
    if (!esp_rmaker_topic_ctx_is_valid(ctx) || !ctx->ops->write_thing_name) {
        return -1;
    }
    char thing_name[RMAKER_THING_NAME_BUFFER_SIZE];
    if (ctx->ops->write_thing_name(ctx->priv, thing_name, sizeof(thing_name)) < 0) {
        return -1;
    }
    int written = snprintf(buffer, buffer_size, format, thing_name);
    return (written > 0 && (size_t)written < buffer_size) ? written : -1;
}

static int __format_with_ctx_thing_and_group(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size, const char *format)
{
    if (!esp_rmaker_topic_ctx_is_valid(ctx) || !ctx->ops->write_thing_name || !ctx->ops->write_group_info_str) {
        return -1;
    }
    char thing_name[RMAKER_THING_NAME_BUFFER_SIZE];
    char group_info_str[RMAKER_GROUP_INFO_STR_BUFFER_SIZE];
    if (ctx->ops->write_thing_name(ctx->priv, thing_name, sizeof(thing_name)) < 0) {
        return -1;
    }
    if (ctx->ops->write_group_info_str(ctx->priv, group_info_str, sizeof(group_info_str)) < 0) {
        /* Treat absent group info as empty string. */
        group_info_str[0] = '\0';
    }
    int written = snprintf(buffer, buffer_size, format, thing_name, group_info_str);
    return (written > 0 && (size_t)written < buffer_size) ? written : -1;
}

/* Legacy helpers used by the self-only topic builders below. */
static int __format_with_thing_name(char *buffer, size_t buffer_size, const char *format)
{
    return __format_with_ctx_thing(&esp_rmaker_topic_ctx_self, buffer, buffer_size, format);
}

static int __format_with_thing_name_and_group_info(char *buffer, size_t buffer_size, const char *format)
{
    return __format_with_ctx_thing_and_group(&esp_rmaker_topic_ctx_self, buffer, buffer_size, format);
}

/* Public function definitions *******************************************************/

int esp_rmaker_mqtt_topic_to_cloud(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

#if ESP_RMAKER_MQTT_USE_BASIC_INGEST
    return __format_with_ctx_thing(ctx, buffer, buffer_size, "$aws/rules/node_to_cloud_rule/rainmaker/nodes/%s/to_cloud");
#else
    return __format_with_ctx_thing(ctx, buffer, buffer_size, "rainmaker/nodes/%s/to_cloud");
#endif /* ESP_RMAKER_MQTT_USE_BASIC_INGEST */
}

int esp_rmaker_mqtt_topic_from_cloud(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

    return __format_with_thing_name(buffer, buffer_size, "rainmaker/nodes/%s/from_cloud");
}

int esp_rmaker_mqtt_topic_params_to_node(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

    return __format_with_thing_name_and_group_info(buffer, buffer_size, "rainmaker/nodes/%s/user/params-%s/params");
}

int esp_rmaker_mqtt_topic_params_named_shadow_update(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }
    return __format_with_ctx_thing_and_group(ctx, buffer, buffer_size, "$aws/things/%s/shadow/name/params-%s/update");
}

int esp_rmaker_mqtt_topic_params_named_shadow_delete(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }
    return __format_with_ctx_thing_and_group(ctx, buffer, buffer_size, "$aws/things/%s/shadow/name/params-%s/delete");
}

int esp_rmaker_mqtt_topic_params_indexed_shadow_update(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }
    return __format_with_ctx_thing(ctx, buffer, buffer_size, "$aws/things/%s/shadow/name/iparams/update");
}

int esp_rmaker_mqtt_topic_timeseries_report(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }
#if ESP_RMAKER_MQTT_USE_BASIC_INGEST
    return __format_with_ctx_thing_and_group(ctx, buffer, buffer_size, "$aws/rules/node_ts_rule/rainmaker/nodes/%s/ts/%s");
#else
    return __format_with_ctx_thing_and_group(ctx, buffer, buffer_size, "rainmaker/nodes/%s/ts/%s");
#endif
}

int esp_rmaker_mqtt_topic_notify(char *buffer, size_t buffer_size)
{
    return esp_rmaker_mqtt_topic_notify_for_node(&esp_rmaker_topic_ctx_self, buffer, buffer_size);
}

int esp_rmaker_mqtt_topic_notify_for_node(const esp_rmaker_topic_ctx_t *ctx, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

#if ESP_RMAKER_MQTT_USE_BASIC_INGEST
    return __format_with_ctx_thing_and_group(ctx, buffer, buffer_size, "$aws/rules/node_notify_rule/rainmaker/nodes/%s/notify/%s");
#else
    return __format_with_ctx_thing_and_group(ctx, buffer, buffer_size, "rainmaker/nodes/%s/notify/%s");
#endif /* ESP_RMAKER_MQTT_USE_BASIC_INGEST */
}

int esp_rmaker_mqtt_topic_group_control_broadcast(char *buffer, size_t buffer_size, const char *primary)
{
    if (buffer == NULL || buffer_size == 0 || primary == NULL || primary[0] == '\0') {
        return -1;
    }

    int written = snprintf(buffer, buffer_size, "rainmaker/nodes/groups/%s/control", primary);
    return (written > 0 && (size_t)written < buffer_size) ? written : -1;
}

int esp_rmaker_mqtt_topic_group_control_subgroup(char *buffer, size_t buffer_size, const char *primary, const char *subgroup)
{
    if (buffer == NULL || buffer_size == 0 || primary == NULL || primary[0] == '\0' || subgroup == NULL || subgroup[0] == '\0') {
        return -1;
    }

    int written = snprintf(buffer, buffer_size, "rainmaker/nodes/groups/%s/subgroups/%s/control", primary, subgroup);
    return (written > 0 && (size_t)written < buffer_size) ? written : -1;
}

esp_rmaker_error_t esp_rmaker_mqtt_topic_parse_group_control_subgroup(
    const char *topic, size_t topic_len,
    char *sg_buf, size_t sg_buf_size)
{
    static const char k_prefix[] = "rainmaker/nodes/groups/";
    static const char k_suffix[] = "/control";
    static const char k_subgroups_sep[] = "/subgroups/";
    const size_t prefix_len = sizeof(k_prefix) - 1;
    const size_t suffix_len = sizeof(k_suffix) - 1;
    const size_t sep_len = sizeof(k_subgroups_sep) - 1;

    if (topic == NULL || sg_buf == NULL || sg_buf_size < RMAKER_SUBGROUP_BUFFER_SIZE) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (topic_len <= prefix_len + suffix_len) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (memcmp(topic, k_prefix, prefix_len) != 0) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (memcmp(topic + topic_len - suffix_len, k_suffix, suffix_len) != 0) {
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Inner = "<primary>" or "<primary>/subgroups/<sg>" */
    const char *inner = topic + prefix_len;
    size_t inner_len = topic_len - prefix_len - suffix_len;

    /* Primary must be non-empty and contain no '/'. */
    const char *first_slash = memchr(inner, '/', inner_len);
    if (first_slash == inner) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (first_slash == NULL) {
        /* Broadcast topic. */
        sg_buf[0] = '\0';
        return ESP_RMAKER_OK;
    }

    /* Must be "<primary>/subgroups/<sg>" with sg non-empty and slash-free. */
    size_t rest_len = inner_len - (size_t)(first_slash - inner);
    if (rest_len <= sep_len) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (memcmp(first_slash, k_subgroups_sep, sep_len) != 0) {
        return ESP_RMAKER_INVALID_ARG;
    }
    const char *sg = first_slash + sep_len;
    size_t sg_len = rest_len - sep_len;
    if (sg_len == 0 || memchr(sg, '/', sg_len) != NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (sg_len >= sg_buf_size) {
        return ESP_RMAKER_INVALID_ARG;
    }
    memcpy(sg_buf, sg, sg_len);
    sg_buf[sg_len] = '\0';
    return ESP_RMAKER_OK;
}

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

int esp_rmaker_mqtt_topic_group_control_subgroup_wildcard(char *buffer, size_t buffer_size, const char *primary)
{
    if (buffer == NULL || buffer_size == 0 || primary == NULL || primary[0] == '\0') {
        return -1;
    }

    int written = snprintf(buffer, buffer_size, "rainmaker/nodes/groups/%s/subgroups/+/control", primary);
    return (written > 0 && (size_t)written < buffer_size) ? written : -1;
}

int esp_rmaker_mqtt_topic_bridges_to_cloud(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

#if ESP_RMAKER_MQTT_USE_BASIC_INGEST
    return __format_with_thing_name(buffer, buffer_size, "$aws/rules/bridge_to_cloud_rule/rainmaker/bridges/%s/to_cloud");
#else
    return __format_with_thing_name(buffer, buffer_size, "rainmaker/bridges/%s/to_cloud");
#endif
}

int esp_rmaker_mqtt_topic_bridges_children_from_cloud_filter(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

    return __format_with_thing_name(buffer, buffer_size, "rainmaker/bridges/%s/children/+/from_cloud");
}

int esp_rmaker_mqtt_topic_bridges_children_params_filter(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

    return __format_with_thing_name(buffer, buffer_size, "rainmaker/bridges/%s/children/+/user/+/params");
}

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */

/* --- Generic operations ---*/

int esp_rmaker_mqtt_topic_append_accepted(esp_rmaker_mqtt_topic_fn_t topic_fn, char *buffer, size_t buffer_size)
{
    if (topic_fn == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    int ret = topic_fn(buffer, buffer_size);
    if (ret < 0 || ret + 9 >= buffer_size) {
        return -1;
    }

    return ret + snprintf(buffer + ret, buffer_size - ret, "/accepted");
}
