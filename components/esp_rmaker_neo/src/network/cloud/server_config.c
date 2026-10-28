/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file server_config.c
 * @brief Cache of the deployment's server config (getServerConfig cloud event).
 */

/* Includes **********************************************************************/

/* Declaration includes */
#include "network/cloud/server_config.h"

/* The whole file builds only with CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG (see sources.cmake). */

/* Standard includes */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Platform includes */
#include "osal_event_group.h"
#include "osal_log.h"
#include "osal_semaphore.h"
#include "osal_task.h"
#include "osal_ticks.h"

/* JSON includes */
#include "json_generator.h"

/* Work queue (no waiting on it) */
#include "esp_rmaker_work_queue.h"

/* Configuration includes */
#include "sdkconfig.h"

/* Private variables **************************************************************/

static const char *TAG = "rmng_server_cfg";

/** Set once a getServerConfig response with at least one requested key has been handled. */
#define SERVER_CONFIG_RECEIVED_BIT (1 << 0)
/** Set by deinit, so readers blocked in get() return. */
#define SERVER_CONFIG_STOPPED_BIT (1 << 1)

/** Longest single wait in get(); keeps each ms-to-ticks conversion far from 32-bit overflow. */
#define SERVER_CONFIG_WAIT_SLICE_MS 30000

/** The "rmng-base" keys. */
static const esp_rmaker_server_config_node_t s_base_keys[] = {
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_STACK_REGION
    {.name = "StackRegion",                .key = ESP_RMAKER_SERVER_CONFIG_STACK_REGION},
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_CRED_ENDPOINT
    {.name = "CredentialProviderEndpoint", .key = ESP_RMAKER_SERVER_CONFIG_CREDENTIAL_PROVIDER_ENDPOINT},
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_VIDEO_ROLE_ALIAS
    {.name = "NodeVideoRoleAliases",       .key = ESP_RMAKER_SERVER_CONFIG_NODE_VIDEO_ROLE_ALIASES},
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_FILE_ROLE_ALIAS
    {.name = "NodeFileRoleAliases",        .key = ESP_RMAKER_SERVER_CONFIG_NODE_FILE_ROLE_ALIASES},
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_FILES_BUCKET_NAME
    {.name = "FilesBucketName",            .key = ESP_RMAKER_SERVER_CONFIG_FILES_BUCKET_NAME},
#endif
};

/** The request tree. Each key appears in it exactly once. */
static const esp_rmaker_server_config_node_t s_tree[] = {
    {.name = "rmng-base", .children = s_base_keys, .num_children = sizeof(s_base_keys) / sizeof(s_base_keys[0])},
};
#define TREE_SIZE (sizeof(s_tree) / sizeof(s_tree[0]))
#define NUM_KEYS ESP_RMAKER_SERVER_CONFIG_KEY_MAX

#define VALUE(buffer) {.buf = (buffer), .size = sizeof(buffer)}

/* Value sizes, NUL included. A longer value is dropped. */
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_STACK_REGION
static char s_stack_region[32];             /* "ap-southeast-5" is 14 */
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_CRED_ENDPOINT
static char s_credential_endpoint[128];     /* "<prefix>.credentials.iot.<region>.amazonaws.com[.cn]" */
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_VIDEO_ROLE_ALIAS
static char s_video_role_aliases[2 * 128 + 2];  /* Two 128-character AWS IoT role aliases and a comma */
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_FILE_ROLE_ALIAS
static char s_file_role_aliases[2 * 128 + 2];
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_FILES_BUCKET_NAME
static char s_files_bucket_name[64];        /* S3 bucket names are 63 at most */
#endif

