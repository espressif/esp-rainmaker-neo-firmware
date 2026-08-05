/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file services/svc_local_endpoints.c
 * @brief Local endpoints service: local control and/or challenge-response
 *
 * One protocomm HTTP instance (protocomm_httpd is a process-wide singleton),
 * owned and refcounted by this service, serves two independently enabled
 * endpoint sets:
 * - local control: get_params / set_params / get_config
 *   (see docs/en/specs/local_ctrl_endpoint_protocol.md)
 * - challenge-response: ch_resp (on-network user-node association)
 *
 * A single `_esp_rmaker_ctrl._tcp` service is advertised whenever the
 * instance is up, with the node ID as hostname/instance name and a `cap` TXT
 * record reflecting the active endpoint sets.
 */

/* Includes *******************************************************/

/* Declaration includes. */
#include "data_model_internal.h"
#include "esp_rmaker_standard_services.h"
#include "services/standard_creation.h"

/* Standard includes. */
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* Platform common includes. */
#include "osal_mem_alloc.h"
#include "osal_log.h"
#include "osal_random.h"
#include "osal_discovery.h"
#include "osal_scheduler.h"

/* Protocomm includes. */
#include "protocomm.h"
#include "protocomm_httpd.h"
#include "protocomm_security1.h"
#if CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2
#include "protocomm_security2.h"
#endif

#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2
#include "esp_srp.h"
#endif

#if CONFIG_ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

/* Constants includes. */
#include "constants/nvs.h"
#include "constants/discovery.h"

/* NVS includes. */
#include "util/esp_rmaker_nvs.h"

/* Convert includes. */
#include "util/esp_rmaker_convert_hex.h"

/* Local configuration includes. */
#include "sdkconfig.h"
#include "local_config.h"

/* RMNG includes. */
#include "node_internal.h"
#include "event_loop.h"
#include "esp_rmaker_work_queue.h"
#include "local_ctrl/endpoints.h"
#include "local_ctrl/sec2_cache.h"
#include "chal_resp/impl.h"

/* Feature convenience macros *******************************************************/

/* Set by Kconfig when the configured security scheme uses a PoP (security 2, or
 * security 1 with PoP). */
#define LOCAL_CTRL_POP_IN_USE CONFIG_ESP_RMAKER_LOCAL_CTRL_POP_IN_USE

/* Types *******************************************************/

/**
 * @brief State for the local endpoints service (exists while any feature is active).
 */
typedef struct {
    char *name; /**< Hostname / service instance name (node ID) */
    char *pop; /**< PoP (NULL when not in use) */
    protocomm_t *pc; /**< Protocomm instance owned by this service */
    char *version_json; /**< Version endpoint payload (owned; protocomm keeps the pointer) */
    uint16_t port; /**< Effective HTTP port */
    bool discovery_initialized; /**< osal_discovery_init() succeeded, so teardown must deinit */
    bool advertised; /**< Discovery service currently advertised */
    bool local_ctrl_active; /**< Local control endpoints registered */
    bool chal_resp_active; /**< Challenge-response endpoint registered */
    bool stop_scheduled; /**< Delayed chal-only stop scheduled */
    osal_scheduler_task_handle_t stop_task_handle; /**< Delayed stop scheduler task */
#if LOCAL_CTRL_POP_IN_USE
    bool pop_from_nvs; /**< True if the PoP was loaded from NVS (i.e. not freshly generated this run) */
#endif
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_1
    protocomm_security1_params_t sec1_params; /**< Security 1 parameters (PoP) */
#endif
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2
    protocomm_security2_params_t sec2_params; /**< Security 2 parameters (salt + verifier) */
    char *sec2_salt;       /**< SRP6a salt buffer (allocated by esp_srp_gen_salt_verifier or NVS read) */
    int   sec2_salt_len;
    char *sec2_verifier;   /**< SRP6a verifier buffer (allocated by esp_srp_gen_salt_verifier or NVS read) */
    int   sec2_verifier_len;
#endif
} __local_ctrl_service_priv_data_t;

/* Constants *******************************************************/

/**
 * @brief Tag for logging.
 */
static const char *TAG = "rmng_svc_local_ctrl";

/**
 * @brief Protocomm session-security and version endpoint names.
 */
#define LOCAL_CTRL_SECURITY_ENDPOINT "rmaker_local_ctrl/session"
#define LOCAL_CTRL_VERSION_ENDPOINT  "rmaker_local_ctrl/version"

/**
 * @brief Version endpoint payload format. sec_ver / sec_patch_ver are reported
 *        by protocomm for the registered security scheme; the cap list
 *        reflects the active endpoint sets, plus "no_pop" for security 1
 *        without PoP (network-provisioning capability convention).
 */
#define LOCAL_CTRL_VERSION_FMT \
    "{\"rmaker_local_ctrl\":{\"ver\":\"v1.0\",\"sec_ver\":%d,\"sec_patch_ver\":%u,\"cap\":[%s]}}"

/**
 * @brief Feature tokens used in both the version cap list and the `cap` TXT record.
 */
#define LOCAL_CTRL_CAPS_LOCAL_CTRL "\"get_params\",\"set_params\",\"get_config\""
#define LOCAL_CTRL_TXT_CAP_LOCAL_CTRL "local_ctrl"
#define LOCAL_CTRL_TXT_CAP_CHAL_RESP  RMAKER_CHAL_RESP_ENDPOINT_NAME

/**
 * @brief Delay before the chal-resp-only instance is stopped after a
 *        client-issued disable, so the response gets flushed first.
 */
