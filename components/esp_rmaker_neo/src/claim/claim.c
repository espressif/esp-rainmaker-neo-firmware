/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file claim.c
 * @brief Implementation of assisted claiming.
 *
 * The device is a pure responder over the `rmaker_claim` provisioning endpoint; the phone
 * app drives the exchange and proxies it to the ESP RainMaker claiming service. Protocol
 * details are in docs/en/specs/provisioning.md.
 */

/* Includes *******************************************************/

/* Declaration includes */
#include "claim/impl.h"

/* Standard includes */
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* Platform common includes */
#include "osal_log.h"
#include "osal_event_group.h"
#include "osal_event_loop.h"
#include "osal_mem_alloc.h"
#include "osal_random.h"
#include "osal_semaphore.h"
#include "osal_sysinfo.h"
#include "osal_task.h"
#include "osal_ticks.h"

/* Protobuf includes */
#include "claim/pb-c.h"

/* JSON includes */
#include "json_generator.h"
#include "json_parser.h"

/* ESP RainMaker includes */
#include "esp_rmaker_core.h"
#include "esp_rmaker_credentials.h"
#include "esp_rmaker_event_loop.h"
#include "util/esp_rmaker_crypto.h"

/* Prov registry includes */
#include "prov_helpers.h"

/* Configuration includes */
#include "sdkconfig.h"

/* Preprocessor definitions *******************************************************/

#define CLAIM_ENDPOINT           "rmaker_claim"
#define RMAKER_APP_NAME          "rmaker"
#define RMAKER_APP_VERSION       "1.0"

/** Outbound fragment size. Must match what the phone app expects. */
#define CLAIM_FRAGMENT_SIZE      200

/** Length of the general-purpose random blob stored in the factory partition. */
#define CLAIM_RANDOM_NUMBER_LEN  64

/** Stack for the crypto worker. Both key and CSR generation are mbedTLS-heavy. */
#define CLAIM_WORKER_STACK_SIZE  (10 * 1024)

/* Types *******************************************************/

/**
 * @brief Work item handed to the worker task: what to run, and where the result goes.
 */
typedef struct {
    esp_rmaker_error_t (*fn)(esp_rmaker_claim_data_t *claim_data);
    esp_rmaker_claim_data_t *claim_data;
    esp_rmaker_error_t result;
} claim_work_t;

/* Variables *******************************************************/

static const char *TAG = "rmng_claim";

static prov_registration_info_t prov_info;

static const char *__app_info_capabilities[] = { "claim" };

/** The live claim context, or NULL once it has been released.
 *
 * The endpoint stays registered for as long as the provisioning session lives, and under
 * CONFIG_APP_NETWORK_ASYNCHRONOUS_CONNECTION=y esp_rmaker_assisted_claim_perform() runs while
 * that session is still up. So the context can be released with the endpoint still able to
 * fire, and a command arriving afterwards must find NULL rather than freed memory. */
static esp_rmaker_claim_data_t *claim_ctx;

/** Serialises ::claim_ctx between the protocomm task, which runs the commands, and the
 * application task, which releases the context in esp_rmaker_assisted_claim_perform().
 *
 * Held across a whole command, not just the pointer read: without that, a handler that had
 * already loaded the pointer would go on using it while the release ran. Releasing therefore
 * waits for any command in flight, which is exactly the intent. */
static osal_semaphore_handle_t claim_ctx_lock;

/** Guards against a second esp_rmaker_assisted_claim_init() while a claim context is live: the
 * event group and the prov handler registrations are shared, so a second init would leak them.
 * Cleared by esp_rmaker_assisted_claim_perform(), which releases everything it guards --
 * esp_rmaker_pre_prov_init()/deinit() are re-entrant, so a claim that fails must not stop the
 * next provisioning attempt in the same boot from initialising claiming again. */
static bool claim_init_done;

/** Signals the end of the claiming exchange, either way. */
static osal_event_group_handle_t claim_event_group;
/** The claim succeeded: the certificate is persisted and no retry can follow, since
 * ::RMAKER_CLAIM_STATE_VERIFY_DONE is terminal for every command.
 *
 * Set on success only. A rejected certificate or an abort leaves the wait pending on purpose:
 * both are retryable, the phone app is free to start another attempt over the same session, and
 * latching a failure here would consume that attempt's result before it happened -- and, worse,
 * release the context while the retry was running against it. Failure is bounded by
 * ::CLAIM_ABANDONED_BIT instead, which is the point at which a retry stops being possible. */
static const osal_event_group_bits_t CLAIM_DONE_BIT = (1 << 0);
/** The provisioning session ended, so no peer is left to reach a result. Both bits can be
 * set; ::CLAIM_DONE_BIT wins, since a result that arrived is a result either way. */
static const osal_event_group_bits_t CLAIM_ABANDONED_BIT = (1 << 1);
/** Whether a failed attempt has already posted ::RMAKER_EVENT_CLAIM_FAILED, so that giving up
 * in esp_rmaker_assisted_claim_perform() does not post a second one for the same failure. */
static bool claim_failed_posted;

/** Signals completion of the current worker task. See __run_in_worker_task(). */
static osal_event_group_handle_t claim_worker_event_group;
static const osal_event_group_bits_t CLAIM_WORKER_BIT = (1 << 1);

/* Private function declarations *******************************************************/

static esp_rmaker_error_t __escape_new_line(esp_rmaker_claim_data_t *claim_data);
static esp_rmaker_error_t __run_in_worker_task(esp_rmaker_error_t (*fn)(esp_rmaker_claim_data_t *),
        esp_rmaker_claim_data_t *claim_data, uint32_t priority);
static esp_rmaker_error_t __load_or_generate_key(esp_rmaker_claim_data_t *claim_data);
static esp_rmaker_error_t __generate_csr(esp_rmaker_claim_data_t *claim_data);
static esp_rmaker_error_t __generate_claim_init_request(esp_rmaker_claim_data_t *claim_data);
static esp_rmaker_error_t __handle_claim_init_response(esp_rmaker_claim_data_t *claim_data);
static esp_rmaker_error_t __handle_claim_verify_response(esp_rmaker_claim_data_t *claim_data);
static osal_err_t __claim_endpoint_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
        uint8_t **outbuf, ssize_t *outlen, void *priv_data);