static esp_rmaker_server_config_value_t s_values[NUM_KEYS] = {
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_STACK_REGION
    [ESP_RMAKER_SERVER_CONFIG_STACK_REGION]                 = VALUE(s_stack_region),
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_CRED_ENDPOINT
    [ESP_RMAKER_SERVER_CONFIG_CREDENTIAL_PROVIDER_ENDPOINT] = VALUE(s_credential_endpoint),
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_VIDEO_ROLE_ALIAS
    [ESP_RMAKER_SERVER_CONFIG_NODE_VIDEO_ROLE_ALIASES]      = VALUE(s_video_role_aliases),
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_FILE_ROLE_ALIAS
    [ESP_RMAKER_SERVER_CONFIG_NODE_FILE_ROLE_ALIASES]       = VALUE(s_file_role_aliases),
#endif
#if CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_KEY_FILES_BUCKET_NAME
    [ESP_RMAKER_SERVER_CONFIG_FILES_BUCKET_NAME]            = VALUE(s_files_bucket_name),
#endif
};


static osal_semaphore_handle_t s_lock;
static osal_event_group_handle_t s_events;

/* Private functions **************************************************************/

/** Write each node: a level as an object of its children, a key as null. */
static void __emit_nodes(json_gen_str_t *jstr, const esp_rmaker_server_config_node_t *nodes, size_t num_nodes)
{
    for (size_t i = 0; i < num_nodes; i++) {
        if (nodes[i].children) {
            json_gen_push_object(jstr, nodes[i].name);
            __emit_nodes(jstr, nodes[i].children, nodes[i].num_children);
            json_gen_pop_object(jstr);
        } else {
            json_gen_obj_set_null(jstr, nodes[i].name);
        }
    }
}

/* Public functions ***************************************************************/

esp_rmaker_error_t esp_rmaker_server_config_init(void)
{
    if (!s_lock) {
        s_lock = osal_semaphore_create_mutex();
    }
    if (!s_events) {
        s_events = osal_event_group_create();
    }
    if (!s_lock || !s_events) {
        OSAL_LOGE(TAG, "Failed to create the server config cache.");
        return ESP_RMAKER_NO_MEM;
    }
    osal_event_group_clear_bits(s_events, SERVER_CONFIG_STOPPED_BIT);
    return ESP_RMAKER_OK;
}

void esp_rmaker_server_config_deinit(void)
{
    /* The sync objects are kept: an application task may still be in get(). */
    if (!s_lock || !s_events) {
        return;
    }
    osal_semaphore_take(s_lock, OSAL_MAX_DELAY);
    for (size_t i = 0; i < NUM_KEYS; i++) {
        s_values[i].stored = false;
        s_values[i].buf[0] = '\0';
    }
    osal_event_group_clear_bits(s_events, SERVER_CONFIG_RECEIVED_BIT);
    osal_event_group_set_bits(s_events, SERVER_CONFIG_STOPPED_BIT);
    osal_semaphore_give(s_lock);
}

bool esp_rmaker_server_config_is_received(void)
{
    return s_events && (osal_event_group_get_bits(s_events) & SERVER_CONFIG_RECEIVED_BIT);
}

int esp_rmaker_server_config_build_request(const esp_rmaker_server_config_node_t *nodes, size_t num_nodes, char *buf,
        size_t buf_size)
{
    if ((!nodes && num_nodes) || (!buf && buf_size) || (buf && !buf_size) || buf_size > INT32_MAX) {
        return -1;
    }
    json_gen_str_t jstr;
    json_gen_str_start(&jstr, buf, (int)buf_size, NULL, NULL);
    json_gen_start_object(&jstr);
    __emit_nodes(&jstr, nodes, num_nodes);
    json_gen_end_object(&jstr);
    /* json_gen keeps the count after an overflow, so this is the full size. */
    int req_size = json_gen_str_end(&jstr);
    if (buf && req_size > (int)buf_size) {
        buf[0] = '\0';
    }
    return req_size;
}

const char *esp_rmaker_server_config_request_data(void)
{
    /* The tree is fixed at build time, so the request is too: build it once. Five keys take 141 bytes. */
    static char s_request[192];
    if (!s_request[0]) {
        int req_size = esp_rmaker_server_config_build_request(s_tree, TREE_SIZE, s_request, sizeof(s_request));
        if (req_size < 0 || (size_t)req_size > sizeof(s_request)) {
            OSAL_LOGE(TAG, "Server config request does not fit: %d bytes", req_size);
            s_request[0] = '\0';
            return NULL;
        }
    }
    return s_request;
}