#define LOCAL_CTRL_STOP_DELAY_MS 2000U

/**
 * @brief NVS namespace for the local control service.
 */
#define LOCAL_CTRL_NVS_NAMESPACE "local_ctrl"

/**
 * @brief NVS key for the PoP.
 */
#define LOCAL_CTRL_NVS_POP_KEY   "pop"

#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2
/**
 * @brief NVS keys for the cached SRP6a salt and verifier.
 */
#define LOCAL_CTRL_NVS_SEC2_SALT_KEY     "sec2_salt"
#define LOCAL_CTRL_NVS_SEC2_VERIFIER_KEY "sec2_verifier"

/**
 * @brief Fixed SRP6a salt length (matches protocomm security2).
 */
#define LOCAL_CTRL_SEC2_SALT_LEN 16

/**
 * @brief SRP6a username used for the salt/verifier derivation.
 * @note Must match the value used by the client.
 *       Fixed to "wifiprov" for parity with unified provisioning.
 */
static const char *LOCAL_CTRL_SEC2_USERNAME = "wifiprov";
#endif

#if LOCAL_CTRL_POP_IN_USE
/**
 * @brief PoP length in characters - the value printed on the device and entered by a user.
 */
#define LOCAL_CTRL_POP_CHARS    CONFIG_ESP_RMAKER_LOCAL_CTRL_POP_LENGTH

/**
 * @brief Random bytes behind the PoP. Hex is two characters per byte, so an odd character
 *        count rounds up to a whole byte (the extra nibble is simply not emitted).
 */
#define LOCAL_CTRL_POP_BYTES    ((LOCAL_CTRL_POP_CHARS + 1) / 2)
#endif

/**
 * @brief Stack size for the HTTP server task.
 */
#define LOCAL_CTRL_STACK_SIZE    CONFIG_ESP_RMAKER_LOCAL_CTRL_STACK_SIZE

/* Variables *******************************************************/

/**
 * @brief Service state; NULL while no feature is active.
 */
static __local_ctrl_service_priv_data_t *__priv_data = NULL;

#if CONFIG_RMNG_HOST_CTRL
/**
 * @brief HTTP port override from host control (POSIX/tests). 0 = use CONFIG_ESP_RMAKER_LOCAL_CTRL_HTTP_PORT.
 */
static uint16_t s_local_ctrl_http_port_override;
#endif /* CONFIG_RMNG_HOST_CTRL */

/**
 * @brief Custom PoP set by application before the service starts.
 */
static char *__custom_pop = NULL;

/* Private function declarations *******************************************************/

static esp_rmaker_error_t __core_refresh_advert(void);
static void __core_stop_if_idle(void);
static esp_rmaker_error_t __local_ctrl_service_disable(bool post_stopped_event);

/* PoP *******************************************************/

#if LOCAL_CTRL_POP_IN_USE
#define __local_ctrl_get_nvs_string(key) esp_rmaker_nvs_get_string(RMAKER_NVS_PART_NAME, LOCAL_CTRL_NVS_NAMESPACE, key)
#define __local_ctrl_update_nvs_string(key, value) esp_rmaker_nvs_update_string(RMAKER_NVS_PART_NAME, LOCAL_CTRL_NVS_NAMESPACE, key, value)

/**
 * @brief Write the PoP into the private data.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
static esp_rmaker_error_t __local_ctrl_get_pop(void)
{
    if (__priv_data == NULL) {
        OSAL_LOGE(TAG, "Private data is NULL");
        return ESP_RMAKER_INVALID_STATE;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    char *pop = __priv_data->pop;

    /* Reached only via __core_start(), which returns early when __priv_data is already
     * set, so pop is always NULL here (fresh calloc) - nothing cached to validate. */

    /* Use application configured PoP, if provided. */
    if (__custom_pop != NULL) {
        pop = OSAL_STRDUP_EXTRAM(__custom_pop);
        if (pop == NULL) {
            OSAL_LOGE(TAG, "Failed to allocate memory for custom PoP");
            return ESP_RMAKER_NO_MEM;
        }
        __priv_data->pop = pop;
        __priv_data->pop_from_nvs = false;
        return ESP_RMAKER_OK;
    }

    /* Attempt to get PoP from NVS */
    pop = __local_ctrl_get_nvs_string(LOCAL_CTRL_NVS_POP_KEY);
    bool pop_loaded_from_nvs = (pop != NULL && strlen(pop) == LOCAL_CTRL_POP_CHARS);

    /* Generate a new PoP if not found in NVS or the length is not correct */
    if (!pop_loaded_from_nvs) {
        OSAL_LOGI(TAG, "PoP not found in NVS or length is not correct, generating a new one");
        if (pop) {
            free(pop);
        }

        /* Allocate for the full hex encoding; an odd character count leaves one spare
         * character that is trimmed below. */
        pop = OSAL_CALLOC_EXTRAM(LOCAL_CTRL_POP_BYTES * 2 + 1, sizeof(char));
        if (pop == NULL) {
            OSAL_LOGE(TAG, "Failed to allocate memory for PoP");
            err = ESP_RMAKER_NO_MEM;
            goto __local_ctrl_get_pop_fail;
        }

        /* Generate random bytes for the PoP */
        uint8_t random_bytes[LOCAL_CTRL_POP_BYTES];
        osal_random_fill(random_bytes, LOCAL_CTRL_POP_BYTES);
        err = esp_rmaker_convert_bytes_to_hex(random_bytes, LOCAL_CTRL_POP_BYTES, pop, LOCAL_CTRL_POP_BYTES * 2 + 1);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to convert random bytes to hex");
            goto __local_ctrl_get_pop_fail;
        }
        /* Trim to the configured character count (no-op when it is even). */
        pop[LOCAL_CTRL_POP_CHARS] = '\0';

        /* Write the PoP to NVS */
        err = __local_ctrl_update_nvs_string(LOCAL_CTRL_NVS_POP_KEY, pop);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to update PoP in NVS");
            goto __local_ctrl_get_pop_fail;
        }
    }

    /* Store the PoP in the private data */
    __priv_data->pop = pop;
    __priv_data->pop_from_nvs = pop_loaded_from_nvs;
    return ESP_RMAKER_OK;

