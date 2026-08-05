/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file chal_resp.c
 * @brief Implementation of the challenge response functionality.
 */

/* Includes *******************************************************/

/* Declaraction includes */
#include "chal_resp/impl.h"

/* Standard includes */
#include <string.h>

/* Platform common includes */
#include "osal_log.h"
#include "osal_event_loop.h"
#include "osal_mem_alloc.h"

/* Protobuf includes */
#include "chal_resp/pb-c.h"

/* ESP RainMaker includes */
#include "core_internal.h"
#include "local_config.h"

/* NVS includes */
#include "constants/nvs.h"
#include "util/esp_rmaker_nvs.h"

/* Forward declaration: persist the client-issued disable (defined with the
 * enable/disable state handling below). */
static void __chal_resp_persist_disabled(void);

/* Prov registry includes */
#include "prov_helpers.h"

/* Preprocessor definitions *******************************************************/

#define RMAKER_EXTRA_APP_NAME    "rmaker_extra"
#define RMAKER_EXTRA_APP_VERSION "1.0"

/* Variables *******************************************************/

static const char *TAG = "rmng_chal_resp";

static prov_registration_info_t prov_info;

static const char *__app_info_capabilities[] = { RMAKER_CHAL_RESP_ENDPOINT_NAME };

/**
 * @brief Flag to indicate if the challenge response is disabled.
 */
static bool g_chal_resp_disabled = false;

/* Private function declarations *******************************************************/

/**
 * @brief Handle the challenge.
 *
 * @param[in] challenge The challenge.
 * @param[in] challenge_len The length of the challenge.
 * @param[out] response The response.
 * @param[out] response_len The length of the response.
 *
 * @return ESP_RMAKER_OK on success, otherwise an error code.
 */
static esp_rmaker_error_t __handle_challenge(const uint8_t *challenge, size_t challenge_len, uint8_t **response, size_t *response_len);

/**
 * @brief Challenge response endpoint handler.
 *
 * @param[in] session_id The session ID.
 * @param[in] inbuf The input buffer.
 * @param[in] inlen The length of the input buffer.
 * @param[out] outbuf The output buffer.
 * @param[out] outlen The length of the output buffer.
 * @param[in] priv_data The private data.
 *
 * @return OSAL_ERR_OK on success, otherwise an error code.
 */
static osal_err_t __chal_resp_endpoint_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
        uint8_t **outbuf, ssize_t *outlen, void *priv_data);

/**
 * @brief Challenge response event handler.
 *
 * @param[in] arg The argument.
 * @param[in] event_base The event base.
 * @param[in] event_id The event ID.
 * @param[in] event_data The event data.
 */
static void __chal_resp_event_handler(void *arg, osal_event_base_t event_base,
                                      int32_t event_id, void *event_data);

/* Private function definitions *******************************************************/

static esp_rmaker_error_t __handle_challenge(const uint8_t *challenge, size_t challenge_len, uint8_t **response, size_t *response_len)
{
    if (!challenge || !response || !response_len) {
        OSAL_LOGE(TAG, "Invalid params");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Sign the challenge */
    esp_rmaker_error_t err = esp_rmaker_core_sign_challenge(challenge, challenge_len, response, response_len);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to sign challenge");
        return err;
    }
    return ESP_RMAKER_OK;
}