static void __claim_event_handler(void *arg, osal_event_base_t event_base,
                                  int32_t event_id, void *event_data);

/* Variables, continued *******************************************************/

/** The endpoint the provisioning registry keeps a pointer to, and hands back to protocomm on
 * every request. It outlives the provisioning session, which outlives the claim context. */
static prov_endpoint_t claim_endpoint = {
    .name = CLAIM_ENDPOINT,
    .handler = __claim_endpoint_handler,
    /* Deliberately NULL: the context reaches the handler through ::claim_ctx, under
     * ::claim_ctx_lock, so it cannot be read after esp_rmaker_assisted_claim_perform() has
     * released it. An endpoint that outlives the context must not carry a pointer to it. */
    .priv_data = NULL,
    .app_info = {
        .label = RMAKER_APP_NAME,
        .version = RMAKER_APP_VERSION,
        .capabilities = __app_info_capabilities,
        .total_capabilities = sizeof(__app_info_capabilities) / sizeof(__app_info_capabilities[0]),
    },
};

/* Private function definitions *******************************************************/

/**
 * @brief Escape the CSR's newlines in place, using `payload` as scratch.
 *
 * Must not be called while `payload` holds anything live.
 */
static esp_rmaker_error_t __escape_new_line(esp_rmaker_claim_data_t *claim_data)
{
    esp_rmaker_error_t err = claim_rules_escape_new_line(claim_data->payload, sizeof(claim_data->payload),
                             (char *)claim_data->csr);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to escape the CSR: %d", err);
        return err;
    }

    size_t escaped_len = strlen(claim_data->payload);
    if (escaped_len + 1 > sizeof(claim_data->csr)) {
        OSAL_LOGE(TAG, "Escaped CSR is %" PRIu32 " bytes, larger than the %" PRIu32 "-byte CSR buffer.",
                  (uint32_t)escaped_len, (uint32_t)sizeof(claim_data->csr));
        return ESP_RMAKER_NO_MEM;
    }
    memcpy(claim_data->csr, claim_data->payload, escaped_len + 1);
    OSAL_LOGD(TAG, "Escaped CSR: %s", claim_data->csr);
    return ESP_RMAKER_OK;
}

/**
 * @brief Generate the CSR for the node's current node ID.
 */
static esp_rmaker_error_t __generate_csr(esp_rmaker_claim_data_t *claim_data)
{
    char *node_id = esp_rmaker_get_node_id();
    if (!node_id) {
        OSAL_LOGE(TAG, "Could not read the node ID for the CSR subject.");
        return ESP_RMAKER_FAIL;
    }

    uint8_t *csr_pem = NULL;
    size_t csr_pem_len = 0;
    esp_rmaker_error_t err = esp_rmaker_crypto_gen_csr_pem(claim_data->key_pem, claim_data->key_pem_len,
                             node_id, &csr_pem, &csr_pem_len);
    free(node_id);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to generate the CSR: %d", err);
        return err;
    }
    if (csr_pem_len > sizeof(claim_data->csr)) {
        OSAL_LOGE(TAG, "CSR too large: %" PRIu32 " > %" PRIu32,
                  (uint32_t)csr_pem_len, (uint32_t)sizeof(claim_data->csr));
        free(csr_pem);
        return ESP_RMAKER_NO_MEM;
    }

    memset(claim_data->csr, 0, sizeof(claim_data->csr));
    memcpy(claim_data->csr, csr_pem, csr_pem_len);
    free(csr_pem);
    /* Deliberately no state change: the caller advances to INIT_DONE once the CSR has also
     * been escaped into the verify request. Leaving the state at INIT until then is what lets
     * the app retry a failed init with a fresh TypeCmdClaimInit. */
    return ESP_RMAKER_OK;
}

/**
 * @brief Worker task body: run one work item, publish its result, exit.
 */
static void __claim_worker_task(void *args)
{
    claim_work_t *work = (claim_work_t *)args;
    work->result = work->fn(work->claim_data);
    osal_event_group_set_bits(claim_worker_event_group, CLAIM_WORKER_BIT);
    osal_task_delete(NULL);
}

/**
 * @brief Run an expensive crypto step on a dedicated task and wait for it to finish.
 *
 * Both callers are on threads whose stacks are far too small for mbedTLS key/CSR work:
 * key generation is driven from the application's main task (3.5 kB, less on some targets)
 * and CSR generation from the BLE/protocomm endpoint handler (4 kB). Key generation
 * additionally runs at idle priority so that however long it takes, it cannot starve the
 * idle task and trip the task watchdog.
 *
 * This is stack borrowing, not concurrency: the caller blocks until the worker exits, and
 * the two work kinds happen at different points in the claim flow, so they never overlap
 * and can share one event group.
 *
 * @param[in] fn The work to perform.
 * @param[in] claim_data The claiming context to operate on.
 * @param[in] priority Priority for the worker task.
 * @return The work item's result, or an error if the task could not be started.
 */
static esp_rmaker_error_t __run_in_worker_task(esp_rmaker_error_t (*fn)(esp_rmaker_claim_data_t *),
        esp_rmaker_claim_data_t *claim_data, uint32_t priority)
{
    claim_worker_event_group = osal_event_group_create();
    if (!claim_worker_event_group) {
        OSAL_LOGE(TAG, "Couldn't create the claim worker event group.");
        return ESP_RMAKER_NO_MEM;
    }

    claim_work_t work = {
        .fn = fn,
        .claim_data = claim_data,
        .result = ESP_RMAKER_FAIL,
    };

    if (osal_task_create(&__claim_worker_task, "claim_worker", CLAIM_WORKER_STACK_SIZE,
                         &work, priority, NULL) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Couldn't create the claim worker task.");
        osal_event_group_delete(claim_worker_event_group);
        claim_worker_event_group = NULL;
        return ESP_RMAKER_NO_MEM;
    }

    /* `work` lives on this stack, so the wait must not be abandoned. */
    osal_event_group_wait_bits(claim_worker_event_group, CLAIM_WORKER_BIT, false, true, OSAL_MAX_DELAY);
    /* Unlike esp-rainmaker, delete the group on the success path too, rather than leaking
     * one per claim. */
    osal_event_group_delete(claim_worker_event_group);
    claim_worker_event_group = NULL;

    return work.result;
}