__local_ctrl_get_pop_fail:
    if (pop) {
        free(pop);
    }
    __priv_data->pop = NULL;
    return err;
}
#endif /* LOCAL_CTRL_POP_IN_USE */

static uint16_t __local_ctrl_effective_http_port(void)
{
#if CONFIG_RMNG_HOST_CTRL
    if (s_local_ctrl_http_port_override != 0) {
        return s_local_ctrl_http_port_override;
    }
#endif /* CONFIG_RMNG_HOST_CTRL */
    return (uint16_t) CONFIG_ESP_RMAKER_LOCAL_CTRL_HTTP_PORT;
}

/* Session security *******************************************************/

#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2
/* Declared in local_ctrl/sec2_cache.h - not static, so the caching rule below can be
 * unit-tested without standing up the HTTP server and mDNS. */
esp_rmaker_error_t esp_rmaker_local_ctrl_sec2_resolve_salt_verifier(
    const char *pop, bool pop_from_nvs, bool pop_is_custom,
    char **out_salt, char **out_verifier, int *out_verifier_len)
{
    if (pop == NULL || out_salt == NULL || out_verifier == NULL || out_verifier_len == NULL) {
        return ESP_RMAKER_INVALID_ARG;
    }

    char *salt = NULL;
    char *verifier = NULL;
    int verifier_len = 0;

    /* Reuse the cached pair only if the PoP itself came from NVS: if it was regenerated this
     * run the SRP password changed, so the cache no longer belongs to it. */
    bool reuse = pop_from_nvs;
    if (reuse) {
        size_t salt_len = 0;
        size_t cached_verifier_len = 0;
        salt = (char *) esp_rmaker_nvs_get_binary(RMAKER_NVS_PART_NAME, LOCAL_CTRL_NVS_NAMESPACE,
                LOCAL_CTRL_NVS_SEC2_SALT_KEY, &salt_len);
        verifier = (char *) esp_rmaker_nvs_get_binary(RMAKER_NVS_PART_NAME, LOCAL_CTRL_NVS_NAMESPACE,
                   LOCAL_CTRL_NVS_SEC2_VERIFIER_KEY, &cached_verifier_len);
        if (salt == NULL || salt_len != LOCAL_CTRL_SEC2_SALT_LEN ||
                verifier == NULL || cached_verifier_len == 0) {
            OSAL_LOGI(TAG, "Cached SRP6a salt/verifier missing or invalid; regenerating");
            free(salt);
            free(verifier);
            salt = NULL;
            verifier = NULL;
            reuse = false;
        } else {
            verifier_len = (int) cached_verifier_len;
            OSAL_LOGI(TAG, "Reusing cached SRP6a salt/verifier from NVS");
        }
    }

    if (!reuse) {
        esp_err_t srp_err = esp_srp_gen_salt_verifier(
                                LOCAL_CTRL_SEC2_USERNAME, strlen(LOCAL_CTRL_SEC2_USERNAME),
                                pop, strlen(pop),
                                &salt, LOCAL_CTRL_SEC2_SALT_LEN,
                                &verifier, &verifier_len);
        if (srp_err != ESP_OK || salt == NULL || verifier == NULL) {
            OSAL_LOGE(TAG, "esp_srp_gen_salt_verifier failed: %d", srp_err);
            free(salt);
            free(verifier);
            return ESP_RMAKER_FAIL;
        }

        /* Cache for the next boot - but only for a PoP that is in NVS. A custom PoP lives in
         * RAM only (esp_rmaker_local_ctrl_set_pop()), so caching a pair derived from it would
         * leave NVS holding one PoP and a verifier for another; the next start without a
         * custom PoP would then reuse that pair and fail every SEC2 handshake. */
        if (pop_is_custom) {
            OSAL_LOGI(TAG, "Custom PoP in use; not caching SRP6a salt/verifier");
        } else {
            esp_rmaker_error_t nvs_err = esp_rmaker_nvs_update_binary(RMAKER_NVS_PART_NAME, LOCAL_CTRL_NVS_NAMESPACE,
                                         LOCAL_CTRL_NVS_SEC2_SALT_KEY, salt, LOCAL_CTRL_SEC2_SALT_LEN);
            if (nvs_err != ESP_RMAKER_OK) {
                OSAL_LOGW(TAG, "Failed to cache SRP6a salt to NVS: %d", nvs_err);
            }
            nvs_err = esp_rmaker_nvs_update_binary(RMAKER_NVS_PART_NAME, LOCAL_CTRL_NVS_NAMESPACE,
                                                   LOCAL_CTRL_NVS_SEC2_VERIFIER_KEY, verifier, (size_t) verifier_len);
            if (nvs_err != ESP_RMAKER_OK) {
                OSAL_LOGW(TAG, "Failed to cache SRP6a verifier to NVS: %d", nvs_err);
            }
        }
    }

    *out_salt = salt;
    *out_verifier = verifier;
    *out_verifier_len = verifier_len;
    return ESP_RMAKER_OK;
}
#endif /* CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2 */

