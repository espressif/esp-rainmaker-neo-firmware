/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mdns_avahi.c
 * @brief mDNS discovery implementation for POSIX using Avahi.
 */

/* Includes *******************************************************/

/* Declaration includes. */
#include "osal_discovery.h"

/* Avahi includes. */
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/error.h>
#include <avahi-common/address.h>

/* Platform common includes. */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Standard includes. */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <ifaddrs.h>

/* Constants *******************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "osal_disc_avahi";

/* Private types *******************************************************/

/**
 * @brief Avahi client state structure.
 */
typedef struct {
    AvahiClient *client;
    AvahiSimplePoll *poll;
    AvahiEntryGroup *group;
    char *hostname;
    char *service_name;
    char *service_type;
    char *service_protocol;
    uint16_t port;
    AvahiStringList *txt_records;
} avahi_state_t;

/**
 * @brief Global Avahi state.
 */
static avahi_state_t avahi_state = {0};

/**
 * @brief Additional mDNS services (beyond the primary local control service).
 */
typedef struct extra_service_node {
    char *service_type;
    char *service_protocol;
    char *instance_name;
    AvahiStringList *txt_list;
    struct extra_service_node *next;
} extra_service_node_t;

static extra_service_node_t *s_extra_services = NULL;

/**
 * @brief When false, the primary advertised service is not announced.
 */
static bool s_primary_service_active = true;

/* Private function declarations *******************************************************/

/**
 * @brief Avahi client callback.
 */
static void avahi_client_callback(AvahiClient *client, AvahiClientState state, void *userdata);

/**
 * @brief Create TXT records string list.
 */
static AvahiStringList *create_txt_list(const osal_discovery_service_config_t *service_config);

/**
 * @brief Create TXT string list from txt_items (same encoding as create_txt_list).
 */
static AvahiStringList *create_txt_list_from_items(const osal_discovery_txt_items_t *txt_items);

/**
 * @brief Reset entry group and re-register A record, primary service (if active), and extras.
 */
static osal_err_t avahi_rebuild_entry_group(void);

/**
 * @brief Free all nodes in the extra-services list.
 */
static void free_extra_services_list(void);

/**
 * @brief Remove one extra-service node matching type and protocol.
 */
static void extra_services_remove_by_type(const char *service_type, const char *service_protocol);

/**
 * @brief True if type/protocol identify the primary local control discovery service.
 */
static bool is_primary_discovery_service(const char *service_type, const char *service_protocol);

/**
 * @brief Entry group callback.
 */
static void avahi_entry_group_callback(AvahiEntryGroup *group, AvahiEntryGroupState state, void *userdata);

/**
 * @brief Get the local IPv4 address.
 */
static int get_local_ipv4_address(struct in_addr *addr);

/**
 * @brief Free string pointer and set it to NULL.
 */
static void free_string(char **str);

/**
 * @brief Clear currently allocated service registration state.
 */
static void avahi_clear_service_registration(void);

/**
 * @brief Reset discovery services list and primary service state.
 */
static void avahi_reset_services_state(void);

/* Private function definitions *******************************************************/

static void avahi_client_callback(AvahiClient *client, AvahiClientState state, void *userdata)
{
    (void)userdata;

    switch (state) {
    case AVAHI_CLIENT_S_RUNNING:
        OSAL_LOGI(TAG, "Avahi client running");
        break;
    case AVAHI_CLIENT_FAILURE:
        OSAL_LOGE(TAG, "Avahi client failure: %s", avahi_strerror(avahi_client_errno(client)));
        break;
    case AVAHI_CLIENT_S_COLLISION:
        OSAL_LOGW(TAG, "Avahi client collision");
        break;
    default:
        break;
    }
}

static AvahiStringList *create_txt_list_from_items(const osal_discovery_txt_items_t *txt_items)
{
    AvahiStringList *txt_list = NULL;

    if (!txt_items) {
        return NULL;
    }

    for (size_t i = 0; i < txt_items->count; i++) {
        osal_discovery_txt_item_t *txt_item = &txt_items->list[i];
        if (txt_item && txt_item->var && txt_item->val) {
            char txt_record[256];
            snprintf(txt_record, sizeof(txt_record), "%s=%s", txt_item->var, txt_item->val);
            txt_list = avahi_string_list_add(txt_list, txt_record);
        }
    }

    return txt_list;
}