static osal_err_t __chal_resp_endpoint_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
        uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    OSAL_LOGD(TAG, "Challenge-Response handler invoked");
    if (!inbuf || !inlen || !outbuf || !outlen) {
        OSAL_LOGE(TAG, "Invalid params");
        return OSAL_ERR_INVALID_ARG;
    }
    OSAL_LOGD(TAG, "Received challenge data of length: %d", (int) inlen);

    /* Initialize all resources to NULL for safe cleanup */
    RmakerChResp__RMakerChRespPayload *msg = NULL;
    uint8_t *signed_data = NULL;
    RmakerChResp__RespCRPayload *resp_payload = NULL;
    uint8_t *resp_buf = NULL;
    osal_err_t ret = OSAL_ERR_FAIL;

    /* Check if challenge-response is disabled.
     *
     * Must consult the persisted state, not just the in-RAM flag: a client-issued
     * disable survives a reboot, and the provisioning-instance endpoint is registered
     * straight from the provisioning events without going through
     * esp_rmaker_chal_resp_enable() (which is what would otherwise set the flag). */
    if (esp_rmaker_chal_resp_is_disabled()) {
        OSAL_LOGW(TAG, "Challenge-response is disabled");
        /* Return a "Disabled" response */
        RmakerChResp__RMakerChRespPayload resp_msg = RMAKER_CH_RESP__RMAKER_CH_RESP_PAYLOAD__INIT;
        resp_msg.msg = RMAKER_CH_RESP__RMAKER_CH_RESP_MSG_TYPE__TypeRespChallengeResponse;
        resp_msg.status = RMAKER_CH_RESP__RMAKER_CH_RESP_STATUS__Disabled;

        size_t resp_len = rmaker_ch_resp__rmaker_ch_resp_payload__get_packed_size(&resp_msg);
        resp_buf = OSAL_CALLOC_EXTRAM(resp_len, sizeof(uint8_t));
        if (!resp_buf) {
            return OSAL_ERR_NO_MEM;
        }
        rmaker_ch_resp__rmaker_ch_resp_payload__pack(&resp_msg, resp_buf);
        *outbuf = resp_buf;
        *outlen = resp_len;
        return OSAL_ERR_OK;
    }

    /* Parse the received protobuf message */
    msg = rmaker_ch_resp__rmaker_ch_resp_payload__unpack(NULL, inlen, inbuf);
    if (!msg) {
        OSAL_LOGE(TAG, "Failed to unpack message");
        goto cleanup;
    }
    OSAL_LOGD(TAG, "Successfully unpacked protobuf message");

    /* Handle disable challenge-response command */
    if (msg->msg == RMAKER_CH_RESP__RMAKER_CH_RESP_MSG_TYPE__TypeCmdDisableChalResp) {
        OSAL_LOGD(TAG, "Received Disable Challenge-Response command");

        /* Disable challenge-response using generic API and persist the
         * client's choice so it survives reboots. */
        esp_rmaker_error_t disable_err = esp_rmaker_chal_resp_disable();
        if (disable_err == ESP_RMAKER_OK) {
            __chal_resp_persist_disabled();
        }

        /* Create response */
        RmakerChResp__RMakerChRespPayload resp_msg = RMAKER_CH_RESP__RMAKER_CH_RESP_PAYLOAD__INIT;
        resp_msg.msg = RMAKER_CH_RESP__RMAKER_CH_RESP_MSG_TYPE__TypeRespDisableChalResp;
        resp_msg.status = (disable_err == ESP_RMAKER_OK) ?
                          RMAKER_CH_RESP__RMAKER_CH_RESP_STATUS__Success :
                          RMAKER_CH_RESP__RMAKER_CH_RESP_STATUS__Fail;

        size_t resp_len = rmaker_ch_resp__rmaker_ch_resp_payload__get_packed_size(&resp_msg);
        resp_buf = OSAL_CALLOC_EXTRAM(resp_len, sizeof(uint8_t));
        if (!resp_buf) {
            ret = OSAL_ERR_NO_MEM;
            goto cleanup;
        }
        rmaker_ch_resp__rmaker_ch_resp_payload__pack(&resp_msg, resp_buf);
        *outbuf = resp_buf;
        *outlen = resp_len;
        resp_buf = NULL; /* Don't free in cleanup */

        rmaker_ch_resp__rmaker_ch_resp_payload__free_unpacked(msg, NULL);
        return OSAL_ERR_OK;
    }

    if (msg->msg != RMAKER_CH_RESP__RMAKER_CH_RESP_MSG_TYPE__TypeCmdChallengeResponse) {
        OSAL_LOGE(TAG, "Invalid message type received: %d", msg->msg);
        goto cleanup;
    }
    OSAL_LOGD(TAG, "Received valid challenge response command");

    /* Get the challenge from the message */
    RmakerChResp__CmdCRPayload *cmd_payload = msg->cmdchallengeresponsepayload;
    if (!cmd_payload || !cmd_payload->payload.data || !cmd_payload->payload.len) {
        OSAL_LOGE(TAG, "Invalid challenge received");
        goto cleanup;
    }
    OSAL_LOGD(TAG, "Challenge payload length: %d", (int)cmd_payload->payload.len);
    OSAL_LOGD(TAG, "Challenge string (len %d): %.*s", (int)cmd_payload->payload.len, (int)cmd_payload->payload.len, (char *)cmd_payload->payload.data);

    /* Sign the challenge */
    size_t signed_len = 0;
    esp_rmaker_error_t err = __handle_challenge(cmd_payload->payload.data, cmd_payload->payload.len,
                             &signed_data, &signed_len);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to handle challenge");
        goto cleanup;
    }
    OSAL_LOGD(TAG, "Successfully signed challenge. Signature length: %d", (int)signed_len);

    /* Create response protobuf message */
    RmakerChResp__RMakerChRespPayload resp_msg = RMAKER_CH_RESP__RMAKER_CH_RESP_PAYLOAD__INIT;
    resp_msg.msg = RMAKER_CH_RESP__RMAKER_CH_RESP_MSG_TYPE__TypeRespChallengeResponse;
    resp_msg.status = RMAKER_CH_RESP__RMAKER_CH_RESP_STATUS__Success;

    /* Allocate the response payload structure */
    resp_payload = OSAL_MALLOC_EXTRAM(sizeof(RmakerChResp__RespCRPayload));
    if (!resp_payload) {
        OSAL_LOGE(TAG, "Failed to allocate memory for response payload");
        ret = OSAL_ERR_NO_MEM;
        goto cleanup;
    }
    rmaker_ch_resp__resp_crpayload__init(resp_payload);

    /* Set up the payload fields with binary data */
    resp_payload->payload.data = signed_data;
    resp_payload->payload.len = signed_len;
    err = esp_rmaker_credentials_get_thing_name(&resp_payload->node_id);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get node_id");
        ret = OSAL_ERR_FAIL;
        goto cleanup;
    }
    OSAL_LOGD(TAG, "Setting up response with node_id: %s, binary signature len: %d", resp_payload->node_id, (int)resp_payload->payload.len);

    /* Set the oneof field */
    resp_msg.payload_case = RMAKER_CH_RESP__RMAKER_CH_RESP_PAYLOAD__PAYLOAD_RESP_CHALLENGE_RESPONSE_PAYLOAD;
    resp_msg.respchallengeresponsepayload = resp_payload;

    /* Verify response structure before packing */
    if (!resp_msg.respchallengeresponsepayload ||
            !resp_msg.respchallengeresponsepayload->payload.data ||
            !resp_msg.respchallengeresponsepayload->payload.len) {
        OSAL_LOGE(TAG, "Response signature data not properly set");
        goto cleanup;
    }

    /* Serialize the response */
    size_t resp_len = rmaker_ch_resp__rmaker_ch_resp_payload__get_packed_size(&resp_msg);
    OSAL_LOGD(TAG, "Calculated packed size: %d", (int)resp_len);

    resp_buf = OSAL_CALLOC_EXTRAM(resp_len, sizeof(uint8_t));
    if (!resp_buf) {
        OSAL_LOGE(TAG, "Failed to allocate memory for response");
        ret = OSAL_ERR_NO_MEM;
        goto cleanup;
    }

    size_t packed_size = rmaker_ch_resp__rmaker_ch_resp_payload__pack(&resp_msg, resp_buf);
    OSAL_LOGD(TAG, "Actually packed size: %d", (int)packed_size);
    if (packed_size != resp_len) {
        OSAL_LOGE(TAG, "Packed size mismatch! Expected: %d, Got: %d", (int)resp_len, (int)packed_size);
    }

    /* Success - transfer ownership of resp_buf to caller */
    *outbuf = resp_buf;
    *outlen = resp_len;
    resp_buf = NULL; /* Don't free this in cleanup */
    ret = OSAL_ERR_OK;
    OSAL_LOGI(TAG, "Challenge-Response handler completed successfully");