/**
 * @brief Set up session security on the protocomm instance per the configured version.
 */
static esp_rmaker_error_t __core_setup_security(void)
{
    esp_err_t err = ESP_FAIL;

#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_1
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC1_POP
    esp_rmaker_error_t rerr = __local_ctrl_get_pop();
    if (rerr != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get PoP");
        return rerr;
    }
    __priv_data->sec1_params.data = (const uint8_t *) __priv_data->pop;
    __priv_data->sec1_params.len = strlen(__priv_data->pop);
    err = protocomm_set_security(__priv_data->pc, LOCAL_CTRL_SECURITY_ENDPOINT, &protocomm_security1,
                                 &__priv_data->sec1_params);
#else /* !CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC1_POP */
    OSAL_LOGI(TAG, "Security 1 configured without PoP");
    err = protocomm_set_security(__priv_data->pc, LOCAL_CTRL_SECURITY_ENDPOINT, &protocomm_security1, NULL);
#endif /* CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC1_POP */
#elif CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2
    esp_rmaker_error_t rerr = __local_ctrl_get_pop();
    if (rerr != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get PoP");
        return rerr;
    }

    char *sec2_salt = NULL;
    char *sec2_verifier = NULL;
    int sec2_verifier_len = 0;

    rerr = esp_rmaker_local_ctrl_sec2_resolve_salt_verifier(
               __priv_data->pop, __priv_data->pop_from_nvs, __custom_pop != NULL,
               &sec2_salt, &sec2_verifier, &sec2_verifier_len);
    if (rerr != ESP_RMAKER_OK) {
        return rerr;
    }

    __priv_data->sec2_salt = sec2_salt;
    __priv_data->sec2_salt_len = LOCAL_CTRL_SEC2_SALT_LEN;
    __priv_data->sec2_verifier = sec2_verifier;
    __priv_data->sec2_verifier_len = sec2_verifier_len;

    __priv_data->sec2_params.salt = sec2_salt;
    __priv_data->sec2_params.salt_len = (uint16_t) LOCAL_CTRL_SEC2_SALT_LEN;
    __priv_data->sec2_params.verifier = sec2_verifier;
    __priv_data->sec2_params.verifier_len = (uint16_t) sec2_verifier_len;
    err = protocomm_set_security(__priv_data->pc, LOCAL_CTRL_SECURITY_ENDPOINT, &protocomm_security2,
                                 &__priv_data->sec2_params);
#else
#error "Local control requires security version 1 or 2"
#endif

    if (err != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to set session security: %d", (int) err);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

/* Version endpoint + advertisement *******************************************************/

/**
 * @brief (Re)set the version endpoint and (re)advertise the discovery service
 *        to reflect the currently active endpoint sets.
 */
static esp_rmaker_error_t __core_refresh_advert(void)
{
    if (__priv_data == NULL || __priv_data->pc == NULL) {
        return ESP_RMAKER_INVALID_STATE;
    }

    int sec_ver = 0;
    uint8_t sec_patch_ver = 0;
    protocomm_get_sec_version(__priv_data->pc, &sec_ver, &sec_patch_ver);

    /* Build the cap list for the version endpoint and the TXT record. */
    char caps_json[96] = "";
    char caps_txt[32] = "";
    if (__priv_data->local_ctrl_active) {
        strlcat(caps_json, LOCAL_CTRL_CAPS_LOCAL_CTRL, sizeof(caps_json));
        strlcat(caps_txt, LOCAL_CTRL_TXT_CAP_LOCAL_CTRL, sizeof(caps_txt));
    }
    if (__priv_data->chal_resp_active) {
        if (caps_json[0] != '\0') {
            strlcat(caps_json, ",", sizeof(caps_json));
            strlcat(caps_txt, ",", sizeof(caps_txt));
        }
        strlcat(caps_json, "\"" LOCAL_CTRL_TXT_CAP_CHAL_RESP "\"", sizeof(caps_json));
        strlcat(caps_txt, LOCAL_CTRL_TXT_CAP_CHAL_RESP, sizeof(caps_txt));
    }
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_1 && !CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC1_POP
    if (caps_json[0] != '\0') {
        strlcat(caps_json, ",", sizeof(caps_json));
    }
    strlcat(caps_json, "\"no_pop\"", sizeof(caps_json));
#endif

    /* (Re)set the version endpoint */
    int size = snprintf(NULL, 0, LOCAL_CTRL_VERSION_FMT, sec_ver, (unsigned) sec_patch_ver, caps_json) + 1;
    char *version_json = OSAL_CALLOC_EXTRAM(size, sizeof(char));
    if (version_json == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for version string");
        return ESP_RMAKER_NO_MEM;
    }
    snprintf(version_json, size, LOCAL_CTRL_VERSION_FMT, sec_ver, (unsigned) sec_patch_ver, caps_json);

    /* The phone apps read `cap` from here, not only from the TXT record, and the TXT copy
     * cannot replace it: the same handlers are meant to run over BLE, where there is no
     * mDNS and therefore no TXT record, so this endpoint is the only capability source on
     * that transport. Even on HTTP a client may have connected by IP from a QR code without
     * browsing, or hold a TXT record that has since gone stale. So this is the authority,
     * and has to be rewritten whenever an endpoint set toggles.
     *
     * protocomm_set_version() refuses when a version is already set, so the old one has to
     * come off first. Keep the old string until the new one is accepted, and put it back if
     * the set fails: a registered payload with a slightly stale cap list still lets a client
     * complete its connect flow, whereas no /version at all breaks it outright. */
    char *old_version_json = __priv_data->version_json;
    if (old_version_json != NULL) {
        protocomm_unset_version(__priv_data->pc, LOCAL_CTRL_VERSION_ENDPOINT);
        __priv_data->version_json = NULL;
    }
    esp_err_t err = protocomm_set_version(__priv_data->pc, LOCAL_CTRL_VERSION_ENDPOINT, version_json);
    if (err != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to set version endpoint: %d", (int) err);
        free(version_json);
        if (old_version_json != NULL) {
            if (protocomm_set_version(__priv_data->pc, LOCAL_CTRL_VERSION_ENDPOINT,
                                      old_version_json) == ESP_OK) {
                __priv_data->version_json = old_version_json;
                OSAL_LOGW(TAG, "Restored the previous version endpoint payload");
            } else {
                OSAL_LOGE(TAG, "Version endpoint is now unregistered");
                free(old_version_json);
            }
        }
        return ESP_RMAKER_FAIL;
    }
    /* protocomm strdup()s the string, so this copy is ours to free on the next refresh
     * or at teardown. */
    free(old_version_json);
    __priv_data->version_json = version_json;

    /* (Re)advertise with the updated cap TXT record */
    osal_discovery_txt_item_t txt_list[2] = {
        { .var = RMAKER_DISCOVERY_LOCAL_CTRL_TXT_ITEM_NODE_ID, .val = __priv_data->name },
        { .var = RMAKER_DISCOVERY_LOCAL_CTRL_TXT_ITEM_CAP, .val = caps_txt },
    };
    osal_discovery_txt_items_t txt_items = { .list = txt_list, .count = 2 };
    if (__priv_data->advertised) {
        (void) osal_discovery_remove_service(RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_TYPE,
                                             RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_PROTOCOL);
        __priv_data->advertised = false;
    }
    osal_err_t derr = osal_discovery_add_service(RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_TYPE,
                      RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_PROTOCOL,
                      __priv_data->name, &txt_items);
    if (derr != OSAL_ERR_OK) {
        OSAL_LOGW(TAG, "Failed to advertise service: %d (still reachable by IP)", (int) derr);
    } else {
        __priv_data->advertised = true;
    }
    return ESP_RMAKER_OK;
}

/* Core lifecycle *******************************************************/

/**
 * @brief Start the shared protocomm instance (idempotent).
 */
static esp_rmaker_error_t __core_start(void)
{
    if (__priv_data != NULL) {
        return ESP_RMAKER_OK;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;

    __priv_data = OSAL_CALLOC_EXTRAM(1, sizeof(__local_ctrl_service_priv_data_t));
    if (__priv_data == NULL) {
        OSAL_LOGE(TAG, "Failed to allocate memory for private data");
        return ESP_RMAKER_NO_MEM;
    }

    /* Hostname / instance name: the node ID */
    esp_rmaker_credentials_get_thing_name(&__priv_data->name);
    if (__priv_data->name == NULL) {
        OSAL_LOGE(TAG, "Failed to get thing name");
        err = ESP_RMAKER_INVALID_STATE;
        goto __core_start_fail;
    }
    __priv_data->port = __local_ctrl_effective_http_port();

    /* Initialize discovery: hostname = node ID (service advertised on refresh) */
    osal_discovery_service_config_t disc_cfg = {
        .name = __priv_data->name,
        .type = RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_TYPE,
        .protocol = RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_PROTOCOL,
        .port = __priv_data->port,
        .txt_items = { NULL, 0 },
    };
    osal_err_t derr = osal_discovery_init(&disc_cfg);
    if (derr != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "osal_discovery_init failed: %d", (int) derr);
        err = ESP_RMAKER_FAIL;
        goto __core_start_fail;
    }
    __priv_data->discovery_initialized = true;

    /* Create and start the protocomm instance owned by this service */
    __priv_data->pc = protocomm_new();
    if (__priv_data->pc == NULL) {
        OSAL_LOGE(TAG, "Failed to create protocomm instance");
        err = ESP_RMAKER_NO_MEM;
        goto __core_start_fail;
    }

    protocomm_httpd_config_t httpd_config = {
        .ext_handle_provided = false,
        .data = {
            .config = {
                .port = __priv_data->port,
                .stack_size = LOCAL_CTRL_STACK_SIZE,
#ifdef CONFIG_IDF_TARGET
                .task_priority = tskIDLE_PRIORITY + 5,
#else
                .task_priority = 5,
#endif
            },
        },
    };
    esp_err_t pc_err = protocomm_httpd_start(__priv_data->pc, &httpd_config);
    if (pc_err != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to start HTTP server: %d", (int) pc_err);
        protocomm_delete(__priv_data->pc);
        __priv_data->pc = NULL;
        err = ESP_RMAKER_FAIL;
        goto __core_start_fail;
    }

    /* Session security */
    err = __core_setup_security();
    if (err != ESP_RMAKER_OK) {
        goto __core_start_fail;
    }

    return ESP_RMAKER_OK;

__core_start_fail:
    /* Neither endpoint set is active yet, so this tears down whatever was brought up
     * (protocomm, discovery, private data) - no need to repeat any of it here. */
    __core_stop_if_idle();
    return err;
}

/**
 * @brief Free the private data.
 */
static void __local_ctrl_free_priv_data(void)
{
    if (__priv_data) {
        if (__priv_data->name) {
            free(__priv_data->name);
        }
        if (__priv_data->pop) {
            free(__priv_data->pop);
        }
        if (__priv_data->version_json) {
            free(__priv_data->version_json);
        }
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2
        if (__priv_data->sec2_salt) {
            free(__priv_data->sec2_salt);
        }
        if (__priv_data->sec2_verifier) {
            free(__priv_data->sec2_verifier);
        }
#endif
        free(__priv_data);
        __priv_data = NULL;
    }
}

/**
 * @brief Stop and free the shared instance when no feature is active.
 */
static void __core_stop_if_idle(void)
{
    if (__priv_data == NULL || __priv_data->local_ctrl_active || __priv_data->chal_resp_active) {
        return;
    }

    if (__priv_data->stop_task_handle != NULL) {
        (void) osal_scheduler_cancel_task(&__priv_data->stop_task_handle);
    }

    if (__priv_data->advertised) {
        (void) osal_discovery_remove_service(RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_TYPE,
                                             RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_PROTOCOL);
        __priv_data->advertised = false;
    }
    if (__priv_data->pc != NULL) {
        if (__priv_data->version_json != NULL) {
            protocomm_unset_version(__priv_data->pc, LOCAL_CTRL_VERSION_ENDPOINT);
        }
        protocomm_unset_security(__priv_data->pc, LOCAL_CTRL_SECURITY_ENDPOINT);
        protocomm_httpd_stop(__priv_data->pc);
        protocomm_delete(__priv_data->pc);
        __priv_data->pc = NULL;
    }
    if (__priv_data->discovery_initialized) {
        osal_err_t derr = osal_discovery_deinit();
        if (derr != OSAL_ERR_OK) {
            OSAL_LOGW(TAG, "osal_discovery_deinit failed: %d", (int) derr);
        }
        __priv_data->discovery_initialized = false;
    }
    __local_ctrl_free_priv_data();

#if CONFIG_RMNG_HOST_CTRL
    s_local_ctrl_http_port_override = 0;
#endif /* CONFIG_RMNG_HOST_CTRL */
}

/* Challenge-response feature *******************************************************/

/**
 * @brief Delayed chal-resp service disable, so the final response gets
 *        flushed before a chal-only instance is torn down.
 */
static esp_rmaker_error_t __chal_resp_service_disable(bool cancel_stop_task);

static void __chal_resp_stop_work_task(void *arg)
{
    (void) arg;
    (void) __chal_resp_service_disable(true);
}

static void __chal_resp_delayed_stop_scheduler_cb(void *arg)
{
    (void) arg;
    if (esp_rmaker_work_queue_add_task(__chal_resp_stop_work_task, NULL) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Work queue unavailable; disabling challenge-response service immediately");
        /* Running on the scheduler's own callback thread: cancelling the task here
         * would join and free the thread currently executing this function. Leave the
         * handle for __core_stop_if_idle() or the next teardown to release. */
        (void) __chal_resp_service_disable(false);
    }
}

/**
 * @brief Challenge response handler: forwards to the core handler; a
 *        client-issued disable schedules the (delayed) feature disable.
 */
static esp_err_t __local_ctrl_chal_resp_handler(
    uint32_t session_id,
    const uint8_t *inbuf,
    ssize_t inlen,
    uint8_t **outbuf,
    ssize_t *outlen,
    void *priv_data)
{
    bool was_enabled = !esp_rmaker_chal_resp_is_disabled();
    esp_rmaker_error_t err = esp_rmaker_chal_resp_handler(session_id, inbuf, inlen, outbuf, outlen, priv_data);

    if (was_enabled && esp_rmaker_chal_resp_is_disabled() &&
            __priv_data != NULL && !__priv_data->stop_scheduled) {
        /* Never tear down from within the handler: the endpoint (and possibly
         * the whole chal-only instance) is removed from the work queue after a
         * delay, so this response is flushed first. */
        if (osal_scheduler_schedule_task(&__priv_data->stop_task_handle, LOCAL_CTRL_STOP_DELAY_MS,
                                         __chal_resp_delayed_stop_scheduler_cb, NULL) == OSAL_ERR_OK) {
            __priv_data->stop_scheduled = true;
            OSAL_LOGI(TAG, "Challenge-response disable scheduled in %u ms", (unsigned) LOCAL_CTRL_STOP_DELAY_MS);
        } else {
            OSAL_LOGE(TAG, "Failed to schedule challenge-response disable");
        }
    }

    return (err == ESP_RMAKER_OK) ? ESP_OK : ESP_FAIL;
}

/* Local control endpoint wrappers *******************************************************/

/* Protocomm requires esp_err_t-returning handlers; the endpoint handlers return
 * esp_rmaker_error_t. The codes are unrelated enum spaces, so map explicitly
 * rather than casting: protocomm only distinguishes success from failure (a
 * failure tears down the session). */
static esp_err_t __to_protocomm_err(esp_rmaker_error_t err)
{
    return (err == ESP_RMAKER_OK) ? ESP_OK : ESP_FAIL;
}

static esp_err_t __local_ctrl_get_params_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
        uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    return __to_protocomm_err(esp_rmaker_local_ctrl_get_params_ep_handler(session_id, inbuf, inlen, outbuf, outlen,
                              priv_data));
}

static esp_err_t __local_ctrl_set_params_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
        uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    return __to_protocomm_err(esp_rmaker_local_ctrl_set_params_ep_handler(session_id, inbuf, inlen, outbuf, outlen,
                              priv_data));
}

