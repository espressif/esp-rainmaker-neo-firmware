/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file mdns_sd.c
 * @brief mDNS discovery implementation for POSIX using mDNSResponder.
 */

/* Includes *******************************************************/

/* Declaration includes. */
#include "osal_discovery.h"

/* mDNSResponder includes. */
#include <dns_sd.h>

/* Platform common includes. */
#include "osal_log.h"
#include "osal_mem_alloc.h"

/* Standard includes. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <ifaddrs.h>

/* Constants *******************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "osal_disc_mdns_sd";

/* Private types *******************************************************/

/**
 * @brief mDNSResponder service reference.
 */
static DNSServiceRef service_ref = NULL;

/**
 * @brief mDNSResponder connection reference for A record registration.
 */
static DNSServiceRef connection_ref = NULL;

/**
 * @brief mDNSResponder A record reference.
 */
static DNSRecordRef a_record_ref = NULL;

/**
 * @brief Flag to track A record registration success.
 */
static bool a_record_registered = false;

/**
 * @brief Flag to track service registration success.
 */
static bool service_registered = false;

/**
 * @brief Host FQDN used for SRV targets after successful discovery start (e.g. name.local).
 */
static char s_host_name_local[256];

/**
 * @brief Port registered for services (from service config at on_start).
 */
static uint16_t s_service_port;

/**
 * @brief Service type registered for services (from service config at on_start).
 */
static char *s_service_type = NULL;

/**
 * @brief Service protocol registered for services (from service config at on_start).
 */
static char *s_service_protocol = NULL;

/**
 * @brief Additional mDNS services sharing connection_ref.
 */
typedef struct extra_mdns_service_node {
    char *service_type;
    char *service_protocol;
    DNSServiceRef ref;
    struct extra_mdns_service_node *next;
} extra_mdns_service_node_t;

static extra_mdns_service_node_t *s_extra_mdns_services = NULL;


/* Private function declarations *******************************************************/

/**
 * @brief Create TXT records for mDNSResponder from txt_items.
 */
static TXTRecordRef create_txt_record_from_items(const osal_discovery_txt_items_t *txt_items);

/**
 * @brief Create TXT records for mDNSResponder.
 */
static TXTRecordRef create_txt_record(const osal_discovery_service_config_t *service_config);

/**
 * @brief Store service type and protocol.
 */
static osal_err_t store_service_type_and_protocol(const char *service_type, const char *service_protocol);

/**
 * @brief Free service type and protocol.
 */
static void free_service_type_and_protocol(void);

/**
 * @brief True if type/protocol identify the primary local control discovery service.
 */
static bool is_primary_discovery_service(const char *service_type, const char *service_protocol);

/**
 * @brief Remove and deallocate extra service registrations matching type and protocol.
 */
static void extra_mdns_services_remove_by_type(const char *service_type, const char *service_protocol);

/**
 * @brief Free all extra mDNS service nodes.
 */
static void free_extra_mdns_services_list(void);

/**
 * @brief Free one extra mDNS service node.
 */
static void free_extra_mdns_service_node(extra_mdns_service_node_t *node);

/**
 * @brief Reset discovery registration flags.
 */
static void reset_registration_flags(void);

/**
 * @brief Remove active A record if present.
 */
static void remove_a_record_if_present(void);

/**
 * @brief Deallocate and clear primary service reference.
 */
static void clear_service_ref(void);

/**
 * @brief Deallocate and clear shared connection reference.
 */
static void clear_connection_ref(void);

/**
 * @brief Reply callback for non-primary (extra) service registrations.
 */
static void register_extra_reply_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
        DNSServiceErrorType errorCode, const char *name,
        const char *regtype, const char *domain, void *context);

/**
 * @brief Register service reply callback for mDNSResponder.
 */
static void register_reply_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
                                    DNSServiceErrorType errorCode, const char *name,
                                    const char *regtype, const char *domain, void *context);

/**
 * @brief Register A record reply callback for mDNSResponder.
 */