cleanup:
    /* Common cleanup section - safe to call with NULL pointers */
    if (msg) {
        rmaker_ch_resp__rmaker_ch_resp_payload__free_unpacked(msg, NULL);
    }
    if (resp_payload) {
        if (resp_payload->node_id) {
            free(resp_payload->node_id);
        }
        free(resp_payload);
    }
    if (signed_data) {
        free(signed_data);
    }
    if (resp_buf) {
        free(resp_buf);
    }

    return ret;
}

static void __chal_resp_event_handler(void *arg, osal_event_base_t event_base,
                                      int32_t event_id, void *event_data)
{
    static const prov_endpoint_t endpoint = {
        .name = RMAKER_CHAL_RESP_ENDPOINT_NAME,
        .handler = __chal_resp_endpoint_handler,
        .priv_data = NULL,
        .app_info = {
            .label = RMAKER_EXTRA_APP_NAME,
            .version = RMAKER_EXTRA_APP_VERSION,
            .capabilities = __app_info_capabilities,
            .total_capabilities = sizeof(__app_info_capabilities) / sizeof(__app_info_capabilities[0]),
        },
    };
    if (event_base == prov_info.event_base) {
        if (event_id == prov_info.event_ids.prov_init) {
            if (prov_info.actions.endpoint_create(&endpoint) != OSAL_ERR_OK) {
                OSAL_LOGE(TAG, "Failed to create challenge response endpoint.");
            }
        } else if (event_id == prov_info.event_ids.prov_start) {
            if (prov_info.actions.endpoint_register(&endpoint) != OSAL_ERR_OK) {
                OSAL_LOGE(TAG, "Failed to register challenge response endpoint.");
            }
        }
    }
}