/**
 * @brief Build the claim-init request that the phone app POSTs to claim/initiate.
 *
 * Format: {"mac_addr":"AABBCC112233","platform":"<target>"}
 */
static esp_rmaker_error_t __generate_claim_init_request(esp_rmaker_claim_data_t *claim_data)
{
    if (claim_data->state < RMAKER_CLAIM_STATE_PK_GENERATED) {
        return ESP_RMAKER_INVALID_STATE;
    }

    uint8_t mac_addr[OSAL_MAC_ADDR_LEN];
    if (osal_sysinfo_get_base_mac(mac_addr, sizeof(mac_addr)) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Could not fetch the MAC address.");
        return ESP_RMAKER_FAIL;
    }

    snprintf(claim_data->payload, sizeof(claim_data->payload),
             "{\"mac_addr\":\"%02X%02X%02X%02X%02X%02X\",\"platform\":\"%s\"}",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
             osal_sysinfo_get_platform_name());
    claim_data->payload_len = strlen(claim_data->payload);
    claim_data->payload_offset = 0;
    return ESP_RMAKER_OK;
}

/**
 * @brief Handle the claim-init response and build the claim-verify request.
 *
 * Init response: {"node_id":"<cloud-assigned-node-id>"}
 * Verify request: {"csr":"<escaped PEM CSR>","send_mqtt_host":true}
 */
static esp_rmaker_error_t __handle_claim_init_response(esp_rmaker_claim_data_t *claim_data)
{
    OSAL_LOGD(TAG, "Claim init response: %s", claim_data->payload);

    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, claim_data->payload, (int)strlen(claim_data->payload)) != 0) {
        OSAL_LOGE(TAG, "Failed to parse the claim init response.");
        return ESP_RMAKER_FAIL;
    }

    char node_id[64];
    int ret = json_obj_get_string(&jctx, "node_id", node_id, sizeof(node_id));
    json_parse_end(&jctx);
    if (ret != 0) {
        OSAL_LOGE(TAG, "Claim init response has no node_id.");
        return ESP_RMAKER_FAIL;
    }

    /* Persist the cloud-assigned node ID before generating the CSR: the CSR's Common Name
     * must be the new node ID, and it is read back through the credentials layer.
     *
     * Unlike esp-rainmaker there is no live node-ID swap to do here, because in this SDK
     * claiming completes before esp_rmaker_node_init() reads any credential. */
    esp_rmaker_error_t err = esp_rmaker_credentials_set_client_id(node_id);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to store the node ID: %d", err);
        return err;
    }
    OSAL_LOGI(TAG, "Assigned node ID: %s", node_id);

    err = __run_in_worker_task(__generate_csr, claim_data, CONFIG_RMAKER_WORK_QUEUE_TASK_PRIORITY + 1);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to generate the CSR.");
        return err;
    }
    err = __escape_new_line(claim_data);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    json_gen_str_t jstr;
    json_gen_str_start(&jstr, claim_data->payload, sizeof(claim_data->payload), NULL, NULL);
    json_gen_start_object(&jstr);
    json_gen_obj_set_string(&jstr, "csr", (char *)claim_data->csr);
    json_gen_obj_set_bool(&jstr, "send_mqtt_host", true);
    json_gen_end_object(&jstr);
    json_gen_str_end(&jstr);

    claim_data->payload_len = strlen(claim_data->payload);
    claim_data->payload_offset = 0;
    return ESP_RMAKER_OK;
}

/**
 * @brief Handle the claim-verify response and persist the credentials.
 *
 * Format: {"certificate":"<escaped PEM>","mqtt_host":"..."}
 * Both fields are required; a response missing either one fails the claim.
 */
static esp_rmaker_error_t __handle_claim_verify_response(esp_rmaker_claim_data_t *claim_data)
{
    OSAL_LOGD(TAG, "Claim verify response: %s", claim_data->payload);

    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, claim_data->payload, (int)strlen(claim_data->payload)) != 0) {
        OSAL_LOGE(TAG, "Failed to parse the claim verify response.");
        return ESP_RMAKER_FAIL;
    }

    /* Both values are sized from the response rather than one being assumed to fit the other's
     * buffer: a host longer than the certificate would otherwise be reported as a missing host
     * and send whoever debugs it looking in the wrong place. */
    int cert_len = 0;
    if (json_obj_get_strlen(&jctx, "certificate", &cert_len) != 0) {
        json_parse_end(&jctx);
        OSAL_LOGE(TAG, "Claim verify response has no certificate.");
        return ESP_RMAKER_FAIL;
    }
    /* The MQTT host is requested via `send_mqtt_host` and is mandatory: without it a claimed
     * node has no broker to connect to, and there is no sane value to invent on its behalf. */
    int host_len = 0;
    if (json_obj_get_strlen(&jctx, "mqtt_host", &host_len) != 0) {
        json_parse_end(&jctx);
        OSAL_LOGE(TAG, "Claim verify response has no MQTT host.");
        return ESP_RMAKER_FAIL;
    }
    cert_len++; /* NUL terminator */
    host_len++;
    /* The response may also carry `mqtt_cred_host`, the AWS IoT credentials-provider
     * endpoint. It is deliberately ignored: nothing in this SDK fetches temporary AWS
     * credentials (that serves camera/videostream, which is not supported here), so storing
     * it would just consume factory-partition space. Add it back together with its consumer. */

    char *cert_buf = (char *)OSAL_CALLOC_EXTRAM(1, cert_len);
    char *host_buf = (char *)OSAL_CALLOC_EXTRAM(1, host_len);
    esp_rmaker_error_t err = ESP_RMAKER_FAIL;
    if (!cert_buf || !host_buf) {
        OSAL_LOGE(TAG, "Failed to allocate %d + %d bytes for the claimed credentials.",
                  cert_len, host_len);
        err = ESP_RMAKER_NO_MEM;
        goto cleanup;
    }

    /* Read both values before persisting either, so a response that parses only halfway leaves
     * the factory partition untouched. */
    if (json_obj_get_string(&jctx, "certificate", cert_buf, cert_len) != 0) {
        OSAL_LOGE(TAG, "Failed to read the certificate from the claim verify response.");
        goto cleanup;
    }
    if (json_obj_get_string(&jctx, "mqtt_host", host_buf, host_len) != 0) {
        OSAL_LOGE(TAG, "Failed to read the MQTT host from the claim verify response.");
        goto cleanup;
    }

    claim_rules_unescape_new_line(cert_buf);
    err = esp_rmaker_credentials_set_client_cert((const uint8_t *)cert_buf, strlen(cert_buf));
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to store the certificate: %d", err);
        goto cleanup;
    }
    OSAL_LOGI(TAG, "Storing the received MQTT host: %s", host_buf);
    err = esp_rmaker_credentials_set_mqtt_host(host_buf);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to store the MQTT host: %d", err);
    }