static void register_a_record_reply_callback(DNSServiceRef sdRef, DNSRecordRef RecordRef,
        DNSServiceFlags flags,
        DNSServiceErrorType errorCode, void *context);

/**
 * @brief Get the local IPv4 address.
 */
static uint32_t get_local_ipv4_address(void);

/**
 * @brief Open shared connection, register A record, set host and port for add_service (no primary PTR yet).
 */
static osal_err_t mdns_sd_setup_transport(const osal_discovery_service_config_t *service_config);

/**
 * @brief Get error description string for DNS service error code.
 */
static const char *get_dns_error_string(DNSServiceErrorType errorCode);

/* Private function definitions *******************************************************/

static TXTRecordRef create_txt_record_from_items(const osal_discovery_txt_items_t *txt_items)
{
    TXTRecordRef txt_record;
    TXTRecordCreate(&txt_record, 0, NULL);

    if (!txt_items) {
        return txt_record;
    }

    for (size_t i = 0; i < txt_items->count; i++) {
        osal_discovery_txt_item_t *txt_item = &txt_items->list[i];
        if (txt_item && txt_item->var && txt_item->val) {
            TXTRecordSetValue(&txt_record, txt_item->var, strlen(txt_item->val), txt_item->val);
        }
    }

    return txt_record;
}

static TXTRecordRef create_txt_record(const osal_discovery_service_config_t *service_config)
{
    if (!service_config) {
        return create_txt_record_from_items(NULL);
    }
    return create_txt_record_from_items(&service_config->txt_items);
}

static osal_err_t store_service_type_and_protocol(const char *service_type, const char *service_protocol)
{
    if (!service_type || !service_protocol) {
        return OSAL_ERR_INVALID_ARG;
    }
    free_service_type_and_protocol();

    s_service_type = OSAL_STRDUP_EXTRAM(service_type);
    if (!s_service_type) {
        return OSAL_ERR_NO_MEM;
    }
    s_service_protocol = OSAL_STRDUP_EXTRAM(service_protocol);
    if (!s_service_protocol) {
        free(s_service_type);
        s_service_type = NULL;
        return OSAL_ERR_NO_MEM;
    }
    return OSAL_ERR_OK;
}

static void free_service_type_and_protocol(void)
{
    if (s_service_type) {
        free(s_service_type);
        s_service_type = NULL;
    }
    if (s_service_protocol) {
        free(s_service_protocol);
        s_service_protocol = NULL;
    }
}

static bool is_primary_discovery_service(const char *service_type, const char *service_protocol)
{
    if (!s_service_type || !s_service_protocol) {
        return false;
    }
    return strcmp(service_type, s_service_type) == 0 &&
           strcmp(service_protocol, s_service_protocol) == 0;
}

static void extra_mdns_services_remove_by_type(const char *service_type, const char *service_protocol)
{
    extra_mdns_service_node_t **pp = &s_extra_mdns_services;

    while (*pp) {
        extra_mdns_service_node_t *node = *pp;
        if (strcmp(node->service_type, service_type) == 0 &&
                strcmp(node->service_protocol, service_protocol) == 0) {
            *pp = node->next;
            free_extra_mdns_service_node(node);
        } else {
            pp = &node->next;
        }
    }
}

static void free_extra_mdns_services_list(void)
{
    while (s_extra_mdns_services) {
        extra_mdns_service_node_t *node = s_extra_mdns_services;
        s_extra_mdns_services = node->next;
        free_extra_mdns_service_node(node);
    }
}

static void free_extra_mdns_service_node(extra_mdns_service_node_t *node)
{
    if (!node) {
        return;
    }
    if (node->ref) {
        DNSServiceRefDeallocate(node->ref);
    }
    free(node->service_type);
    free(node->service_protocol);
    free(node);
}

static void reset_registration_flags(void)
{
    a_record_registered = false;
    service_registered = false;
}