static AvahiStringList *create_txt_list(const osal_discovery_service_config_t *service_config)
{
    if (!service_config) {
        return NULL;
    }
    return create_txt_list_from_items(&service_config->txt_items);
}

static bool is_primary_discovery_service(const char *service_type, const char *service_protocol)
{
    if (!avahi_state.service_type || !avahi_state.service_protocol) {
        return false;
    }
    return strcmp(service_type, avahi_state.service_type) == 0 &&
           strcmp(service_protocol, avahi_state.service_protocol) == 0;
}

static void free_extra_service_node(extra_service_node_t *node)
{
    if (!node) {
        return;
    }
    free(node->service_type);
    free(node->service_protocol);
    free(node->instance_name);
    if (node->txt_list) {
        avahi_string_list_free(node->txt_list);
    }
    free(node);
}

static void free_extra_services_list(void)
{
    while (s_extra_services) {
        extra_service_node_t *next = s_extra_services->next;
        free_extra_service_node(s_extra_services);
        s_extra_services = next;
    }
}

static void extra_services_remove_by_type(const char *service_type, const char *service_protocol)
{
    extra_service_node_t **pp = &s_extra_services;

    while (*pp) {
        if (strcmp((*pp)->service_type, service_type) == 0 &&
                strcmp((*pp)->service_protocol, service_protocol) == 0) {
            extra_service_node_t *dead = *pp;
            *pp = dead->next;
            free_extra_service_node(dead);
        } else {
            pp = &(*pp)->next;
        }
    }
}

static osal_err_t avahi_rebuild_entry_group(void)
{
    int avahi_error;

    if (!avahi_state.group || !avahi_state.client || !avahi_state.hostname || !avahi_state.service_name) {
        return OSAL_ERR_INVALID_STATE;
    }

    avahi_error = avahi_entry_group_reset(avahi_state.group);
    if (avahi_error < 0) {
        OSAL_LOGE(TAG, "Failed to reset entry group: %s", avahi_strerror(avahi_error));
        return OSAL_ERR_FAIL;
    }

    char host_name_local[256];
    int ret = snprintf(host_name_local, sizeof(host_name_local), "%s.local", avahi_state.hostname);
    if (ret <= 0 || (size_t)ret >= sizeof(host_name_local)) {
        OSAL_LOGE(TAG, "Failed to construct local hostname for service");
        return OSAL_ERR_FAIL;
    }

    struct in_addr local_ip;
    if (get_local_ipv4_address(&local_ip) != 0) {
        OSAL_LOGE(TAG, "Failed to get local IPv4 address");
        return OSAL_ERR_FAIL;
    }

    AvahiAddress avahi_addr;
    avahi_addr.proto = AVAHI_PROTO_INET;
    avahi_addr.data.ipv4.address = local_ip.s_addr;

    avahi_error = avahi_entry_group_add_address(avahi_state.group,
                  AVAHI_IF_UNSPEC,
                  AVAHI_PROTO_UNSPEC,
                  AVAHI_PUBLISH_NO_REVERSE,
                  host_name_local,
                  &avahi_addr);
    if (avahi_error < 0) {
        OSAL_LOGE(TAG, "Failed to add A record: %s", avahi_strerror(avahi_error));
        return OSAL_ERR_FAIL;
    }

    char primary_type[256];
    snprintf(primary_type, sizeof(primary_type), "%s.%s", avahi_state.service_type, avahi_state.service_protocol);

    if (s_primary_service_active) {
        avahi_error = avahi_entry_group_add_service_strlst(avahi_state.group,
                      AVAHI_IF_UNSPEC,
                      AVAHI_PROTO_UNSPEC,
                      0,
                      avahi_state.service_name,
                      primary_type,
                      NULL,
                      host_name_local,
                      avahi_state.port,
                      avahi_state.txt_records);
        if (avahi_error < 0) {
            OSAL_LOGE(TAG, "Failed to add primary service: %s", avahi_strerror(avahi_error));
            return OSAL_ERR_FAIL;
        }
    }

    for (extra_service_node_t *e = s_extra_services; e != NULL; e = e->next) {
        char full_type[256];
        snprintf(full_type, sizeof(full_type), "%s.%s", e->service_type, e->service_protocol);
        avahi_error = avahi_entry_group_add_service_strlst(avahi_state.group,
                      AVAHI_IF_UNSPEC,
                      AVAHI_PROTO_UNSPEC,
                      0,
                      e->instance_name,
                      full_type,
                      NULL,
                      host_name_local,
                      avahi_state.port,
                      e->txt_list);
        if (avahi_error < 0) {
            OSAL_LOGE(TAG, "Failed to add service %s: %s", full_type, avahi_strerror(avahi_error));
            return OSAL_ERR_FAIL;
        }
    }

    avahi_error = avahi_entry_group_commit(avahi_state.group);
    if (avahi_error < 0) {
        OSAL_LOGE(TAG, "Failed to commit entry group: %s", avahi_strerror(avahi_error));
        return OSAL_ERR_FAIL;
    }

    return OSAL_ERR_OK;
}