cleanup:
    json_parse_end(&jctx);
    if (cert_buf) {
        memset(cert_buf, 0, cert_len);
        free(cert_buf);
    }
    free(host_buf);
    return err;
}

/**
 * @brief Validate an inbound fragment against the reassembly buffer.
 *
 * @return The fragment payload, or NULL with the response status already set.
 */
static ProtobufCBinaryData *__validate_data(RmakerClaim__PayloadBuf *recv_payload,
        RmakerClaim__RMakerClaimPayload *response,
        esp_rmaker_claim_data_t *claim_data)
{
    if (recv_payload == NULL) {
        OSAL_LOGE(TAG, "Empty payload received. Cannot proceed.");
        response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidParam;
        return NULL;
    }
    ProtobufCBinaryData *recv_payload_buf = &recv_payload->payload;

    RmakerClaim__RMakerClaimStatus status = claim_rules_validate_fragment(sizeof(claim_data->payload),
                                            recv_payload->offset, recv_payload_buf->len, recv_payload->totallen);
    if (status != RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success) {
        OSAL_LOGE(TAG, "Rejected fragment (offset %" PRIu32 ", len %" PRIu32 ", total %" PRIu32
                  ", capacity %" PRIu32 "): status %d",
                  (uint32_t)recv_payload->offset, (uint32_t)recv_payload_buf->len,
                  (uint32_t)recv_payload->totallen, (uint32_t)sizeof(claim_data->payload), (int)status);
        response->resppayload->status = status;
        return NULL;
    }
    return recv_payload_buf;
}

/**
 * @brief Handle TypeCmdClaimStart: emit the claim-init request.
 */
static void __handle_start(RmakerClaim__RMakerClaimPayload *response, esp_rmaker_claim_data_t *claim_data)
{
    RmakerClaim__RMakerClaimStatus allowed = claim_rules_check_command(claim_data->state,
            RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimStart);
    if (allowed != RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success) {
        OSAL_LOGE(TAG, "Cannot start claiming in state %d.", (int)claim_data->state);
        response->resppayload->status = allowed;
        return;
    }
    if (__generate_claim_init_request(claim_data) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to generate the claim init request.");
        response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Fail;
        return;
    }

    RmakerClaim__PayloadBuf *payload_buf = response->resppayload->buf;
    payload_buf->offset = 0;
    payload_buf->totallen = claim_data->payload_len;
    payload_buf->payload.data = (uint8_t *)claim_data->payload;
    payload_buf->payload.len = claim_data->payload_len;
    response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success;

    claim_data->state = RMAKER_CLAIM_STATE_INIT;
    /* A fresh attempt: whatever the previous one reported is no longer the verdict. */
    claim_failed_posted = false;
    OSAL_LOGI(TAG, "Assisted claiming started.");
    osal_event_post(RMAKER_EVENT, RMAKER_EVENT_CLAIM_STARTED, NULL, 0, OSAL_MAX_DELAY);
}

/**
 * @brief Handle TypeCmdClaimInit: consume the init response, then pump verify-request fragments.
 */
static void __handle_init(RmakerClaim__RMakerClaimPayload *command,
                          RmakerClaim__RMakerClaimPayload *response,
                          esp_rmaker_claim_data_t *claim_data)
{
    RmakerClaim__RMakerClaimStatus allowed = claim_rules_check_command(claim_data->state,
            RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimInit);
    if (allowed != RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success) {
        OSAL_LOGE(TAG, "Cannot handle claim init in state %d.", (int)claim_data->state);
        response->resppayload->status = allowed;
        return;
    }

    /* Only the very first init command carries data; later ones just pump fragments.
     *
     * Tested for equality with INIT, not inequality with INIT_DONE:
     * every state past INIT_DONE also satisfies "!= INIT_DONE", so a late TypeCmdClaimInit
     * would re-enter the data branch and re-run __handle_claim_init_response() after the flow
     * had moved on -- storing a second node ID and regenerating the CSR. Worst case is after
     * VERIFY_DONE, where the node ID would no longer match the CN of the certificate it has
     * already been issued, leaving the node needing a fresh claim. */
    if (claim_data->state == RMAKER_CLAIM_STATE_INIT) {
        if (command->payload_case != RMAKER_CLAIM__RMAKER_CLAIM_PAYLOAD__PAYLOAD_CMD_PAYLOAD) {
            OSAL_LOGE(TAG, "Invalid payload received for claim init.");
            response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidParam;
            return;
        }
        RmakerClaim__PayloadBuf *recv_payload = command->cmdpayload;
        ProtobufCBinaryData *recv_payload_buf = __validate_data(recv_payload, response, claim_data);
        if (!recv_payload_buf) {
            return;
        }
        if (recv_payload_buf->len != recv_payload->totallen) {
            OSAL_LOGE(TAG, "Claim init data must arrive in a single fragment.");
            response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidParam;
            return;
        }

        memset(claim_data->payload, 0, sizeof(claim_data->payload));
        memcpy(claim_data->payload, recv_payload_buf->data, recv_payload_buf->len);
        if (__handle_claim_init_response(claim_data) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Error handling the claim init response.");
            response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidParam;
            return;
        }
        claim_data->state = RMAKER_CLAIM_STATE_INIT_DONE;
    }

    /* Send the next fragment of the verify request. */
    RmakerClaim__PayloadBuf *payload_buf = response->resppayload->buf;
    payload_buf->totallen = claim_data->payload_len;
    payload_buf->offset = claim_data->payload_offset;
    payload_buf->payload.data = (uint8_t *)claim_data->payload + claim_data->payload_offset;
    payload_buf->payload.len = (claim_data->payload_len - claim_data->payload_offset) > CLAIM_FRAGMENT_SIZE
                               ? CLAIM_FRAGMENT_SIZE
                               : (claim_data->payload_len - claim_data->payload_offset);
    claim_data->payload_offset += payload_buf->payload.len;
    response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success;

    if (claim_data->payload_offset == claim_data->payload_len) {
        OSAL_LOGD(TAG, "Finished sending the claim verify payload.");
        /* Clear the cursor before handing over to the verify step, so its contiguity check
         * starts from a known zero rather than from wherever this send finished. `payload`
         * itself stays put: the response fragment just written still points into it. */
        claim_data->payload_offset = 0;
        claim_data->payload_len = 0;
        claim_data->payload_total = 0;
        claim_data->state = RMAKER_CLAIM_STATE_VERIFY_PENDING;
    }
}