static void remove_a_record_if_present(void)
{
    if (a_record_ref && connection_ref) {
        DNSServiceRemoveRecord(connection_ref, a_record_ref, 0);
        a_record_ref = NULL;
    }
}

static void clear_service_ref(void)
{
    if (service_ref) {
        DNSServiceRefDeallocate(service_ref);
        service_ref = NULL;
    }
}

static void clear_connection_ref(void)
{
    if (connection_ref) {
        DNSServiceRefDeallocate(connection_ref);
        connection_ref = NULL;
    }
}

static const char *get_dns_error_string(DNSServiceErrorType errorCode)
{
    switch (errorCode) {
    case -65537: return "kDNSServiceErr_Unknown";
    case -65538: return "kDNSServiceErr_NoSuchName";
    case -65539: return "kDNSServiceErr_NoMemory";
    case -65540: return "kDNSServiceErr_BadParam";
    case -65541: return "kDNSServiceErr_BadReference";
    case -65542: return "kDNSServiceErr_BadState";
    case -65543: return "kDNSServiceErr_BadFlags";
    case -65544: return "kDNSServiceErr_Unsupported";
    case -65545: return "kDNSServiceErr_NotInitialized";
    case -65546: return "kDNSServiceErr_AlreadyRegistered";
    case -65547: return "kDNSServiceErr_NameConflict";
    case -65548: return "kDNSServiceErr_Invalid";
    case -65549: return "kDNSServiceErr_Firewall";
    case -65550: return "kDNSServiceErr_Incompatible";
    case -65551: return "kDNSServiceErr_BadInterfaceIndex";
    case -65552: return "kDNSServiceErr_Refused";
    case -65553: return "kDNSServiceErr_NoSuchRecord";
    case -65554: return "kDNSServiceErr_NoAuth";
    case -65555: return "kDNSServiceErr_NoSuchKey";
    case -65556: return "kDNSServiceErr_NATTraversal";
    case -65557: return "kDNSServiceErr_DoubleNAT";
    case -65558: return "kDNSServiceErr_BadTime";
    default: return "Unknown error";
    }
}

static void register_extra_reply_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
        DNSServiceErrorType errorCode, const char *name,
        const char *regtype, const char *domain, void *context)
{
    (void)sdRef;
    (void)flags;
    (void)context;

    if (errorCode == kDNSServiceErr_NoError) {
        OSAL_LOGI(TAG, "Extra service registered: %s.%s in domain %s", name, regtype, domain);
    } else {
        OSAL_LOGE(TAG, "Extra service registration failed: %d (%s) for %s.%s in domain %s",
                  errorCode, get_dns_error_string(errorCode), name, regtype, domain);
    }
}

static void register_reply_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
                                    DNSServiceErrorType errorCode, const char *name,
                                    const char *regtype, const char *domain, void *context)
{
    (void)sdRef;
    (void)flags;
    (void)context;

    if (errorCode == kDNSServiceErr_NoError) {
        service_registered = true;
        OSAL_LOGI(TAG, "Service registered successfully: %s.%s in domain %s", name, regtype, domain);
    } else {
        service_registered = false;
        OSAL_LOGE(TAG, "Service registration failed: %d (%s) for %s.%s in domain %s",
                  errorCode, get_dns_error_string(errorCode), name, regtype, domain);
    }
}

static void register_a_record_reply_callback(DNSServiceRef sdRef, DNSRecordRef RecordRef,
        DNSServiceFlags flags,
        DNSServiceErrorType errorCode, void *context)
{
    (void)sdRef;
    (void)RecordRef;
    (void)flags;
    (void)context;

    if (errorCode == kDNSServiceErr_NoError) {
        a_record_registered = true;
        OSAL_LOGI(TAG, "A record registered successfully");
    } else {
        a_record_registered = false;
        OSAL_LOGE(TAG, "A record registration failed: %d (%s)", errorCode, get_dns_error_string(errorCode));
    }
}