static esp_err_t __local_ctrl_get_config_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
        uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    return __to_protocomm_err(esp_rmaker_local_ctrl_get_config_ep_handler(session_id, inbuf, inlen, outbuf, outlen,
                              priv_data));
}

/* Public function definitions *******************************************************/

esp_rmaker_error_t esp_rmaker_local_ctrl_set_pop(const char *pop)
{
    if (pop != NULL && strlen(pop) == 0) {
        OSAL_LOGE(TAG, "PoP cannot be an empty string");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* The PoP is read once, while the service starts. Accepting a change afterwards
     * would silently do nothing (and for security 2 would not match the cached SRP6a
     * verifier), so refuse instead of pretending to apply it. */
    if (__priv_data != NULL) {
        OSAL_LOGE(TAG, "Local endpoints service already started; PoP must be set before enabling it");
        return ESP_RMAKER_INVALID_STATE;
    }

    char *new_pop = NULL;
    if (pop != NULL) {
        new_pop = OSAL_STRDUP_EXTRAM(pop);
        if (new_pop == NULL) {
            OSAL_LOGE(TAG, "Failed to allocate memory for custom PoP");
            return ESP_RMAKER_NO_MEM;
        }
    }

    if (__custom_pop != NULL) {
        free(__custom_pop);
    }
    __custom_pop = new_pop;

    return ESP_RMAKER_OK;
}

#if CONFIG_RMNG_HOST_CTRL
esp_rmaker_error_t esp_rmaker_local_ctrl_set_http_port_from_host_ctrl(int port)
{
    if (port < 1 || port > 65535) {
        OSAL_LOGE(TAG, "Invalid local control HTTP port: %d", port);
        return ESP_RMAKER_INVALID_ARG;
    }
    /* The HTTP port can only be applied at start time. If the instance is already running
     * (e.g. left over from a previous test on a reused instance), stop it first so the new
     * port takes effect on the next enable instead of rejecting the request and leaving a
     * stale, wrong-port instance bound. */
    if (__priv_data != NULL) {
        OSAL_LOGW(TAG, "Local endpoints service running; stopping to apply HTTP port %d", port);
        esp_rmaker_error_t stop_err = esp_rmaker_chal_resp_service_disable();
        if (stop_err == ESP_RMAKER_OK) {
            stop_err = esp_rmaker_local_ctrl_service_disable();
        }
        if (stop_err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to stop service before setting HTTP port: %d", (int) stop_err);
            return stop_err;
        }
    }
    /* Stopping clears s_local_ctrl_http_port_override, so assign after stopping. */
    s_local_ctrl_http_port_override = (uint16_t) port;
    return ESP_RMAKER_OK;
}
#endif /* CONFIG_RMNG_HOST_CTRL */

esp_rmaker_error_t esp_rmaker_local_ctrl_service_enable(void)
{
    if (__priv_data != NULL && __priv_data->local_ctrl_active) {
        OSAL_LOGW(TAG, "Local control is already enabled");
        return ESP_RMAKER_OK;
    }

    esp_rmaker_error_t err = __core_start();
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    /* Register the endpoint-protocol handlers */
    if (protocomm_add_endpoint(__priv_data->pc, RMAKER_LOCAL_CTRL_GET_PARAMS_ENDPOINT,
                               __local_ctrl_get_params_handler, NULL) != ESP_OK ||
            protocomm_add_endpoint(__priv_data->pc, RMAKER_LOCAL_CTRL_SET_PARAMS_ENDPOINT,
                                   __local_ctrl_set_params_handler, NULL) != ESP_OK ||
            protocomm_add_endpoint(__priv_data->pc, RMAKER_LOCAL_CTRL_GET_CONFIG_ENDPOINT,
                                   __local_ctrl_get_config_handler, NULL) != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to register local control endpoints");
        protocomm_remove_endpoint(__priv_data->pc, RMAKER_LOCAL_CTRL_GET_PARAMS_ENDPOINT);
        protocomm_remove_endpoint(__priv_data->pc, RMAKER_LOCAL_CTRL_SET_PARAMS_ENDPOINT);
        __core_stop_if_idle();
        return ESP_RMAKER_FAIL;
    }
    __priv_data->local_ctrl_active = true;

    err = __core_refresh_advert();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to refresh advertisement: %d", (int) err);
    }

    /* Add the local control service to the node */
#if CONFIG_ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2
    const char *sec2_username = LOCAL_CTRL_SEC2_USERNAME;
    esp_rmaker_local_ctrl_sec_t sec_ver = ESP_RMAKER_LOCAL_CTRL_SEC2;
#else
    const char *sec2_username = NULL;
    esp_rmaker_local_ctrl_sec_t sec_ver = ESP_RMAKER_LOCAL_CTRL_SEC1;
#endif
    err = esp_rmaker_local_ctrl_service_add_to_node(sec_ver, __priv_data->pop, sec2_username);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add local control service to node");
        (void) __local_ctrl_service_disable(false);
        return err;
    }

    /* Post the local control started event */
    char *service_name = __priv_data->name;
    osal_event_post(RMAKER_EVENT, RMAKER_EVENT_LOCAL_CTRL_STARTED, service_name, strlen(service_name) + 1, OSAL_MAX_DELAY);
    OSAL_LOGI(TAG, "Local control started: %s.%s port %u",
              RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_TYPE, RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_PROTOCOL,
              (unsigned) __priv_data->port);
    return ESP_RMAKER_OK;
}