void esp_rmaker_server_config_parse_tree(jparse_ctx_t *jctx, const esp_rmaker_server_config_node_t *nodes,
        size_t num_nodes, esp_rmaker_server_config_value_t *values)
{
    for (size_t i = 0; i < num_nodes; i++) {
        const char *name = nodes[i].name;
        if (nodes[i].children) {
            if (json_obj_get_object(jctx, name) != 0) {
                OSAL_LOGW(TAG, "Server config has no level %s", name);
                continue;
            }
            esp_rmaker_server_config_parse_tree(jctx, nodes[i].children, nodes[i].num_children, values);
            json_obj_leave_object(jctx);
            continue;
        }
        esp_rmaker_server_config_value_t *value = &values[nodes[i].key];
        int len = 0;
        if (json_obj_get_strlen(jctx, name, &len) != 0) {
            OSAL_LOGW(TAG, "Server config has no string value for %s", name);
            continue;
        }
        if ((size_t)len >= value->size) {
            OSAL_LOGE(TAG, "Server config value for %s is too long: %d, max %d", name, len, (int)value->size - 1);
            continue;
        }
        value->stored = json_obj_get_string(jctx, name, value->buf, (int)value->size) == 0;
    }
}

bool esp_rmaker_server_config_parse(jparse_ctx_t *jctx)
{
    if (!s_lock) {
        return false;
    }
    osal_semaphore_take(s_lock, OSAL_MAX_DELAY);
    esp_rmaker_server_config_parse_tree(jctx, s_tree, TREE_SIZE, s_values);
    bool any = false;
    for (size_t i = 0; i < NUM_KEYS; i++) {
        any |= s_values[i].stored;
    }
    osal_semaphore_give(s_lock);
    return any;
}

void esp_rmaker_server_config_mark_received(void)
{
    if (s_events) {
        osal_event_group_set_bits(s_events, SERVER_CONFIG_RECEIVED_BIT);
    }
}

esp_rmaker_error_t esp_rmaker_server_config_get(esp_rmaker_server_config_key_t key, char **p_value,
        uint32_t timeout_ms)
{
    if (p_value) {
        *p_value = NULL;
    }
    if ((unsigned)key >= NUM_KEYS || !p_value) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (!s_lock || !s_events) {
        return ESP_RMAKER_INVALID_STATE;
    }
    bool received = esp_rmaker_server_config_is_received();
    if (!received && timeout_ms > 0 && esp_rmaker_work_queue_is_current_task()) {
        OSAL_LOGE(TAG, "Cannot wait for the server config on the work queue, which handles its response.");
        return ESP_RMAKER_INVALID_STATE;
    }
    /* The remaining time stays in ms, so a long timeout cannot overflow the ms-to-ticks conversion. */
    uint32_t remaining_ms = timeout_ms;
    while (!received && remaining_ms > 0) {
        uint32_t wait_ms = remaining_ms < SERVER_CONFIG_WAIT_SLICE_MS ? remaining_ms : SERVER_CONFIG_WAIT_SLICE_MS;
        osal_event_group_bits_t bits = osal_event_group_wait_bits(s_events,
                                       SERVER_CONFIG_RECEIVED_BIT | SERVER_CONFIG_STOPPED_BIT, false, false, osal_ticks_from_ms(wait_ms));
        received = bits & SERVER_CONFIG_RECEIVED_BIT;
        if (!received && (bits & SERVER_CONFIG_STOPPED_BIT)) {
            break;
        }
        remaining_ms -= wait_ms;
    }
    if (!received) {
        return ESP_RMAKER_TIMEOUT;
    }
    esp_rmaker_error_t err = ESP_RMAKER_NOT_FOUND;
    osal_semaphore_take(s_lock, OSAL_MAX_DELAY);
    if (s_values[key].stored) {
        *p_value = strdup(s_values[key].buf);
        err = *p_value ? ESP_RMAKER_OK : ESP_RMAKER_NO_MEM;
    }
    osal_semaphore_give(s_lock);
    return err;
}