static void avahi_entry_group_callback(AvahiEntryGroup *group, AvahiEntryGroupState state, void *userdata)
{
    (void)userdata;

    switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
        OSAL_LOGI(TAG, "Service registered successfully");
        break;
    case AVAHI_ENTRY_GROUP_COLLISION:
        OSAL_LOGW(TAG, "Service name collision");
        break;
    case AVAHI_ENTRY_GROUP_FAILURE:
        OSAL_LOGE(TAG, "Entry group failure: %s",
                  avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(group))));
        break;
    default:
        break;
    }
}

static int get_local_ipv4_address(struct in_addr *addr)
{
    struct ifaddrs *ifaddr, *ifa;

    if (!addr) {
        return -1;
    }

    if (getifaddrs(&ifaddr) == -1) {
        OSAL_LOGE(TAG, "Failed to get network interfaces");
        return -1;
    }

    // Look for the first non-loopback IPv4 address
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }

        if (ifa->ifa_addr->sa_family == AF_INET) {  // IPv4
            struct sockaddr_in *sockaddr = (struct sockaddr_in *)ifa->ifa_addr;
            uint32_t ip = ntohl(sockaddr->sin_addr.s_addr);

            // Skip loopback addresses (127.x.x.x)
            if ((ip & 0xFF000000) != 0x7F000000) {
                addr->s_addr = sockaddr->sin_addr.s_addr;
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sockaddr->sin_addr, ip_str, sizeof(ip_str));
                OSAL_LOGI(TAG, "Found local IPv4 address: %s on interface %s", ip_str, ifa->ifa_name);
                freeifaddrs(ifaddr);
                return 0;
            }
        }
    }

    freeifaddrs(ifaddr);
    return -1;
}

static void free_string(char **str)
{
    if (!str || !*str) {
        return;
    }
    free(*str);
    *str = NULL;
}

static void avahi_clear_service_registration(void)
{
    if (avahi_state.group) {
        avahi_entry_group_free(avahi_state.group);
        avahi_state.group = NULL;
    }
    free_string(&avahi_state.service_name);
    free_string(&avahi_state.service_type);
    free_string(&avahi_state.service_protocol);
    if (avahi_state.txt_records) {
        avahi_string_list_free(avahi_state.txt_records);
        avahi_state.txt_records = NULL;
    }
}

static void avahi_reset_services_state(void)
{
    free_extra_services_list();
    s_primary_service_active = true;
}

/* Public function definitions *******************************************************/