/**
 * @brief Handle TypeCmdClaimVerify: reassemble the certificate response and persist it.
 */
static void __handle_verify(RmakerClaim__RMakerClaimPayload *command,
                            RmakerClaim__RMakerClaimPayload *response,
                            esp_rmaker_claim_data_t *claim_data)
{
    RmakerClaim__RMakerClaimStatus allowed = claim_rules_check_command(claim_data->state,
            RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimVerify);
    if (allowed != RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success) {
        OSAL_LOGE(TAG, "Not awaiting a certificate in state %d. Cannot handle verify.", (int)claim_data->state);
        response->resppayload->status = allowed;
        return;
    }
    if (command->payload_case != RMAKER_CLAIM__RMAKER_CLAIM_PAYLOAD__PAYLOAD_CMD_PAYLOAD) {
        OSAL_LOGE(TAG, "Invalid payload received for claim verify.");
        response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidParam;
        return;
    }

    RmakerClaim__PayloadBuf *recv_payload = command->cmdpayload;
    ProtobufCBinaryData *recv_payload_buf = __validate_data(recv_payload, response, claim_data);
    if (!recv_payload_buf) {
        return;
    }

    /* offset == 0 marks the start of a fresh fragmented response. Anything else must
     * continue exactly where the previous fragment ended: without this, a peer can open a
     * response at a nonzero offset and reassemble on top of the previous step's payload, or
     * leave gaps, overlaps and retransmits that all pass silently. */
    if (recv_payload->offset == 0) {
        memset(claim_data->payload, 0, sizeof(claim_data->payload));
        claim_data->payload_offset = 0;
        claim_data->payload_len = 0;
        /* The opening fragment pins the total the rest of this response must declare. */
        claim_data->payload_total = recv_payload->totallen;
    } else if (!claim_rules_fragment_in_sequence(recv_payload->offset, claim_data->payload_offset)) {
        OSAL_LOGE(TAG, "Out-of-order claim verify fragment (offset %" PRIu32 ", expected %" PRIu32 ").",
                  (uint32_t)recv_payload->offset, (uint32_t)claim_data->payload_offset);
        response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidParam;
        return;
    } else if (!claim_rules_fragment_total_pinned(recv_payload->offset, recv_payload->totallen,
               claim_data->payload_total)) {
        OSAL_LOGE(TAG, "Claim verify fragment changed the total mid-response (%" PRIu32 ", pinned %"
                  PRIu32 ").", (uint32_t)recv_payload->totallen, (uint32_t)claim_data->payload_total);
        response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidParam;
        return;
    }
    memcpy(claim_data->payload + recv_payload->offset, recv_payload_buf->data, recv_payload_buf->len);
    claim_data->payload_offset = recv_payload->offset + recv_payload_buf->len;
    claim_data->payload_len = claim_data->payload_offset;

    if ((recv_payload->offset + recv_payload_buf->len) != recv_payload->totallen) {
        /* More fragments to come. */
        response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success;
        return;
    }

    OSAL_LOGD(TAG, "Received the complete claim verify response (%" PRIu32 " bytes).",
              (uint32_t)recv_payload->totallen);
    if (__handle_claim_verify_response(claim_data) == ESP_RMAKER_OK) {
        OSAL_LOGI(TAG, "Assisted claiming was successful.");
        claim_data->state = RMAKER_CLAIM_STATE_VERIFY_DONE;
        response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success;
        osal_event_post(RMAKER_EVENT, RMAKER_EVENT_CLAIM_SUCCESSFUL, NULL, 0, OSAL_MAX_DELAY);
        /* Only success ends the wait; see ::CLAIM_DONE_BIT. */
        if (claim_event_group) {
            osal_event_group_set_bits(claim_event_group, CLAIM_DONE_BIT);
        }
    } else {
        /* The state stays at VERIFY_PENDING, so the app can send TypeCmdClaimStart and try the
         * whole exchange again while the session lasts. */
        OSAL_LOGE(TAG, "The claim verify response was rejected. A retry is still possible for as "
                  "long as the provisioning session lasts.");
        response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__InvalidParam;
        claim_failed_posted = true;
        osal_event_post(RMAKER_EVENT, RMAKER_EVENT_CLAIM_FAILED, NULL, 0, OSAL_MAX_DELAY);
    }
}

/**
 * @brief Handle TypeCmdClaimAbort: reset so claiming can be retried.
 */
static void __handle_abort(RmakerClaim__RMakerClaimPayload *response, esp_rmaker_claim_data_t *claim_data)
{
    RmakerClaim__RMakerClaimStatus allowed = claim_rules_check_command(claim_data->state,
            RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimAbort);
    if (allowed != RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success) {
        OSAL_LOGE(TAG, "Nothing to abort in state %d.", (int)claim_data->state);
        response->resppayload->status = allowed;
        return;
    }

    memset(claim_data->payload, 0, sizeof(claim_data->payload));
    claim_data->payload_len = 0;
    claim_data->payload_offset = 0;
    claim_data->payload_total = 0;
    /* Back to PK_GENERATED so a fresh claim attempt can start. */
    claim_data->state = RMAKER_CLAIM_STATE_PK_GENERATED;
    response->resppayload->status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Success;
    /* No CLAIM_DONE_BIT: an abort is the app saying "not this attempt", not "not at all", and
     * the state above leaves a fresh TypeCmdClaimStart legal. See ::CLAIM_DONE_BIT. */
    OSAL_LOGW(TAG, "Assisted claiming aborted. A fresh attempt is still possible for as long as "
              "the provisioning session lasts.");
    claim_failed_posted = true;
    osal_event_post(RMAKER_EVENT, RMAKER_EVENT_CLAIM_FAILED, NULL, 0, OSAL_MAX_DELAY);
}