/**
 * @brief Disable the local control endpoints.
 *
 * @param[in] post_stopped_event Post RMAKER_EVENT_LOCAL_CTRL_STOPPED. Pass false when
 *            unwinding a failed enable: STARTED has not been posted yet, and an
 *            unpaired STOPPED would look like a transition to an app that tracks the
 *            pair (examples enable/disable challenge-response off these two events).
 */
static esp_rmaker_error_t __local_ctrl_service_disable(bool post_stopped_event)
{
    /* Idempotent: safe to call on an already-stopped service (host-control
     * disable-all-services / reset path). */
    if (__priv_data == NULL || !__priv_data->local_ctrl_active) {
        OSAL_LOGW(TAG, "Local control is not enabled");
        return ESP_RMAKER_OK;
    }

    /* Drop any cached endpoint-protocol transfer data and the endpoints */
    esp_rmaker_local_ctrl_endpoints_free_data();
    protocomm_remove_endpoint(__priv_data->pc, RMAKER_LOCAL_CTRL_GET_CONFIG_ENDPOINT);
    protocomm_remove_endpoint(__priv_data->pc, RMAKER_LOCAL_CTRL_SET_PARAMS_ENDPOINT);
    protocomm_remove_endpoint(__priv_data->pc, RMAKER_LOCAL_CTRL_GET_PARAMS_ENDPOINT);
    __priv_data->local_ctrl_active = false;

    /* Remove the local control service from the node */
    esp_rmaker_error_t err = esp_rmaker_local_ctrl_service_remove_from_node();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to remove local control service from node: %d", (int) err);
    }

    if (__priv_data->chal_resp_active) {
        (void) __core_refresh_advert();
    } else {
        __core_stop_if_idle();
    }

    if (post_stopped_event) {
        osal_event_post(RMAKER_EVENT, RMAKER_EVENT_LOCAL_CTRL_STOPPED, NULL, 0, OSAL_MAX_DELAY);
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_local_ctrl_service_disable(void)
{
    return __local_ctrl_service_disable(true);
}

esp_rmaker_error_t esp_rmaker_chal_resp_service_enable(void)
{
    if (__priv_data != NULL && __priv_data->chal_resp_active) {
        OSAL_LOGW(TAG, "Challenge response is already enabled");
        return ESP_RMAKER_OK;
    }

    /* Enable the core module first: it refuses when a client persistently
     * disabled challenge-response (cleared only by a factory reset), so doing
     * it before __core_start() avoids bringing the instance up just to tear it
     * down again. */
    esp_rmaker_error_t err = esp_rmaker_chal_resp_enable();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Challenge response not enabled (disabled by the user?): %d", (int) err);
        return err;
    }

    err = __core_start();
    if (err != ESP_RMAKER_OK) {
        (void) esp_rmaker_chal_resp_disable();
        return err;
    }

    esp_err_t pc_err = protocomm_add_endpoint(__priv_data->pc, RMAKER_CHAL_RESP_ENDPOINT_NAME,
                       __local_ctrl_chal_resp_handler, NULL);
    if (pc_err != ESP_OK) {
        OSAL_LOGE(TAG, "Failed to add challenge response endpoint");
        (void) esp_rmaker_chal_resp_disable();
        __core_stop_if_idle();
        return ESP_RMAKER_FAIL;
    }
    __priv_data->chal_resp_active = true;

    err = __core_refresh_advert();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to refresh advertisement: %d", (int) err);
    }

    OSAL_LOGI(TAG, "Challenge-response endpoint enabled on %s.%s port %u",
              RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_TYPE, RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_PROTOCOL,
              (unsigned) __priv_data->port);
    return ESP_RMAKER_OK;
}