osal_err_t osal_discovery_init(const osal_discovery_service_config_t *service_config)
{
    int avahi_error;

    if (!service_config || !service_config->name) {
        OSAL_LOGE(TAG, "Invalid discovery init configuration");
        return OSAL_ERR_INVALID_ARG;
    }

    // Initialize Avahi poll
    avahi_state.poll = avahi_simple_poll_new();
    if (!avahi_state.poll) {
        OSAL_LOGE(TAG, "Failed to create Avahi poll");
        return OSAL_ERR_FAIL;
    }

    // Create Avahi client
    avahi_state.client = avahi_client_new(avahi_simple_poll_get(avahi_state.poll),
                                          AVAHI_CLIENT_NO_FAIL,
                                          avahi_client_callback, NULL, &avahi_error);
    if (!avahi_state.client) {
        OSAL_LOGE(TAG, "Failed to create Avahi client: %s", avahi_strerror(avahi_error));
        avahi_simple_poll_free(avahi_state.poll);
        avahi_state.poll = NULL;
        return OSAL_ERR_FAIL;
    }

    if (service_config->port <= 0) {
        OSAL_LOGE(TAG, "Service port is invalid");
        avahi_client_free(avahi_state.client);
        avahi_simple_poll_free(avahi_state.poll);
        avahi_state.client = NULL;
        avahi_state.poll = NULL;
        return OSAL_ERR_INVALID_ARG;
    }

    // Store hostname
    avahi_state.hostname = OSAL_STRDUP_EXTRAM(service_config->name);
    if (!avahi_state.hostname) {
        OSAL_LOGE(TAG, "Failed to allocate hostname");
        avahi_client_free(avahi_state.client);
        avahi_simple_poll_free(avahi_state.poll);
        avahi_state.client = NULL;
        avahi_state.poll = NULL;
        return OSAL_ERR_NO_MEM;
    }

    avahi_state.port = (uint16_t) service_config->port;

    avahi_state.group = avahi_entry_group_new(avahi_state.client, avahi_entry_group_callback, NULL);
    if (!avahi_state.group) {
        OSAL_LOGE(TAG, "Failed to create Avahi entry group");
        free_string(&avahi_state.hostname);
        avahi_client_free(avahi_state.client);
        avahi_simple_poll_free(avahi_state.poll);
        avahi_state.client = NULL;
        avahi_state.poll = NULL;
        return OSAL_ERR_FAIL;
    }

    /* Allow osal_discovery_add_service() without osal_discovery_on_start(): extras-only announcement. */
    s_primary_service_active = false;
    avahi_state.service_name = OSAL_STRDUP_EXTRAM("");
    avahi_state.service_type = OSAL_STRDUP_EXTRAM("_");
    avahi_state.service_protocol = OSAL_STRDUP_EXTRAM("_");
    if (!avahi_state.service_name || !avahi_state.service_type || !avahi_state.service_protocol) {
        OSAL_LOGE(TAG, "Failed to allocate placeholder service strings");
        avahi_clear_service_registration();
        free_string(&avahi_state.hostname);
        avahi_client_free(avahi_state.client);
        avahi_simple_poll_free(avahi_state.poll);
        avahi_state.client = NULL;
        avahi_state.poll = NULL;
        return OSAL_ERR_NO_MEM;
    }
    avahi_state.txt_records = NULL;

    osal_err_t err = avahi_rebuild_entry_group();
    if (err != OSAL_ERR_OK) {
        avahi_clear_service_registration();
        free_string(&avahi_state.hostname);
        avahi_client_free(avahi_state.client);
        avahi_simple_poll_free(avahi_state.poll);
        avahi_state.client = NULL;
        avahi_state.poll = NULL;
        return err;
    }

    OSAL_LOGI(TAG, "Avahi mDNS initialized with hostname: %s", service_config->name);
    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_deinit(void)
{
    avahi_clear_service_registration();

    if (avahi_state.client) {
        avahi_client_free(avahi_state.client);
        avahi_state.client = NULL;
    }
    if (avahi_state.poll) {
        avahi_simple_poll_free(avahi_state.poll);
        avahi_state.poll = NULL;
    }
    free_string(&avahi_state.hostname);
    avahi_reset_services_state();

    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_on_start(const osal_discovery_service_config_t *service_config, const osal_discovery_transport_config_t *transport_config)
{
    osal_err_t err = OSAL_ERR_OK;

    (void)transport_config;

    if (!avahi_state.client) {
        OSAL_LOGE(TAG, "Avahi client not initialized");
        return OSAL_ERR_INVALID_STATE;
    }

    if (!service_config || !service_config->name) {
        OSAL_LOGE(TAG, "Invalid service config or hostname is NULL");
        return OSAL_ERR_INVALID_ARG;
    }

    // Validate hostname - should not be empty and should not contain invalid characters
    if (strlen(service_config->name) == 0) {
        OSAL_LOGE(TAG, "Hostname is empty");
        return OSAL_ERR_INVALID_ARG;
    }

    // Check for basic invalid characters in hostname
    const char *invalid_chars = " \t\n\r\f\v.:";
    if (strcspn(service_config->name, invalid_chars) != strlen(service_config->name)) {
        OSAL_LOGE(TAG, "Hostname contains invalid characters: '%s'", service_config->name);
        return OSAL_ERR_INVALID_ARG;
    }

    OSAL_LOGI(TAG, "Registering mDNS service: hostname='%s'", service_config->name);

    avahi_reset_services_state();
    avahi_clear_service_registration();

    avahi_state.group = avahi_entry_group_new(avahi_state.client, avahi_entry_group_callback, NULL);
    if (!avahi_state.group) {
        OSAL_LOGE(TAG, "Failed to create Avahi entry group");
        return OSAL_ERR_FAIL;
    }

    avahi_state.service_name = OSAL_STRDUP_EXTRAM(service_config->name);
    if (!avahi_state.service_name) {
        OSAL_LOGE(TAG, "Failed to allocate service name");
        err = OSAL_ERR_NO_MEM;
        goto osal_discovery_on_start_fail;
    }

    avahi_state.service_type = OSAL_STRDUP_EXTRAM(service_config->type);
    if (!avahi_state.service_type) {
        OSAL_LOGE(TAG, "Failed to allocate service type");
        err = OSAL_ERR_NO_MEM;
        goto osal_discovery_on_start_fail;
    }

    avahi_state.service_protocol = OSAL_STRDUP_EXTRAM(service_config->protocol);
    if (!avahi_state.service_protocol) {
        OSAL_LOGE(TAG, "Failed to allocate service protocol");
        err = OSAL_ERR_NO_MEM;
        goto osal_discovery_on_start_fail;
    }

    avahi_state.txt_records = create_txt_list(service_config);

    avahi_state.port = (uint16_t)service_config->port;
    OSAL_LOGI(TAG, "Service port: %" PRIu16, avahi_state.port);

    err = avahi_rebuild_entry_group();
    if (err != OSAL_ERR_OK) {
        goto osal_discovery_on_start_fail;
    }

    OSAL_LOGI(TAG, "Service registered with Avahi");
    return OSAL_ERR_OK;

osal_discovery_on_start_fail:
    avahi_clear_service_registration();
    return err;
}

osal_err_t osal_discovery_add_service(const char *service_type, const char *service_protocol,
                                      const char *instance_name, const osal_discovery_txt_items_t *txt_items)
{
    if (service_type == NULL || service_protocol == NULL || instance_name == NULL || txt_items == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (!avahi_state.group || !avahi_state.client) {
        return OSAL_ERR_INVALID_STATE;
    }

    if (is_primary_discovery_service(service_type, service_protocol)) {
        extra_services_remove_by_type(service_type, service_protocol);

        char *new_name = OSAL_STRDUP_EXTRAM(instance_name);
        if (!new_name) {
            return OSAL_ERR_NO_MEM;
        }
        AvahiStringList *new_txt = create_txt_list_from_items(txt_items);

        char *old_name = avahi_state.service_name;
        AvahiStringList *old_txt = avahi_state.txt_records;
        avahi_state.service_name = new_name;
        avahi_state.txt_records = new_txt;
        s_primary_service_active = true;

        osal_err_t err = avahi_rebuild_entry_group();
        if (err != OSAL_ERR_OK) {
            avahi_state.service_name = old_name;
            avahi_state.txt_records = old_txt;
            free(new_name);
            if (new_txt) {
                avahi_string_list_free(new_txt);
            }
            return err;
        }
        free(old_name);
        if (old_txt) {
            avahi_string_list_free(old_txt);
        }
        return OSAL_ERR_OK;
    }

    extra_service_node_t *node = calloc(1, sizeof(extra_service_node_t));
    if (!node) {
        return OSAL_ERR_NO_MEM;
    }
    node->service_type = OSAL_STRDUP_EXTRAM(service_type);
    node->service_protocol = OSAL_STRDUP_EXTRAM(service_protocol);
    node->instance_name = OSAL_STRDUP_EXTRAM(instance_name);
    node->txt_list = create_txt_list_from_items(txt_items);
    if (!node->service_type || !node->service_protocol || !node->instance_name) {
        free_extra_service_node(node);
        return OSAL_ERR_NO_MEM;
    }

    extra_services_remove_by_type(service_type, service_protocol);
    node->next = s_extra_services;
    s_extra_services = node;

    osal_err_t err = avahi_rebuild_entry_group();
    if (err != OSAL_ERR_OK) {
        s_extra_services = node->next;
        free_extra_service_node(node);
        return err;
    }
    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_remove_service(const char *service_type, const char *service_protocol)
{
    if (service_type == NULL || service_protocol == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (!avahi_state.group || !avahi_state.client) {
        return OSAL_ERR_INVALID_STATE;
    }

    if (is_primary_discovery_service(service_type, service_protocol)) {
        s_primary_service_active = false;
    } else {
        extra_services_remove_by_type(service_type, service_protocol);
    }

    return avahi_rebuild_entry_group();
}

osal_err_t osal_discovery_on_stop(void)
{
    // Services are cleaned up in deinit
    return OSAL_ERR_OK;
}