static uint32_t get_local_ipv4_address(void)
{
    struct ifaddrs *ifaddr, *ifa;
    uint32_t local_ip = 0;

    if (getifaddrs(&ifaddr) == -1) {
        OSAL_LOGE(TAG, "Failed to get network interfaces");
        return 0;
    }

    // Look for the first non-loopback IPv4 address
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }

        if (ifa->ifa_addr->sa_family == AF_INET) {  // IPv4
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            uint32_t ip = ntohl(addr->sin_addr.s_addr);

            // Skip loopback addresses (127.x.x.x)
            if ((ip & 0xFF000000) != 0x7F000000) {
                local_ip = addr->sin_addr.s_addr;  // Keep in network byte order
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
                OSAL_LOGI(TAG, "Found local IPv4 address: %s on interface %s", ip_str, ifa->ifa_name);
                break;
            }
        }
    }

    freeifaddrs(ifaddr);
    return local_ip;
}

/* Public function definitions *******************************************************/

static osal_err_t mdns_sd_setup_transport(const osal_discovery_service_config_t *service_config)
{
    osal_err_t err = OSAL_ERR_OK;
    DNSServiceErrorType error;
    const char *service_name;
    char host_name_local[256] = {0};
    uint32_t local_ip;

    if (!service_config || !service_config->name) {
        OSAL_LOGE(TAG, "Invalid service config or hostname is NULL");
        return OSAL_ERR_INVALID_ARG;
    }
    if (strlen(service_config->name) == 0) {
        OSAL_LOGE(TAG, "Hostname is empty");
        return OSAL_ERR_INVALID_ARG;
    }
    const char *invalid_chars = " \t\n\r\f\v.:";
    if (strcspn(service_config->name, invalid_chars) != strlen(service_config->name)) {
        OSAL_LOGE(TAG, "Hostname contains invalid characters: '%s'", service_config->name);
        return OSAL_ERR_INVALID_ARG;
    }
    if (service_config->port <= 0) {
        OSAL_LOGE(TAG, "Service port is invalid");
        return OSAL_ERR_INVALID_ARG;
    }

    service_name = service_config->name;
    int ret = snprintf(host_name_local, sizeof(host_name_local), "%s.local", service_name);
    if (ret <= 0 || (size_t)ret >= sizeof(host_name_local)) {
        OSAL_LOGE(TAG, "Failed to construct local hostname for service: %s", service_name);
        return OSAL_ERR_FAIL;
    }

    OSAL_LOGI(TAG, "mDNS transport hostname: %s", host_name_local);

    local_ip = get_local_ipv4_address();
    if (local_ip == 0) {
        OSAL_LOGE(TAG, "Failed to get local IPv4 address");
        return OSAL_ERR_FAIL;
    }

    error = DNSServiceCreateConnection(&connection_ref);
    if (error != kDNSServiceErr_NoError) {
        OSAL_LOGE(TAG, "Failed to create DNS service connection: %d", error);
        return OSAL_ERR_FAIL;
    }

    a_record_ref = NULL;
    error = DNSServiceRegisterRecord(connection_ref,
                                     &a_record_ref,
                                     kDNSServiceFlagsUnique,
                                     0,
                                     host_name_local,
                                     kDNSServiceType_A,
                                     kDNSServiceClass_IN,
                                     4,
                                     &local_ip,
                                     0,
                                     register_a_record_reply_callback,
                                     NULL);

    if (error != kDNSServiceErr_NoError) {
        OSAL_LOGE(TAG, "Failed to register A record: %d", error);
        err = OSAL_ERR_FAIL;
        goto mdns_sd_setup_transport_fail;
    }

    DNSServiceProcessResult(connection_ref);

    if (!a_record_registered) {
        OSAL_LOGW(TAG, "A record registration callback not yet received, but registration initiated");
    } else {
        OSAL_LOGI(TAG, "A record registered successfully for %s", host_name_local);
    }

    memcpy(s_host_name_local, host_name_local, sizeof(s_host_name_local));
    s_service_port = (uint16_t) service_config->port;

    OSAL_LOGI(TAG, "mDNSResponder transport ready, port: %d", (int) s_service_port);
    return OSAL_ERR_OK;

mdns_sd_setup_transport_fail:
    remove_a_record_if_present();
    clear_connection_ref();
    s_host_name_local[0] = '\0';
    s_service_port = 0;
    reset_registration_flags();
    return err;
}