/* Public function definitions *******************************************************/

esp_rmaker_error_t esp_rmaker_chal_resp_init(void)
{
    /* Get the provisioning registration information */
    osal_err_t prov_err = prov_get_registration_info(&prov_info);
    if (prov_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to get provisioning registration information");
        return ESP_RMAKER_FAIL;
    }
    OSAL_LOGD(TAG, "Successfully got provisioning registration information: %s", prov_info.event_base);

    /* Register for Wi-Fi Provisioning events */
    osal_err_t platform_err;
    platform_err = osal_event_handler_register(prov_info.event_base, prov_info.event_ids.prov_init, __chal_resp_event_handler, NULL);
    if (platform_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register for provisioning initialization event");
        return ESP_RMAKER_FAIL;
    }
    platform_err = osal_event_handler_register(prov_info.event_base, prov_info.event_ids.prov_start, __chal_resp_event_handler, NULL);
    if (platform_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register for provisioning start event");
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_chal_resp_deinit(void)
{
    /* Unregister the event handler */
    osal_err_t platform_err;
    platform_err = osal_event_handler_unregister(prov_info.event_base, prov_info.event_ids.prov_init, __chal_resp_event_handler);
    if (platform_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to unregister for provisioning initialization event");
        return ESP_RMAKER_FAIL;
    }
    platform_err = osal_event_handler_unregister(prov_info.event_base, prov_info.event_ids.prov_start, __chal_resp_event_handler);
    if (platform_err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to unregister for provisioning start event");
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

/**
 * @brief NVS namespace/key for the client-issued persistent disable.
 */
#define CHAL_RESP_NVS_NAMESPACE    "chal_resp"
#define CHAL_RESP_NVS_DISABLED_KEY "disabled"

/** Cached persistent-disable state: -1 = not read yet, 0/1 = value. */
static int g_chal_resp_persist_cache = -1;

/* Internal: the client-issued disable persists in NVS; enable() consults it. */
static bool __chal_resp_is_persistently_disabled(void)
{
    if (g_chal_resp_persist_cache < 0) {
        size_t len = 0;
        uint8_t *data = esp_rmaker_nvs_get_binary(RMAKER_NVS_PART_NAME, CHAL_RESP_NVS_NAMESPACE,
                        CHAL_RESP_NVS_DISABLED_KEY, &len);
        g_chal_resp_persist_cache = (data != NULL && len == 1 && data[0] == 1) ? 1 : 0;
        free(data);
    }
    return g_chal_resp_persist_cache == 1;
}

/**
 * @brief Persist the client-issued disable so it survives reboots.
 *        Cleared only by a factory reset (the NVS partition is wiped).
 */
static void __chal_resp_persist_disabled(void)
{
    const uint8_t one = 1;
    esp_rmaker_error_t err = esp_rmaker_nvs_update_binary(RMAKER_NVS_PART_NAME, CHAL_RESP_NVS_NAMESPACE,
                             CHAL_RESP_NVS_DISABLED_KEY, &one, sizeof(one));
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to persist challenge-response disable: %d", (int) err);
        return;
    }
    g_chal_resp_persist_cache = 1;
    OSAL_LOGI(TAG, "Challenge-response disable persisted (cleared by factory reset)");
}

esp_rmaker_error_t esp_rmaker_chal_resp_disable(void)
{
    g_chal_resp_disabled = true;
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_chal_resp_enable(void)
{
    if (__chal_resp_is_persistently_disabled()) {
        OSAL_LOGW(TAG, "Challenge-response was disabled by the user (persisted); not enabling");
        g_chal_resp_disabled = true;
        return ESP_RMAKER_INVALID_STATE;
    }
    g_chal_resp_disabled = false;
    return ESP_RMAKER_OK;
}

bool esp_rmaker_chal_resp_is_disabled(void)
{
    return g_chal_resp_disabled || __chal_resp_is_persistently_disabled();
}

esp_rmaker_error_t esp_rmaker_chal_resp_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    osal_err_t ret = __chal_resp_endpoint_handler(session_id, inbuf, inlen, outbuf, outlen, priv_data);
    switch (ret) {
    case OSAL_ERR_OK:
        return ESP_RMAKER_OK;
    case OSAL_ERR_INVALID_ARG:
        return ESP_RMAKER_INVALID_ARG;
    case OSAL_ERR_NO_MEM:
        return ESP_RMAKER_NO_MEM;
    default:
        return ESP_RMAKER_FAIL;
    }
}