static osal_err_t __claim_endpoint_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
        uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    (void)session_id;
    (void)priv_data; /* The context comes from ::claim_ctx instead; see the endpoint's comment. */

    if (!inbuf || !outbuf || !outlen) {
        OSAL_LOGE(TAG, "Invalid params");
        return OSAL_ERR_INVALID_ARG;
    }
    if (!claim_ctx_lock) {
        OSAL_LOGE(TAG, "Claiming is not initialised. Cannot proceed with assisted claiming.");
        return OSAL_ERR_INVALID_STATE;
    }

    /* Held for the whole command. The endpoint outlives the context, so this is what stops a
     * command that arrives around esp_rmaker_assisted_claim_perform()'s teardown from running
     * against a context that is being, or has been, released. */
    if (osal_semaphore_take(claim_ctx_lock, OSAL_MAX_DELAY) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to acquire the claim context lock.");
        return OSAL_ERR_INVALID_STATE;
    }
    esp_rmaker_claim_data_t *claim_data = claim_ctx;
    if (!claim_data) {
        /* The claim has already produced its verdict and the context is gone. */
        OSAL_LOGE(TAG, "Assisted claiming is no longer in progress. Ignoring the command.");
        osal_semaphore_give(claim_ctx_lock);
        return OSAL_ERR_INVALID_STATE;
    }

    RmakerClaim__RMakerClaimPayload *command = rmaker_claim__rmaker_claim_payload__unpack(NULL, inlen, inbuf);
    if (!command) {
        OSAL_LOGE(TAG, "No claim command received.");
        osal_semaphore_give(claim_ctx_lock);
        return OSAL_ERR_INVALID_ARG;
    }

    /* The response message type is always the command's + 1. */
    RmakerClaim__RMakerClaimPayload response;
    rmaker_claim__rmaker_claim_payload__init(&response);
    response.msg = command->msg + 1;
    response.payload_case = RMAKER_CLAIM__RMAKER_CLAIM_PAYLOAD__PAYLOAD_RESP_PAYLOAD;

    RmakerClaim__RespPayload resppayload;
    rmaker_claim__resp_payload__init(&resppayload);
    resppayload.status = RMAKER_CLAIM__RMAKER_CLAIM_STATUS__Fail;
    response.resppayload = &resppayload;

    RmakerClaim__PayloadBuf payload_buf;
    rmaker_claim__payload_buf__init(&payload_buf);
    resppayload.buf = &payload_buf;

    OSAL_LOGD(TAG, "Received claim command: %d", command->msg);

    switch (command->msg) {
    case RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimStart:
        __handle_start(&response, claim_data);
        break;
    case RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimInit:
        __handle_init(command, &response, claim_data);
        break;
    case RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimVerify:
        __handle_verify(command, &response, claim_data);
        break;
    case RMAKER_CLAIM__RMAKER_CLAIM_MSG_TYPE__TypeCmdClaimAbort:
        __handle_abort(&response, claim_data);
        break;
    default:
        OSAL_LOGE(TAG, "Unhandled claim command: %d", command->msg);
        break;
    }

    size_t resp_len = rmaker_claim__rmaker_claim_payload__get_packed_size(&response);
    uint8_t *resp_buf = (uint8_t *)OSAL_CALLOC_EXTRAM(resp_len, sizeof(uint8_t));
    if (!resp_buf) {
        OSAL_LOGE(TAG, "Failed to allocate %" PRIu32 " bytes for the claim response.", (uint32_t)resp_len);
        rmaker_claim__rmaker_claim_payload__free_unpacked(command, NULL);
        osal_semaphore_give(claim_ctx_lock);
        return OSAL_ERR_NO_MEM;
    }
    rmaker_claim__rmaker_claim_payload__pack(&response, resp_buf);
    rmaker_claim__rmaker_claim_payload__free_unpacked(command, NULL);
    osal_semaphore_give(claim_ctx_lock);

    /* Ownership of resp_buf transfers to the caller. */
    *outbuf = resp_buf;
    *outlen = resp_len;
    return OSAL_ERR_OK;
}

static void __claim_event_handler(void *arg, osal_event_base_t event_base,
                                  int32_t event_id, void *event_data)
{
    (void)event_data;
    (void)arg; /* The context reaches the handler through ::claim_ctx. */

    if (event_base != prov_info.event_base) {
        return;
    }
    if (event_id == prov_info.event_ids.prov_init) {
        if (prov_info.actions.endpoint_create(&claim_endpoint) != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to create the claim endpoint.");
        }
    } else if (event_id == prov_info.event_ids.prov_start) {
        if (prov_info.actions.endpoint_register(&claim_endpoint) != OSAL_ERR_OK) {
            OSAL_LOGE(TAG, "Failed to register the claim endpoint.");
        }
    } else if (event_id == prov_info.event_ids.prov_end) {
        /* The provisioning session has stopped, taking the rmaker_claim endpoint with it, so
         * no certificate and no abort can arrive from here on. Releasing the wait in
         * esp_rmaker_assisted_claim_perform() here is what keeps it bounded without inventing
         * a timeout: this is the exact moment claiming stops being possible.
         *
         * esp-rainmaker can wait unbounded because it waits earlier, on a background task
         * while the session is still live. This SDK waits from esp_rmaker_pre_prov_deinit(),
         * i.e. after the session, so it needs this signal instead. */
        if (claim_event_group) {
            osal_event_group_set_bits(claim_event_group, CLAIM_ABANDONED_BIT);
        }
    }
}

/**
 * @brief Load or generate the private key, and ensure the random blob exists.
 *
 * Runs on the worker task; see __run_in_worker_task().
 */