osal_err_t osal_discovery_init(const osal_discovery_service_config_t *service_config)
{
    if (!service_config || !service_config->name || service_config->port <= 0) {
        OSAL_LOGE(TAG, "Invalid discovery init configuration");
        return OSAL_ERR_INVALID_ARG;
    }

    if (service_ref || connection_ref) {
        osal_discovery_deinit();
    }

    return mdns_sd_setup_transport(service_config);
}

osal_err_t osal_discovery_deinit(void)
{
    free_extra_mdns_services_list();
    free_service_type_and_protocol();
    s_host_name_local[0] = '\0';
    s_service_port = 0;

    remove_a_record_if_present();
    clear_service_ref();
    clear_connection_ref();
    reset_registration_flags();

    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_on_start(const osal_discovery_service_config_t *service_config, const osal_discovery_transport_config_t *transport_config)
{
    osal_err_t err = OSAL_ERR_OK;
    DNSServiceErrorType error;
    const char *service_name;
    char host_name_local[256] = {0};
    uint32_t local_ip;
    TXTRecordRef txt_record;
    bool txt_record_valid = false;

    (void)transport_config;

    if (!service_config || !service_config->name || !service_config->type || !service_config->protocol) {
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

    // Ensure previous registration (if any) is cleaned up
    if (service_ref || connection_ref) {
        osal_discovery_deinit();
    }

    // Reset registration flags
    a_record_registered = false;
    service_registered = false;

    // Construct the custom hostname for the service (e.g. <service>.local)
    service_name = service_config->name;

    int ret = snprintf(host_name_local, sizeof(host_name_local), "%s.local", service_name);
    if (ret <= 0 || (size_t)ret >= sizeof(host_name_local)) {
        OSAL_LOGE(TAG, "Failed to construct local hostname for service: %s", service_name);
        return OSAL_ERR_FAIL;
    }

    OSAL_LOGI(TAG, "Using service hostname: %s", host_name_local);

    // Store service type and protocol
    err = store_service_type_and_protocol(service_config->type, service_config->protocol);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to store service type and protocol");
        goto osal_discovery_on_start_fail;
    }

    // Get local IP address for A record
    local_ip = get_local_ipv4_address();
    if (local_ip == 0) {
        OSAL_LOGE(TAG, "Failed to get local IPv4 address");
        err = OSAL_ERR_FAIL;
        goto osal_discovery_on_start_fail;
    }

    // Create connection for registering A record
    error = DNSServiceCreateConnection(&connection_ref);
    if (error != kDNSServiceErr_NoError) {
        OSAL_LOGE(TAG, "Failed to create DNS service connection: %d", error);
        err = OSAL_ERR_FAIL;
        goto osal_discovery_on_start_fail;
    }

    // Register A record for <service_name>.local pointing to current IP address
    // This makes the hostname pingable
    a_record_ref = NULL;
    error = DNSServiceRegisterRecord(connection_ref,
                                     &a_record_ref,
                                     kDNSServiceFlagsUnique,  // Unique hostname
                                     0,                       // All interfaces
                                     host_name_local,         // Full domain name
                                     kDNSServiceType_A,       // A record type
                                     kDNSServiceClass_IN,     // Internet class
                                     4,                       // rdata length (4 bytes for IPv4)
                                     &local_ip,               // IP address in network byte order
                                     0,                       // TTL (0 = default)
                                     register_a_record_reply_callback,
                                     NULL);                   // context

    if (error != kDNSServiceErr_NoError) {
        OSAL_LOGE(TAG, "Failed to register A record: %d", error);
        err = OSAL_ERR_FAIL;
        goto osal_discovery_on_start_fail;
    }

    // Process DNS service events to ensure A record registration callback fires
    // This ensures the A record is fully registered before we continue
    // Note: DNSServiceProcessResult may block if no data is available, but should return quickly
    // after registration as the daemon processes the request immediately
    DNSServiceProcessResult(connection_ref);

    if (!a_record_registered) {
        OSAL_LOGW(TAG, "A record registration callback not yet received, but registration initiated");
    } else {
        OSAL_LOGI(TAG, "A record registered successfully for %s", host_name_local);
    }

    txt_record = create_txt_record(service_config);
    txt_record_valid = true;

    // Extract port from service config
    uint16_t service_port = service_config->port;

    OSAL_LOGI(TAG, "Service port: %d", service_port);

    // Create full service type with protocol
    char service_type[256];
    snprintf(service_type, sizeof(service_type), "%s.%s",
             service_config->type, service_config->protocol);

    OSAL_LOGI(TAG, "Service type: %s", service_type);

    uint16_t txt_len = TXTRecordGetLength(&txt_record);
    OSAL_LOGI(TAG, "TXT record length: %d", txt_len);

    // Register the service with the custom hostname in the local domain
    // Use kDNSServiceFlagsShareConnection to share the connection with the A record
    // Copy the connection_ref to use with DNSServiceRegister
    DNSServiceRef service_ref_copy = connection_ref;
    error = DNSServiceRegister(&service_ref_copy,
                               kDNSServiceFlagsShareConnection, // Share connection with A record
                               0,                              // interface index
                               service_name,                   // name (service instance name)
                               service_type,                   // regtype (full type with protocol)
                               "local",                        // domain (local domain)
                               host_name_local,                // host (custom hostname)
                               htons(service_port),            // port (actual service port)
                               TXTRecordGetLength(&txt_record),
                               TXTRecordGetBytesPtr(&txt_record),
                               register_reply_callback,
                               NULL);                          // context
    service_ref = service_ref_copy;

    if (error != kDNSServiceErr_NoError) {
        OSAL_LOGE(TAG, "Failed to register service with mDNSResponder: %d", error);
        err = OSAL_ERR_FAIL;
        goto osal_discovery_on_start_fail;
    }

    TXTRecordDeallocate(&txt_record);
    txt_record_valid = false;

    // Process DNS service events to ensure service registration callback fires
    // This ensures the service is fully registered before we continue
    // Process both connection_ref (for A record) and service_ref (for service) events
    DNSServiceProcessResult(connection_ref);
    DNSServiceProcessResult(service_ref);

    if (!service_registered) {
        OSAL_LOGW(TAG, "Service registration callback not yet received, but registration initiated");
    } else {
        OSAL_LOGI(TAG, "Service registered successfully with mDNSResponder");
    }

    memcpy(s_host_name_local, host_name_local, sizeof(s_host_name_local));
    s_service_port = service_port;

    return OSAL_ERR_OK;

osal_discovery_on_start_fail:
    if (txt_record_valid) {
        TXTRecordDeallocate(&txt_record);
    }
    clear_service_ref();
    remove_a_record_if_present();
    clear_connection_ref();
    free_service_type_and_protocol();
    s_host_name_local[0] = '\0';
    s_service_port = 0;
    reset_registration_flags();
    return err;
}

osal_err_t osal_discovery_add_service(const char *service_type, const char *service_protocol,
                                      const char *instance_name, const osal_discovery_txt_items_t *txt_items)
{
    osal_err_t err = OSAL_ERR_OK;
    DNSServiceErrorType error;
    TXTRecordRef txt_record;
    uint16_t txt_len;
    char regtype[256];

    if (service_type == NULL || service_protocol == NULL || instance_name == NULL || txt_items == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (connection_ref == NULL || s_host_name_local[0] == '\0') {
        return OSAL_ERR_INVALID_STATE;
    }

    int rt = snprintf(regtype, sizeof(regtype), "%s.%s", service_type, service_protocol);
    if (rt <= 0 || (size_t)rt >= sizeof(regtype)) {
        OSAL_LOGE(TAG, "Service regtype too long");
        return OSAL_ERR_FAIL;
    }

    txt_record = create_txt_record_from_items(txt_items);
    txt_len = TXTRecordGetLength(&txt_record);

    if (is_primary_discovery_service(service_type, service_protocol)) {
        clear_service_ref();
        service_registered = false;

        DNSServiceRef service_ref_copy = connection_ref;
        error = DNSServiceRegister(&service_ref_copy,
                                   kDNSServiceFlagsShareConnection,
                                   0,
                                   instance_name,
                                   regtype,
                                   "local",
                                   s_host_name_local,
                                   htons(s_service_port),
                                   txt_len,
                                   TXTRecordGetBytesPtr(&txt_record),
                                   register_reply_callback,
                                   NULL);
        service_ref = service_ref_copy;

        TXTRecordDeallocate(&txt_record);

        if (error != kDNSServiceErr_NoError) {
            OSAL_LOGE(TAG, "Failed to register primary service: %d", error);
            return OSAL_ERR_FAIL;
        }

        DNSServiceProcessResult(connection_ref);
        if (service_ref) {
            DNSServiceProcessResult(service_ref);
        }
        return OSAL_ERR_OK;
    }

    extra_mdns_services_remove_by_type(service_type, service_protocol);

    extra_mdns_service_node_t *node = calloc(1, sizeof(extra_mdns_service_node_t));
    if (!node) {
        err = OSAL_ERR_NO_MEM;
        goto osal_discovery_add_service_fail;
    }
    node->service_type = OSAL_STRDUP_EXTRAM(service_type);
    node->service_protocol = OSAL_STRDUP_EXTRAM(service_protocol);
    if (!node->service_type || !node->service_protocol) {
        err = OSAL_ERR_NO_MEM;
        goto osal_discovery_add_service_fail;
    }

    DNSServiceRef extra_ref = connection_ref;
    error = DNSServiceRegister(&extra_ref,
                               kDNSServiceFlagsShareConnection,
                               0,
                               instance_name,
                               regtype,
                               "local",
                               s_host_name_local,
                               htons(s_service_port),
                               txt_len,
                               TXTRecordGetBytesPtr(&txt_record),
                               register_extra_reply_callback,
                               NULL);
    node->ref = extra_ref;

    TXTRecordDeallocate(&txt_record);

    if (error != kDNSServiceErr_NoError) {
        OSAL_LOGE(TAG, "Failed to register extra mDNS service: %d", error);
        err = OSAL_ERR_FAIL;
        goto osal_discovery_add_service_fail_node;
    }

    node->next = s_extra_mdns_services;
    s_extra_mdns_services = node;

    DNSServiceProcessResult(connection_ref);
    DNSServiceProcessResult(extra_ref);

    return OSAL_ERR_OK;

osal_discovery_add_service_fail_node:
    free_extra_mdns_service_node(node);
    return err;

osal_discovery_add_service_fail:
    TXTRecordDeallocate(&txt_record);
    return err;
}

osal_err_t osal_discovery_remove_service(const char *service_type, const char *service_protocol)
{
    if (service_type == NULL || service_protocol == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    if (connection_ref == NULL) {
        return OSAL_ERR_INVALID_STATE;
    }

    if (is_primary_discovery_service(service_type, service_protocol)) {
        if (service_ref) {
            DNSServiceRefDeallocate(service_ref);
            service_ref = NULL;
        }
        service_registered = false;
        return OSAL_ERR_OK;
    }

    extra_mdns_services_remove_by_type(service_type, service_protocol);
    return OSAL_ERR_OK;
}

osal_err_t osal_discovery_on_stop(void)
{
    // Services are cleaned up in deinit
    return OSAL_ERR_OK;
}