/**
 * @brief Disable the challenge-response endpoint.
 *
 * @param[in] cancel_stop_task Release the delayed-stop one-shot. Both scheduler
 *            backends free the task object only in cancel - a fired one-shot does not
 *            clean itself up - and __core_stop_if_idle() (which also cancels) is not
 *            reached while local control is still active, so without this the
 *            both-enabled teardown path leaks the task. Pass false when running on the
 *            scheduler's own callback thread, where cancelling would self-join.
 */
static esp_rmaker_error_t __chal_resp_service_disable(bool cancel_stop_task)
{
    if (__priv_data == NULL || !__priv_data->chal_resp_active) {
        OSAL_LOGW(TAG, "Challenge response is not enabled");
        return ESP_RMAKER_OK;
    }

    (void) esp_rmaker_chal_resp_disable();
    protocomm_remove_endpoint(__priv_data->pc, RMAKER_CHAL_RESP_ENDPOINT_NAME);
    __priv_data->chal_resp_active = false;
    __priv_data->stop_scheduled = false;

    if (cancel_stop_task && __priv_data->stop_task_handle != NULL) {
        (void) osal_scheduler_cancel_task(&__priv_data->stop_task_handle);
    }

    if (__priv_data->local_ctrl_active) {
        (void) __core_refresh_advert();
    } else {
        __core_stop_if_idle();
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_chal_resp_service_disable(void)
{
    return __chal_resp_service_disable(true);
}

bool esp_rmaker_chal_resp_service_is_enabled(void)
{
    return __priv_data != NULL && __priv_data->chal_resp_active;
}