static esp_rmaker_error_t __load_or_generate_key(esp_rmaker_claim_data_t *claim_data)
{
    /* A certificate means the node is already claimed; reuse whatever key it was issued
     * against, even if the configured key type has since changed. */
    bool node_already_claimed = false;
    esp_rmaker_credential_t cert = { 0 };
    if (esp_rmaker_credentials_get_client_cert(&cert) == ESP_RMAKER_OK) {
        node_already_claimed = true;
        esp_rmaker_credentials_free_credential(&cert);
        OSAL_LOGI(TAG, "Node already has a certificate. Will reuse the existing key if available.");
    }

    /* Reuse an existing key when it is usable, so a restarted claim does not pay for
     * another key generation. */
    esp_rmaker_credential_t key = { 0 };
    if (esp_rmaker_credentials_get_private_key(&key) == ESP_RMAKER_OK) {
        if (!key.credential || !key.len) {
            /* Present but unusable; free whatever was handed back and regenerate. */
            esp_rmaker_credentials_free_credential(&key);
            goto generate_key;
        }
        esp_rmaker_crypto_key_type_t key_type = RMAKER_CRYPTO_KEY_TYPE_UNKNOWN;
        esp_rmaker_error_t type_err = esp_rmaker_crypto_get_key_type(key.credential, key.len, &key_type);
        bool reuse = false;
        if (type_err != ESP_RMAKER_OK) {
            OSAL_LOGW(TAG, "Failed to parse the existing key. Generating a new one.");
        } else if (node_already_claimed) {
            OSAL_LOGI(TAG, "Reusing the existing key, since the node is already claimed.");
            reuse = true;
        } else if (key_type == CLAIM_KEY_TYPE) {
            OSAL_LOGI(TAG, "Reusing the existing key: its type matches the configuration.");
            reuse = true;
        } else {
            OSAL_LOGW(TAG, "The existing key's type does not match the configuration. Generating a new one.");
        }

        if (reuse) {
            claim_data->key_pem = key.credential;
            claim_data->key_pem_len = key.len;
            claim_data->state = RMAKER_CLAIM_STATE_PK_GENERATED;
        } else {
            esp_rmaker_credentials_free_credential(&key);
        }
    }

generate_key:
    if (claim_data->state != RMAKER_CLAIM_STATE_PK_GENERATED) {
        esp_rmaker_error_t err = esp_rmaker_crypto_gen_key_pem(CLAIM_KEY_TYPE,
                                 &claim_data->key_pem, &claim_data->key_pem_len);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to generate the private key: %d", err);
            return err;
        }
        /* Store strlen() bytes, i.e. without the NUL, matching the factory tooling. */
        err = esp_rmaker_credentials_set_client_key(claim_data->key_pem, claim_data->key_pem_len - 1);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to save the private key: %d", err);
            return err;
        }
        claim_data->state = RMAKER_CLAIM_STATE_PK_GENERATED;
    }

    /* General-purpose random bytes, generated once and then kept.
     *
     * Load-bearing position: this runs from esp_rmaker_pre_prov_init(), i.e. before
     * app_network_start_neo() derives the Proof of Possession and the advertised BLE device
     * name from the same blob. Deferring key generation to the first ClaimStart -- a plausible
     * optimisation, given keygen blocks boot -- would take this with it, and both would
     * silently fall back to MAC-derived values. */
    esp_rmaker_credential_t random_bytes = { 0 };
    if (esp_rmaker_credentials_get_random(&random_bytes) == ESP_RMAKER_OK) {
        esp_rmaker_credentials_free_credential(&random_bytes);
    } else {
        uint8_t buf[CLAIM_RANDOM_NUMBER_LEN];
        osal_random_fill(buf, sizeof(buf));
        esp_rmaker_error_t err = esp_rmaker_credentials_set_random(buf, sizeof(buf));
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to store the random bytes: %d", err);
            return err;
        }
    }

    return ESP_RMAKER_OK;
}

/* Public function definitions *******************************************************/

void esp_rmaker_claim_data_free(esp_rmaker_claim_data_t *claim_data)
{
    if (!claim_data) {
        return;
    }
    /* Wipe before releasing: the PEM is the node's private key, and the context still holds
     * the CSR and whatever the payload buffer was last used for, including the certificate. */
    if (claim_data->key_pem) {
        memset(claim_data->key_pem, 0, claim_data->key_pem_len);
        free(claim_data->key_pem);
    }
    memset(claim_data, 0, sizeof(*claim_data));
    free(claim_data);
}

esp_rmaker_claim_data_t *esp_rmaker_assisted_claim_init(void)
{
    if (claim_init_done) {
        OSAL_LOGE(TAG, "Claiming already initialised.");
        return NULL;
    }

    OSAL_LOGI(TAG, "Initialising assisted claiming. This may take time.");

    esp_rmaker_claim_data_t *claim_data = NULL;
    claim_event_group = osal_event_group_create();
    if (!claim_event_group) {
        OSAL_LOGE(TAG, "Couldn't create the claim event group.");
        return NULL;
    }
    /* Created once and then kept for the lifetime of the process, deliberately: the endpoint
     * handler can be blocked on this lock at the moment claiming is torn down, and deleting a
     * mutex somebody is waiting on cannot be made safe. One mutex is a cheap price. */
    if (!claim_ctx_lock) {
        claim_ctx_lock = osal_semaphore_create_mutex();
        if (!claim_ctx_lock) {
            OSAL_LOGE(TAG, "Couldn't create the claim context lock.");
            osal_event_group_delete(claim_event_group);
            claim_event_group = NULL;
            return NULL;
        }
    }
    claim_failed_posted = false;

    claim_data = (esp_rmaker_claim_data_t *)OSAL_CALLOC_EXTRAM(1, sizeof(esp_rmaker_claim_data_t));
    if (!claim_data) {
        OSAL_LOGE(TAG, "Failed to allocate memory for the claim data.");
        goto fail;
    }

    /* Blocks until the key is ready. Idle priority so a slow keygen cannot trip the
     * task watchdog. */
    if (__run_in_worker_task(__load_or_generate_key, claim_data, 0) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialise the claiming private key.");
        goto fail;
    }

    if (prov_get_registration_info(&prov_info) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to get the provisioning registration information.");
        goto fail;
    }
    if (osal_event_handler_register(prov_info.event_base, prov_info.event_ids.prov_init,
                                    __claim_event_handler, claim_data) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register for the provisioning init event.");
        goto fail;
    }
    if (osal_event_handler_register(prov_info.event_base, prov_info.event_ids.prov_start,
                                    __claim_event_handler, claim_data) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register for the provisioning start event.");
        osal_event_handler_unregister(prov_info.event_base, prov_info.event_ids.prov_init, __claim_event_handler);
        goto fail;
    }
    /* Not optional: without the end event, a claim the app starts and never finishes leaves
     * esp_rmaker_assisted_claim_perform() with nothing that can ever wake it. */
    if (osal_event_handler_register(prov_info.event_base, prov_info.event_ids.prov_end,
                                    __claim_event_handler, claim_data) != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register for the provisioning end event.");
        osal_event_handler_unregister(prov_info.event_base, prov_info.event_ids.prov_init, __claim_event_handler);
        osal_event_handler_unregister(prov_info.event_base, prov_info.event_ids.prov_start, __claim_event_handler);
        goto fail;
    }

    /* Published only once every registration is in place, so the endpoint handler cannot pick
     * up a context that is still being built. */
    claim_ctx = claim_data;
    claim_init_done = true;
    return claim_data;

fail:
    esp_rmaker_claim_data_free(claim_data);
    osal_event_group_delete(claim_event_group);
    claim_event_group = NULL;
    /* claim_ctx_lock is intentionally kept; see where it is created. */
    return NULL;
}

