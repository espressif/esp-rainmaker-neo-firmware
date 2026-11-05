/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/* Credentials-fetch callback for kvs_signaling. The blocking HTTPS exchange
 * runs on a task that this component owns: its stack is in internal RAM, while
 * the KVS task that calls the callback may have a SPIRAM stack that mbedTLS
 * cannot run on. */

#include <inttypes.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"

#include <osal_semaphore.h>
#include <osal_task.h>
#include <osal_ticks.h>

#include "rmaker_aws_credentials.h"
#include "sdkconfig.h"

static const char *TAG = "credentials";

#define CREDS_TASK_STACK    (8 * 1024)
#define CREDS_TASK_PRIORITY 5
/* One fetch makes a TLS connection, sends and reads, each bounded by the HTTP
 * timeout. Wait for all three. */
#define CREDS_WAIT_MS       (3 * CONFIG_ESP_RMAKER_AWS_SIGN_CREDS_HTTP_TIMEOUT_MS)

/* s_lock lets one caller at a time hand a fetch to the task. s_request wakes
 * the task, and s_done wakes the caller. s_err and s_creds hold the result of
 * the last fetch. */
static osal_semaphore_handle_t s_lock;
static osal_semaphore_handle_t s_request;
static osal_semaphore_handle_t s_done;
static esp_err_t s_err;
static const esp_rmaker_aws_sign_creds_t *s_creds;

static void credentials_task(void *arg)
{
    (void)arg;
    while (true) {
        osal_semaphore_take(s_request, OSAL_MAX_DELAY);
        s_err = rmaker_camera_refresh_credentials(&s_creds);
        osal_semaphore_give(s_done);
    }
}

esp_err_t rmaker_camera_credentials_init(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    s_lock = osal_semaphore_create_mutex();
    s_request = osal_semaphore_create_binary();
    s_done = osal_semaphore_create_binary();
    if (!s_lock || !s_request || !s_done ||
            osal_task_create(credentials_task, "rmaker_creds", CREDS_TASK_STACK, NULL, CREDS_TASK_PRIORITY,
                             NULL) != OSAL_ERR_OK) {
        ESP_LOGE(TAG, "Failed to start the credentials task");
        /* Undo, so that a later call starts again from scratch. */
        osal_semaphore_delete(s_lock);
        osal_semaphore_delete(s_request);
        osal_semaphore_delete(s_done);
        s_lock = s_request = s_done = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* Hand one fetch to the credentials task and wait for it. */
static const esp_rmaker_aws_sign_creds_t *refresh_on_task(void)
{
    const esp_rmaker_aws_sign_creds_t *creds = NULL;
    if (!s_lock || osal_semaphore_take(s_lock, osal_ticks_from_ms(CREDS_WAIT_MS)) != OSAL_ERR_OK) {
        ESP_LOGE(TAG, "The credentials task is not available");
        return NULL;
    }
    /* Drop a result that a caller gave up on. */
    osal_semaphore_take(s_done, 0);
    osal_semaphore_give(s_request);
    if (osal_semaphore_take(s_done, osal_ticks_from_ms(CREDS_WAIT_MS)) == OSAL_ERR_OK) {
        creds = s_err == ESP_OK ? s_creds : NULL;
    } else {
        ESP_LOGE(TAG, "Timeout waiting for the credentials fetch");
    }
    osal_semaphore_give(s_lock);
    return creds;
}

/* Assume the RainMaker Neo device-video role and get the AWS credentials */
int rmaker_fetch_aws_credentials(uint64_t user_data,
                                 const char **pAK, uint32_t *pAKLen,
                                 const char **pSK, uint32_t *pSKLen,
                                 const char **pTok, uint32_t *pTokLen,
                                 uint32_t *pExp)
{
    (void)user_data;

    /* The strings stay valid until the next refresh. kvs_signaling copies them
     * before it fetches again. */
    const esp_rmaker_aws_sign_creds_t *creds = refresh_on_task();
    if (!creds) {
        ESP_LOGE(TAG, "Failed to get AWS credentials");
        return -1;
    }

    /* kvs_signaling takes the expiration in seconds from now. A refresh that
     * succeeded means the clock is trusted. */
    int64_t expires_in = creds->expiration_epoch_sec - (int64_t)time(NULL);
    *pAK = creds->access_key_id;
    *pAKLen = (uint32_t)strlen(creds->access_key_id);
    *pSK = creds->secret_access_key;
    *pSKLen = (uint32_t)strlen(creds->secret_access_key);
    *pTok = creds->session_token;
    *pTokLen = (uint32_t)strlen(creds->session_token);
    *pExp = expires_in > 0 ? (uint32_t)expires_in : 0;

    ESP_LOGI(TAG, "AWS credentials expire in %" PRId64 " s", expires_in);
    return 0;
}