esp_rmaker_error_t esp_rmaker_assisted_claim_perform(esp_rmaker_claim_data_t *claim_data)
{
    if (claim_data == NULL) {
        OSAL_LOGE(TAG, "Assisted claiming not initialised.");
        return ESP_RMAKER_INVALID_STATE;
    }
    if (!claim_event_group) {
        OSAL_LOGE(TAG, "Claim event group not created.");
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Read under the lock: the protocomm task writes this field, and under
     * CONFIG_APP_NETWORK_ASYNCHRONOUS_CONNECTION=y it can be doing so right now. */
    esp_rmaker_claim_state_t state = claim_data->state;
    if (claim_ctx_lock && osal_semaphore_take(claim_ctx_lock, OSAL_MAX_DELAY) == OSAL_ERR_OK) {
        state = claim_data->state;
        osal_semaphore_give(claim_ctx_lock);
    }

    esp_rmaker_error_t err;
    if (state <= RMAKER_CLAIM_STATE_PK_GENERATED) {
        /* The phone app never sent TypeCmdClaimStart, so there is nothing to wait for and
         * waiting would block forever.
         *
         * This check replaces esp-rainmaker's "already connected to Wi-Fi" guard. There,
         * claiming runs before the network comes up, so being connected means claiming was
         * skipped. Here claiming is awaited after provisioning has finished, so the state
         * is what tells us whether the app ever tried. */
        OSAL_LOGE(TAG, "Node was provisioned without assisted claiming. Assisted claiming needs a "
                  "phone app that supports it, over the BLE provisioning transport. The node now "
                  "has network credentials, so it will not re-enter provisioning on its own: clear "
                  "them with `reset-to-factory` and provision again with an app that claims, or "
                  "pre-flash credentials into the factory partition.");
        err = ESP_RMAKER_INVALID_STATE;
    } else {
        OSAL_LOGI(TAG, "Waiting for assisted claiming to finish.");
        /* No timeout, and none needed. Either the claim succeeds, or the provisioning session
         * ends and claiming stops being possible; the latter is guaranteed to arrive, since
         * provisioning has to stop for this function to be reached at all. Wait for whichever
         * comes first.
         *
         * A rejected certificate or an abort deliberately does not end the wait: both are
         * retryable over the same session, so the session's end is the honest bound. See
         * ::CLAIM_DONE_BIT. */
        osal_event_group_bits_t bits = osal_event_group_wait_bits(claim_event_group,
                                       CLAIM_DONE_BIT | CLAIM_ABANDONED_BIT,
                                       false, false, OSAL_MAX_DELAY);
        if (bits & CLAIM_DONE_BIT) {
            /* The exchange already posted RMAKER_EVENT_CLAIM_SUCCESSFUL. */
            err = ESP_RMAKER_OK;
        } else {
            OSAL_LOGE(TAG, "The phone app started assisted claiming but never delivered a "
                      "certificate, and the provisioning session it would have arrived over has "
                      "ended. Clear the network credentials with `reset-to-factory` and provision "
                      "again.");
            /* Posted from here rather than from the prov_end handler: RMAKER_EVENT shares the
             * default event loop with the provisioning events, so posting from a handler on
             * that loop could block it on its own queue. Skipped when a failed attempt has
             * already reported itself, so one failure is not announced twice. */
            if (!claim_failed_posted) {
                osal_event_post(RMAKER_EVENT, RMAKER_EVENT_CLAIM_FAILED, NULL, 0, OSAL_MAX_DELAY);
            }
            err = ESP_RMAKER_FAIL;
        }
    }

    /* Unregister before releasing the event group, so the prov_end handler cannot run against
     * a freed handle. */
    osal_event_handler_unregister(prov_info.event_base, prov_info.event_ids.prov_init, __claim_event_handler);
    osal_event_handler_unregister(prov_info.event_base, prov_info.event_ids.prov_start, __claim_event_handler);
    osal_event_handler_unregister(prov_info.event_base, prov_info.event_ids.prov_end, __claim_event_handler);

    /* Retire the context before releasing it. The endpoint stays registered until the
     * provisioning session dies, which under CONFIG_APP_NETWORK_ASYNCHRONOUS_CONNECTION=y can
     * be well after this point, so a command may still arrive: taking the lock waits for any
     * command already running, and clearing the pointer under it makes every later command
     * report that claiming is no longer in progress instead of reaching freed memory. */
    if (claim_ctx_lock && osal_semaphore_take(claim_ctx_lock, OSAL_MAX_DELAY) == OSAL_ERR_OK) {
        claim_ctx = NULL;
        osal_semaphore_give(claim_ctx_lock);
    } else {
        /* Nothing else can be done: leaving the context published would be worse. */
        claim_ctx = NULL;
    }

    esp_rmaker_claim_data_free(claim_data);
    osal_event_group_delete(claim_event_group);
    claim_event_group = NULL;
    /* Everything esp_rmaker_assisted_claim_init() set up is gone, so let it run again: a failed
     * claim must not block claiming on a second provisioning attempt in the same boot. */
    claim_init_done = false;
    return err;
}
