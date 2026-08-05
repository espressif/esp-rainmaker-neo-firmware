/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_mqtt_downloader.c
 * @brief MQTT implementation for image downloading using AWS IoT Core MQTT File Streams
 */

/* Includes *************************************************************/

/* Declarations */
#include "esp_rmaker_ota.h"

/* Standard includes */
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

/* ESP RMaker includes */
#include "esp_rmaker_mqtt_impl.h"
#include "esp_rmaker_credentials.h"
#include "esp_rmaker_common_events.h"
#include "ota_jobs.h"
#include "ota_image_progress.h"
#include "esp_rmaker_work_queue.h"
#include "retry/esp_rmaker_backoff.h"
#include "util/esp_rmaker_convert_hex.h"
#include "util/esp_rmaker_convert_base64.h"

/* Platform common includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_semaphore.h"
#include "osal_event_loop.h"

/* AWS MQTT File Streams includes */
#include "MQTTFileDownloader.h"
#include "MQTTFileDownloader_defaults.h"
#include "MQTTFileDownloader_cbor.h"

/* Handler includes */
#include "ota_image_handler.h"
#include "ota_timeout_handler.h"

/* NVS resume tracker */
#include "ota_nvs.h"

/* Bitmap tracker */
#include "ota_mqtt_downloader_bitmap.h"

/* MQTT channels includes */
#include "network/ota_mqtt_channels.h"

/* Configuration includes */
#include "sdkconfig.h"

/* Types ******************************************************************/

/**
 * @brief Payload to store the received data
 */
typedef struct {
    void *payload;
    size_t payload_len;
} mqtt_downloader_payload_t;

typedef struct {
    uint32_t file_id; /* File ID */
    uint32_t block_offset; /* Block offset */
    size_t block_count; /* Number of blocks to request. Will be decremented as blocks are processed */
    uint8_t *bitmap; /* Bitmap */
    size_t bitmap_len; /* Length of the bitmap */
} mqtt_downloader_request_t;

/**
 * @brief Image context to keep track of the image download progress
 */
typedef struct {
    /**
     * @brief Image handler context
     */
    image_handler_ctx_t image_handler_ctx;

    /**
     * @brief File ID within the stream, as declared by the job doc.
     */
    uint32_t file_id;

    /**
     * @brief Progress tracking
     */
    struct {
        mqtt_downloader_bitmap_t bitmap;   /* Bitmap to keep track of blocks that have been processed */
        mqtt_downloader_request_t request; /* Request to send to AWS */
        image_progress_ctx_t tracking_ctx; /* Progress tracking context */
    } progress;
} mqtt_image_ctx_t;

/**
 * @brief Context for block request handling
 */
typedef struct {
    rmaker_ota_timeout_handler_handle_t timeout_handler; /** Timeout handler for the block request */
    uint16_t flags;                                      /** Flags for the block request */
    uint16_t blocks_per_request;                         /** Number of blocks to request per request. Dynamic for flow control. */
    uint32_t retry_count;                                /** Retry count for the block request */
    uint32_t max_retries;                                /** Maximum number of retries for the block request */
    osal_semaphore_handle_t mutex;            /** Mutex for the context */
} mqtt_downloader_block_request_ctx_t;

/**
 * @brief Downloader context
 */
typedef struct {
    /* AWS MQTT File Downloader context */
    MqttFileDownloaderContext_t aws_ctx;

    /* Current image context */
    mqtt_image_ctx_t current_image_ctx;

    /* Block request context */
    mqtt_downloader_block_request_ctx_t block_request_ctx;

    /* Download state */
    bool active;
    const esp_rmaker_ota_ft_ctx_t *ft_handler;

    /* OTA data */
    const esp_rmaker_ota_data_t *ota_data;

    /* Subscription intent: true between subscribe-intent and explicit unsubscribe.
     * Governs whether reconnect events should re-attempt subscribe. */
    bool sub_intended;

    /* Auto-resume state */
    bool resume_enabled;                       /* file_md5 present + RMNG_OTA_RESUME: persist/clear the tracker */
    bool resume_pending;                       /* a matching tracker was found: open the partition via resume */
    esp_rmaker_ota_resume_desc_t resume_desc;  /* descriptor persisted alongside the bitmap */
} mqtt_downloader_ctx_t;

/* Constants ******************************************************************/

/**
 * @brief Maximum retries = floor(total blocks / this divide factor)
 */
#define MQTT_DOWNLOADER_MAX_RETRIES_DIVIDE_FACTOR CONFIG_RMNG_OTA_MAX_RETRIES_DIVIDE_FACTOR

/**
 * @brief Number of blocks to request per request
 */
#define MQTT_DOWNLOADER_BLOCKS_PER_REQUEST CONFIG_RMNG_OTA_MQTT_BLOCKS_PER_REQUEST

/**
 * @brief Timeout for block requests in milliseconds
 */
#define MQTT_DOWNLOADER_BLOCK_REQUEST_TIMEOUT_MS (uint32_t)CONFIG_RMNG_OTA_REQUEST_TIMEOUT_MS

/**
 * @brief Block size to use for MQTT downloader
 */
#define MQTT_DOWNLOADER_BLOCK_SIZE mqttFileDownloader_CONFIG_BLOCK_SIZE

/* AWS IoT OTA has a limit of 128KB per request */
#if MQTT_DOWNLOADER_BLOCKS_PER_REQUEST * MQTT_DOWNLOADER_BLOCK_SIZE > (128 * 1024)
#error "(blocks per request) * (block size) must be less than 128KB"
#endif

/**
 * @brief Data type to use for MQTT downloader
 */
#if CONFIG_RMNG_OTA_MQTT_DATA_TYPE_CBOR
#define MQTT_DOWNLOADER_DATA_TYPE DATA_TYPE_CBOR
#elif CONFIG_RMNG_OTA_MQTT_DATA_TYPE_JSON
#define MQTT_DOWNLOADER_DATA_TYPE DATA_TYPE_JSON
#else
#error "Invalid data type configuration: RMNG_OTA_MQTT_DATA_TYPE"
#endif

/**
 * @brief Flags for the block request
 */
typedef enum {
    MQTT_DOWNLOADER_BLOCK_REQUEST_FLAG_SUCCESS = (1 << 0), /** Success flag */
    MQTT_DOWNLOADER_BLOCK_REQUEST_FLAG_FAILURE = (1 << 1), /** Timeout flag */
} mqtt_downloader_block_request_flag_t;

/**
 * @brief Subscription retry backoff: base and max delays.
 */
#define MQTT_DOWNLOADER_SUB_RETRY_BASE_DELAY_MS 1000
#define MQTT_DOWNLOADER_SUB_RETRY_MAX_DELAY_MS (5 * 60 * 1000)

/**
 * @brief Logging tag
 */
static const char *TAG = "rmng_ota_dl_mqtt";

/* Private variables **********************************************************/

/**
 * @brief Global MQTT downloader chunk buffer.
 *
 * One block-sized scratch buffer reused for every received data block.
 * Allocated (preferring SPIRAM) when a download starts and freed in
 * downloader cleanup.
 */
static uint8_t *g_mqtt_downloader_chunk_buffer = NULL;

/**
 * @brief Global downloader context
 */
static mqtt_downloader_ctx_t g_mqtt_downloader_ctx = {0};

/**
 * @brief Subscription retry context.
 * Separate from the block-request backoff; governs re-subscribe after
 * disconnect/reconnect or a SUBACK failure.
 */
static esp_rmaker_backoff_retry_context_t g_sub_retry_ctx = {
    .handle = NULL,
    .delay_ctx = {
        .delay_ms = {
            .current = MQTT_DOWNLOADER_SUB_RETRY_BASE_DELAY_MS,
            .max = MQTT_DOWNLOADER_SUB_RETRY_MAX_DELAY_MS,
        },
        .params = {
            .exp_factor = 2,
            .max_jitter_ms = MQTT_DOWNLOADER_SUB_RETRY_BASE_DELAY_MS,
        },
    },
};

/* Private function declarations **********************************************/

/* --- Payload construction --------------------------------------------------- */

/**
 * @brief Create a payload holding a copy of the given bytes
 *
 * @param[in] payload Bytes to copy into the new payload
 * @param[in] payload_size Size of payload in bytes
 *
 * @return The new payload, or NULL on allocation failure. Free with payload_free().
 */
static mqtt_downloader_payload_t *payload_create(const void *payload, size_t payload_size);

/**
 * @brief Free a payload
 *
 * @param[in] payload Payload to free
 */
static void payload_free(mqtt_downloader_payload_t *payload);

/* --- Request construction --------------------------------------------------- */

/**
 * @brief Get the next block request.
 * @details The bitmap is encoded according to https://docs.aws.amazon.com/iot/latest/developerguide/mqtt-based-file-delivery-in-devices.html#mqtt-based-file-delivery-get-getstream.
 *
 * @param[in] file_id Stream file ID to request blocks for
 * @param[in] block_count Total number of blocks in the file
 * @param[in] bitmap Bitmap to get the next block request for
 * @param[out] request Request to store the next block request in
 * - caller is responsible for releasing the request using request_deinit()
 * @return ESP_RMAKER_OK on success, otherwise ESP_RMAKER_FAIL
 */
static esp_rmaker_error_t request_get_next(uint32_t file_id, size_t block_count, const mqtt_downloader_bitmap_t *bitmap, mqtt_downloader_request_t *request);

/**
 * @brief Convert a request to a CBOR payload
 *
 * @param[in] request Request to convert
 * @param[out] payload Payload to store the converted request in
 * @param[in] payload_size Size of the payload buffer
 * @param[out] payload_len Length of the converted request
 */
static esp_rmaker_error_t request_to_cbor_payload(const mqtt_downloader_request_t *request, void *payload, size_t payload_size, size_t *payload_len);

/**
 * @brief Convert a request to a JSON payload
 *
 * @param[in] request Request to convert
 * @param[out] payload Payload to store the converted request in
 * @param[in] payload_size Size of the payload buffer
 * @param[out] payload_len Length of the converted request
 */
static esp_rmaker_error_t request_to_json_payload(const mqtt_downloader_request_t *request, void *payload, size_t payload_size, size_t *payload_len);

/**
 * @brief Convert a request to an MQTT payload
 *
 * @param[in] request Request to convert
 * @param[out] payload Payload to store the converted request in
 * @param[in] payload_size Size of the payload buffer
 * @param[out] payload_len Length of the converted request
 */
static esp_rmaker_error_t request_to_mqtt_payload(const mqtt_downloader_request_t *request, void *payload, size_t payload_size, size_t *payload_len);

/**
 * @brief Deinitialize a request
 *
 * @param[in] request Request to deinitialize
 */
static void request_deinit(mqtt_downloader_request_t *request);

/* --- Block request handling ------------------------------------------------- */

/**
 * @brief Initialize a block request context
 *
 * @param[out] block_request_ctx Block request context to initialize
 * @param[in] max_retries Maximum number of retries for the block request
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
static esp_rmaker_error_t block_request_init(mqtt_downloader_block_request_ctx_t *block_request_ctx, uint32_t max_retries);

/**
 * @brief Deinitialize a block request context
 *
 * @param[in] block_request_ctx Block request context to deinitialize
 */
static void block_request_deinit(mqtt_downloader_block_request_ctx_t *block_request_ctx);

/**
 * @brief Lock a block request context
 *
 * @param[in] block_request_ctx Block request context to lock
 */
#define block_request_lock(block_request_ctx) osal_semaphore_take(block_request_ctx->mutex, OSAL_MAX_DELAY)

/**
 * @brief Unlock a block request context
 *
 * @param[in] block_request_ctx Block request context to unlock
 */
#define block_request_unlock(block_request_ctx) osal_semaphore_give(block_request_ctx->mutex)

/**
 * @brief Begin a block request context
 * - Resets the context flags
 * - Starts the timeout handler
 *
 * @param[in] block_request_ctx Block request context to reset
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
static esp_rmaker_error_t block_request_begin_context(mqtt_downloader_block_request_ctx_t *block_request_ctx);

/**
 * @brief Send the next block request to AWS IoT
 *
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
static esp_rmaker_error_t block_request_send_next(void);

/**
 * @brief Actions to take on a successful block request.
 * This assumes that the block request was successful, and the data was received and processed.
 *
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
static esp_rmaker_error_t block_request_on_success(void);

/**
 * @brief Work queue task to handle a failed block request.
 *
 * @param[in] unused Unused argument
 */
static void block_request_on_failure_work_queue_task(void *unused);

/**
 * @brief Timeout callback for a failed block request.
 *
 * @param[in] unused Unused argument
 */
static void block_request_on_failure_timeout_callback(void *unused);

/* --- Process handling -------------------------------------------------------- */

/**
 * @brief Report and cleanup:
 * - End the image handler
 * - Report to the job state
 * - Cleanup the downloader context
 *
 * @param[in] status Status of the image download
 */
#define report_and_cleanup(status) image_handler_end_and_cleanup(&g_mqtt_downloader_ctx.current_image_ctx.image_handler_ctx, status, cleanup_downloader)

/**
 * @brief MQTT message received callback
 *
 * @param[in] topic Topic on which the message was received
 * @param[in] topic_len Length of the topic
 * @param[in] payload Message payload
 * @param[in] payload_len Length of the payload
 * @param[in] priv_data Private data (downloader context)
 */
static void on_mqtt_message_received(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data);

/**
 * @brief Work queue task to write chunk to image handler
 *
 * @param[in] image_handler_ctx_arg Image handler context argument
 */
static void write_chunk_to_image_handler_work_queue_task(void *image_handler_ctx_arg);

/**
 * @brief Subscribe to MQTT stream topics
 *
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
static esp_rmaker_error_t subscribe_to_stream_topics(void);

/**
 * @brief Unsubscribe from MQTT stream topics
 *
 * @return esp_rmaker_error_t ESP_RMAKER_OK on success
 */
static esp_rmaker_error_t unsubscribe_from_stream_topics(void);

/**
 * @brief Clean up the downloader context
 */
static void cleanup_downloader(void);

#if CONFIG_RMNG_OTA_RESUME
/* --- Auto-resume (forward declarations) ------------------------------------- */
static void resume_attempt(void);
static void resume_persist(void);
#endif

/* --- Subscription retry ------------------------------------------------------ */

/**
 * @brief Scheduler-context callback; dispatches the subscribe attempt to the work queue.
 */
static void sub_retry_sched_cb(void *unused);

/**
 * @brief Work-queue task: attempts to (re)subscribe to stream topics.
 * On local failure, schedules another backoff retry.
 */
static void sub_retry_work(void *unused);

/**
 * @brief Event handler for MQTT connect/disconnect and SUBACK events.
 */
static void on_rmaker_mqtt_event(void *arg, osal_event_base_t base, int32_t id, void *data);

/**
 * @brief Register / unregister handlers for the three RMAKER_MQTT_EVENT_* ids we care about.
 */
static esp_rmaker_error_t register_mqtt_event_handlers(void);
static void unregister_mqtt_event_handlers(void);

/* Private function definitions **********************************************/

/* --- Payload construction --------------------------------------------------- */

static mqtt_downloader_payload_t *payload_create(const void *payload, size_t payload_size)
{
    /* Check parameters */
    if (!payload || payload_size == 0) {
        OSAL_LOGE(TAG, "Invalid parameters: payload: %p, payload_size: %" PRIu32, payload, (uint32_t)payload_size);
        return NULL;
    }

    /* Create payload */
    mqtt_downloader_payload_t *payload_obj = (mqtt_downloader_payload_t *)OSAL_MALLOC_EXTRAM(sizeof(mqtt_downloader_payload_t));
    if (!payload) {
        OSAL_LOGE(TAG, "Failed to allocate memory for payload");
        return NULL;
    }

    payload_obj->payload = OSAL_MALLOC_EXTRAM(payload_size);
    if (!payload_obj->payload) {
        OSAL_LOGE(TAG, "Failed to allocate memory for payload");
        free(payload_obj);
        return NULL;
    }

    memcpy(payload_obj->payload, payload, payload_size);
    payload_obj->payload_len = payload_size;

    /* Success */
    return payload_obj;
}

static void payload_free(mqtt_downloader_payload_t *payload)
{
    /* Check parameters */
    if (!payload) {
        return;
    }

    if (payload->payload) {
        free(payload->payload);
    }
    payload->payload_len = 0;

    free(payload);
}
/* --- Request construction --------------------------------------------------- */

static esp_rmaker_error_t request_get_next(uint32_t file_id, size_t block_count, const mqtt_downloader_bitmap_t *bitmap, mqtt_downloader_request_t *request)
{
    /* Check parameters */
    if (!bitmap || bitmap->bitmap == NULL || request == NULL) {
        OSAL_LOGE(TAG, "Invalid parameters: bitmap: %p, request: %p", bitmap, request);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* If no unprocessed blocks, return empty bitmap */
    if (bitmap->unprocessed_block_count == 0) {
        request->file_id = 0;
        request->block_count = 0;
        request->block_offset = 0;
        request->bitmap = NULL;
        request->bitmap_len = 0;
        return ESP_RMAKER_OK;
    }

    /* Find the first unprocessed block (minimum block ID where bit is set) */
    uint32_t first_unprocessed_block = UINT32_MAX;
    uint32_t last_unprocessed_block = 0;
    uint32_t unprocessed_block_count = 0;

    size_t cell_count = (bitmap->block_count + 7) / 8;
    bool reached_block_count = false;
    for (size_t cell_idx = 0; cell_idx < cell_count; cell_idx++) {
        if (bitmap->bitmap[cell_idx] != 0) {
            for (uint32_t bit_idx = 0; bit_idx < 8; bit_idx++) {
                uint32_t block_id = cell_idx * 8 + bit_idx;
                if (block_id >= bitmap->block_count) {
                    break;
                }
                if (bitmap->bitmap[cell_idx] & (1 << bit_idx)) {
                    if (block_id < first_unprocessed_block) {
                        first_unprocessed_block = block_id;
                    }
                    if (block_id > last_unprocessed_block) {
                        last_unprocessed_block = block_id;
                    }
                    if (++unprocessed_block_count == block_count) {
                        reached_block_count = true;
                        break;
                    }
                }
            }
        }

        if (reached_block_count) {
            break;
        }
    }

    /* Calculate bitmap size starting from offset */
    uint32_t bitmap_range = last_unprocessed_block - first_unprocessed_block + 1;
    size_t compact_cell_count = (bitmap_range + 7) / 8;

    /* Allocate compact bitmap */
    uint8_t *compact_bitmap = (uint8_t *)OSAL_CALLOC_EXTRAM(compact_cell_count, sizeof(uint8_t));
    if (!compact_bitmap) {
        OSAL_LOGE(TAG, "Failed to allocate compact bitmap");
        return ESP_RMAKER_NO_MEM;
    }

    /* Build compact bitmap starting from offset */
    size_t first_cell_idx = first_unprocessed_block / 8;
    size_t last_cell_idx = last_unprocessed_block / 8;
    for (size_t cell_idx = first_cell_idx; cell_idx <= last_cell_idx; cell_idx++) {
        if (bitmap->bitmap[cell_idx] != 0) {
            for (uint32_t bit_idx = 0; bit_idx < 8; bit_idx++) {
                uint32_t block_id = cell_idx * 8 + bit_idx;
                if (block_id > last_unprocessed_block) {
                    break;
                }
                if (bitmap->bitmap[cell_idx] & (1 << bit_idx)) {
                    /**
                        Using https://docs.aws.amazon.com/iot/latest/developerguide/mqtt-based-file-delivery-in-devices.html:
                        block number = (bit position + (byte offset * 8) + base offset)
                        --> bit position + (byte offset * 8) = (block number - base offset) = R
                        so bit position = R % 8 and byte offset = R // 8
                     */
                    uint32_t compact_block_id = block_id - first_unprocessed_block;
                    uint32_t compact_cell_idx = compact_block_id / 8;
                    uint32_t compact_bit_idx = compact_block_id % 8;
                    compact_bitmap[compact_cell_idx] |= (1 << compact_bit_idx);

                    OSAL_LOGD(TAG, "Block %" PRIu32 " mapped to compact bitmap position %" PRIu32 " (cell %" PRIu32 ", bit %" PRIu32 ")", block_id, compact_block_id, compact_cell_idx, compact_bit_idx);
                }
            }
        }
    }

    /* Success */
    request->bitmap = compact_bitmap;
    request->bitmap_len = compact_cell_count;
    request->block_offset = first_unprocessed_block;
    request->block_count = unprocessed_block_count;
    request->file_id = file_id;
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t request_to_cbor_payload(const mqtt_downloader_request_t *request, void *payload, size_t payload_size, size_t *payload_len)
{
    /* Check parameters */
    if (!request || !payload || payload_size == 0) {
        OSAL_LOGE(TAG, "Invalid parameters: request: %p, payload: %p, payload_size: %" PRIu32, request, payload, (uint32_t)payload_size);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Convert request to CBOR payload */
    bool ret = CBOR_Encode_GetStreamRequestMessage(
                   (uint8_t *)payload,
                   payload_size,
                   payload_len,
                   "rdy",
                   request->file_id,
                   (uint32_t)MQTT_DOWNLOADER_BLOCK_SIZE,
                   request->block_offset,
                   request->bitmap,
                   request->bitmap_len,
                   request->block_count
               );
    if (!ret) {
        OSAL_LOGE(TAG, "Failed to encode request to CBOR payload");
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t request_to_json_payload(const mqtt_downloader_request_t *request, void *payload, size_t payload_size, size_t *payload_len)
{
    /* Check parameters */
    if (!request || !payload || payload_size == 0) {
        OSAL_LOGE(TAG, "Invalid parameters: request: %p, payload: %p, payload_size: %" PRIu32, request, payload, (uint32_t)payload_size);
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Convert request to JSON payload */
    size_t bitmap_base64_len = 0;
    char *bitmap_base64_str = esp_rmaker_convert_bytes_to_base64(request->bitmap, request->bitmap_len, &bitmap_base64_len);
    if (!bitmap_base64_str) {
        OSAL_LOGE(TAG, "Failed to convert bitmap to base64");
        return ESP_RMAKER_NO_MEM;
    }
    *payload_len = snprintf(payload, payload_size,
                            "{\"s\":1"
                            ",\"f\":%" PRIu32
                            ",\"l\":%" PRIu32
                            ",\"o\":%" PRIu32
                            ",\"n\":%" PRIu32
                            ",\"b\": \"%*s\"}",
                            request->file_id,
                            (uint32_t)MQTT_DOWNLOADER_BLOCK_SIZE,
                            request->block_offset,
                            (uint32_t)request->block_count,
                            (int)bitmap_base64_len,
                            bitmap_base64_str);
    free(bitmap_base64_str);
    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t request_to_mqtt_payload(const mqtt_downloader_request_t *request, void *payload, size_t payload_size, size_t *payload_len)
{
    /* Check parameters */
    if (!request || !payload || payload_size == 0) {
        OSAL_LOGE(TAG, "Invalid parameters: request: %p, payload: %p, payload_size: %" PRIu32, request, payload, (uint32_t)payload_size);
        return ESP_RMAKER_INVALID_ARG;
    }

    if (MQTT_DOWNLOADER_DATA_TYPE == DATA_TYPE_CBOR) {
        /* Convert request to CBOR payload */
        return request_to_cbor_payload(request, payload, payload_size, payload_len);

    } else if (MQTT_DOWNLOADER_DATA_TYPE == DATA_TYPE_JSON) {
        /* Convert request to JSON payload */
        return request_to_json_payload(request, payload, payload_size, payload_len);

    } else {
        /* Invalid data type */
        OSAL_LOGE(TAG, "Cannot convert request to MQTT payload: invalid data type: %d", MQTT_DOWNLOADER_DATA_TYPE);
        return ESP_RMAKER_INVALID_ARG;
    }
}

static void request_deinit(mqtt_downloader_request_t *request)
{
    /* Check parameters */
    if (!request) {
        OSAL_LOGE(TAG, "Invalid parameters: request: %p", request);
        return;
    }

    /* Free bitmap */
    if (request->bitmap) {
        free(request->bitmap);
        request->bitmap = NULL;
    }
    request->bitmap_len = 0;
    request->block_count = 0;
    request->block_offset = 0;
    request->file_id = 0;
}

/* Block request handling ----------------------------------------------------- */

static esp_rmaker_error_t block_request_init(mqtt_downloader_block_request_ctx_t *block_request_ctx, uint32_t max_retries)
{
    /* Check parameters */
    if (!block_request_ctx) {
        OSAL_LOGE(TAG, "Invalid parameters: block_request_ctx: %p", block_request_ctx);
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err;

    /* Initialize the timeout handler */
    rmaker_ota_timeout_handler_config_t timeout_handler_config = {
        .timeout_ms = MQTT_DOWNLOADER_BLOCK_REQUEST_TIMEOUT_MS,
        .callback = block_request_on_failure_timeout_callback,
        .priv_data = NULL
    };
    err = rmaker_ota_timeout_handler_init(&timeout_handler_config, &block_request_ctx->timeout_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize timeout handler");
        return err;
    }

    /* Initialize the mutex */
    block_request_ctx->mutex = osal_semaphore_create_mutex();
    if (!block_request_ctx->mutex) {
        OSAL_LOGE(TAG, "Failed to create mutex");
        return ESP_RMAKER_NO_MEM;
    }

    /* Initialize the remaining parameters */
    block_request_ctx->flags = 0;
    block_request_ctx->blocks_per_request = MQTT_DOWNLOADER_BLOCKS_PER_REQUEST; /* Start with the default number of blocks per request */
    block_request_ctx->retry_count = 0;
    block_request_ctx->max_retries = max_retries;
    return ESP_RMAKER_OK;
}

static void block_request_deinit(mqtt_downloader_block_request_ctx_t *block_request_ctx)
{
    /* Check parameters */
    if (!block_request_ctx) {
        OSAL_LOGE(TAG, "Invalid parameters: block_request_ctx: %p", block_request_ctx);
        return;
    }

    /* Deinitialize the timeout handler */
    if (block_request_ctx->timeout_handler) {
        esp_rmaker_error_t err = rmaker_ota_timeout_handler_deinit(block_request_ctx->timeout_handler);
        if (err != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to deinitialize timeout handler");
        }
    }

    /* Delete the mutex */
    if (block_request_ctx->mutex) {
        osal_semaphore_delete(block_request_ctx->mutex);
        block_request_ctx->mutex = NULL;
    }

    /* Reset the context */
    memset(block_request_ctx, 0, sizeof(mqtt_downloader_block_request_ctx_t));
}

static esp_rmaker_error_t block_request_begin_context(mqtt_downloader_block_request_ctx_t *block_request_ctx)
{
    /* Check parameters */
    if (!block_request_ctx) {
        OSAL_LOGE(TAG, "Invalid parameters: block_request_ctx: %p", block_request_ctx);
        return ESP_RMAKER_INVALID_ARG;
    }

    esp_rmaker_error_t err = ESP_RMAKER_OK;

    /* Reset the context */
    block_request_lock(block_request_ctx);
    block_request_ctx->flags = 0;

    /* Start the timeout handler */
    err = rmaker_ota_timeout_handler_restart(block_request_ctx->timeout_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to restart timeout handler");
    }
    block_request_unlock(block_request_ctx);
    OSAL_LOGD(TAG, "Block request error timeout will trigger in %" PRIu32 " ms", MQTT_DOWNLOADER_BLOCK_REQUEST_TIMEOUT_MS);
    return err;
}

static esp_rmaker_error_t block_request_send_next(void)
{
    esp_rmaker_error_t err;

    /* Get the next block request */
    mqtt_downloader_bitmap_t *bitmap = &g_mqtt_downloader_ctx.current_image_ctx.progress.bitmap;
    mqtt_downloader_request_t *request = &g_mqtt_downloader_ctx.current_image_ctx.progress.request;
    mqtt_downloader_block_request_ctx_t *block_request_ctx = &g_mqtt_downloader_ctx.block_request_ctx;
    block_request_lock(block_request_ctx);
    uint32_t blocks_per_request = block_request_ctx->blocks_per_request;
    block_request_unlock(block_request_ctx);
    err = request_get_next(
              g_mqtt_downloader_ctx.current_image_ctx.file_id,
              blocks_per_request,
              bitmap,
              request
          );
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get next block request");
        goto block_request_send_next_fail;
    }

    /* Convert request to MQTT payload */
    uint8_t payload[GET_STREAM_REQUEST_BUFFER_SIZE];
    size_t payload_len = 0;
    err = request_to_mqtt_payload(request, payload, sizeof(payload), &payload_len);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to convert request to MQTT payload");
        goto block_request_send_next_fail;
    }

    /* Begin the block request context */
    block_request_begin_context(block_request_ctx);

    /* Publish request to get topic */
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_OTA,
        .sub = MQTT_CHANNEL_SUB_OTA_STREAM_DATA_REQUEST,
    };

    osal_err_t status = esp_rmaker_mqtt_impl.publish(
                            &channel,
                            g_mqtt_downloader_ctx.aws_ctx.topicGetStream,
                            g_mqtt_downloader_ctx.aws_ctx.topicGetStreamLength,
                            payload,
                            payload_len,
                            QoS1,
                            false
                        );

    if (status != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to publish get stream request: %d", status);
        err = ESP_RMAKER_FAIL;
        goto block_request_send_next_fail;
    }

    OSAL_LOGD(TAG, "Published get request to topic: %.*s",
              (int)g_mqtt_downloader_ctx.aws_ctx.topicGetStreamLength,
              g_mqtt_downloader_ctx.aws_ctx.topicGetStream);

    return ESP_RMAKER_OK;

block_request_send_next_fail:
    /* Let the request timeout */
    return err;
}

static esp_rmaker_error_t block_request_on_success(void)
{
    esp_rmaker_error_t err = ESP_RMAKER_OK;
    mqtt_image_ctx_t *image_ctx = &g_mqtt_downloader_ctx.current_image_ctx;
    mqtt_downloader_block_request_ctx_t *block_request_ctx = &g_mqtt_downloader_ctx.block_request_ctx;

    /* Check if download is complete */
    if (image_ctx->progress.bitmap.unprocessed_block_count == 0) {
        /* Download complete, report and cleanup */
        report_and_cleanup(IMAGE_HANDLER_STATUS_SUCCESS);
        return ESP_RMAKER_OK;
    }

    /* If there are remaining blocks we are waiting for, then do nothing */
    if ((--image_ctx->progress.request.block_count) > 0) {
        return ESP_RMAKER_OK;
    }

    /* Lock the block request context */
    block_request_lock(block_request_ctx);

    /* Check if a non-success event has already occurred */
    if ((block_request_ctx->flags & ~MQTT_DOWNLOADER_BLOCK_REQUEST_FLAG_SUCCESS) > 0) {
        OSAL_LOGW(TAG, "Non-success event has already occurred, skipping success handling");
        block_request_unlock(block_request_ctx);
        return ESP_RMAKER_INVALID_STATE;
    }

    /* Stop the block request timeout handler */
    err = rmaker_ota_timeout_handler_stop(block_request_ctx->timeout_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to stop block request timeout handler");
    }

    /* Set success flag */
    block_request_ctx->flags |= MQTT_DOWNLOADER_BLOCK_REQUEST_FLAG_SUCCESS;
    block_request_ctx->retry_count = 0; /* Reset retry count */
    if (block_request_ctx->blocks_per_request < MQTT_DOWNLOADER_BLOCKS_PER_REQUEST) {
        /* Increment the blocks per request */
        block_request_ctx->blocks_per_request++;
    }
    block_request_unlock(block_request_ctx);

#if CONFIG_RMNG_OTA_RESUME
    /* Batch boundary: persist the bitmap so an interrupted download can resume here.
     * Only on batch completion (not per block) to bound NVS wear. */
    resume_persist();
#endif

    /* Send the next block request */
    request_deinit(&image_ctx->progress.request);
    err = block_request_send_next();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to request next block(s): %d", err);
        return err;
    }

    return ESP_RMAKER_OK;
}

static void block_request_on_failure_work_queue_task(void *unused)
{
    mqtt_downloader_block_request_ctx_t *block_request_ctx = &g_mqtt_downloader_ctx.block_request_ctx;
    if (block_request_ctx->mutex == NULL) {
        OSAL_LOGW(TAG, "Block request context mutex is NULL, skipping failure handling");
        return;
    }

    /* Lock the block request context */
    block_request_lock(block_request_ctx);

    /* Check if a success event has already occurred */
    if (block_request_ctx->flags & MQTT_DOWNLOADER_BLOCK_REQUEST_FLAG_SUCCESS) {
        OSAL_LOGW(TAG, "Success event has already occurred, skipping failure handling");
        block_request_unlock(block_request_ctx);
        return;
    }
    /* Check if a failure event has already been handled */
    if (block_request_ctx->flags & MQTT_DOWNLOADER_BLOCK_REQUEST_FLAG_FAILURE) {
        OSAL_LOGW(TAG, "Failure event has already been handled, skipping failure handling");
        block_request_unlock(block_request_ctx);
        return;
    }

    /* Set failure flag */
    block_request_ctx->flags |= MQTT_DOWNLOADER_BLOCK_REQUEST_FLAG_FAILURE;

    /* Check that the retry count is less than the maximum retries */
    bool exceeded_max_retries = ((++block_request_ctx->retry_count) > block_request_ctx->max_retries);

    /* Reset the blocks per request to 1 */
    block_request_ctx->blocks_per_request = 1;
    block_request_unlock(block_request_ctx);

    if (exceeded_max_retries) {
        /* Maximum retries have been exceeded, report timeout and cleanup */
        OSAL_LOGE(TAG, "Maximum retries reached; reporting timeout");
        report_and_cleanup(IMAGE_HANDLER_STATUS_TIMEOUT);
    } else {
        /* Maximum retries have not been exceeded, retry the block request */
        OSAL_LOGW(TAG, "Failed some blocks in the block request, retrying... (%" PRIu32 "/%" PRIu32 " attempt(s))", block_request_ctx->retry_count, block_request_ctx->max_retries);
        if (block_request_send_next() != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to retry block request");
            return;
        }
    }
}

static void block_request_on_failure_timeout_callback(void *unused)
{
    esp_rmaker_error_t err;
    err = esp_rmaker_work_queue_add_task(block_request_on_failure_work_queue_task, NULL);
    if (err == ESP_RMAKER_OK) {
        OSAL_LOGI(TAG, "Block request on failure work queue task added successfully");
        return;
    }

    /* Reschedule the timeout callback */
    OSAL_LOGW(TAG, "Failed to add block request on failure work queue task, rescheduling timeout callback");
    err = rmaker_ota_timeout_handler_restart(g_mqtt_downloader_ctx.block_request_ctx.timeout_handler);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to restart timeout handler");
    }
}

/* --- Process handling -------------------------------------------------------- */

static void on_mqtt_message_received(const char *topic, size_t topic_len, void *payload, size_t payload_len, void *priv_data)
{
    mqtt_downloader_ctx_t *ctx = &g_mqtt_downloader_ctx;
    if (!ctx->active) {
        goto on_mqtt_message_received_end;
    }

    OSAL_LOGD(TAG, "Received MQTT message on topic: %.*s (%" PRIu32 " bytes)",
              (int)topic_len, topic, (uint32_t)payload_len);

    /* Check if this is a data block message */
    MQTTFileDownloaderStatus_t status = mqttDownloader_isDataBlockReceived(
                                            &ctx->aws_ctx,
                                            topic,
                                            topic_len
                                        );

    if (status != MQTTFileDownloaderSuccess) {
        OSAL_LOGD(TAG, "Message is not a data block");
        return;
    }

    /* Copy to buffer */
    mqtt_downloader_payload_t *payload_obj = payload_create(payload, payload_len);
    if (!payload_obj) {
        OSAL_LOGE(TAG, "Failed to create payload object");
        goto on_mqtt_message_received_end;
    }

    /* Add chunk writing task to the work queue */
    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(write_chunk_to_image_handler_work_queue_task, (void *)(uintptr_t)payload_obj);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add chunk writing task to work queue: %d", err);
        goto on_mqtt_message_received_end;
    }

on_mqtt_message_received_end:
    /* Let the request timeout */
    return;
}

static void write_chunk_to_image_handler_work_queue_task(void *payload_obj_arg)
{
    mqtt_downloader_payload_t *payload_obj = (mqtt_downloader_payload_t *)payload_obj_arg;
    if (!payload_obj) {
        OSAL_LOGE(TAG, "Invalid payload object");
        return;
    }

    /* Scratch buffer is allocated for the lifetime of an active download;
     * a data block arriving without it means we're mid-teardown - drop it. */
    if (!g_mqtt_downloader_chunk_buffer) {
        OSAL_LOGW(TAG, "Chunk buffer unavailable; dropping data block");
        payload_free(payload_obj);
        return;
    }

    mqtt_downloader_ctx_t *ctx = &g_mqtt_downloader_ctx;
    mqtt_image_ctx_t *image_ctx = &g_mqtt_downloader_ctx.current_image_ctx;
    do {
        /* Process the data block */
        int32_t file_id, block_id, block_size;
        size_t data_length;
        MQTTFileDownloaderStatus_t status = mqttDownloader_processReceivedDataBlock(
                                                &ctx->aws_ctx,
                                                payload_obj->payload,
                                                payload_obj->payload_len,
                                                &file_id,
                                                &block_id,
                                                &block_size,
                                                g_mqtt_downloader_chunk_buffer,
                                                &data_length
                                            );
        payload_free(payload_obj);

        if (status != MQTTFileDownloaderSuccess) {
            OSAL_LOGE(TAG, "Failed to process received data block: %d", status);
            break;
        }

        /* Blocks of other files in the same stream share this topic: drop them without
         * burning a retry, they are not part of this update. mqttDownloader_processReceivedDataBlock()
         * hands back a signed file id, so reject a negative one before comparing unsigned. */
        if (file_id < 0 || (uint32_t)file_id != image_ctx->file_id) {
            OSAL_LOGW(TAG, "Dropping block %" PRId32 " of file %" PRId32 ", expected file %" PRIu32,
                      block_id, file_id, image_ctx->file_id);
            return;
        }

        /* Validate block */
        if ((uint32_t)data_length != (uint32_t)block_size) {
            OSAL_LOGW(TAG, "Block size mismatch: received %" PRIu32 ", expected %" PRIu32, (uint32_t)data_length, (uint32_t)block_size);
            break;
        }

        /* Range-check before an offset is derived from block_id: bitmap_is_processed()
         * returns false for an out-of-range id, indistinguishable from "not processed". */
        if (block_id < 0 || (size_t)block_id >= image_ctx->progress.bitmap.block_count) {
            OSAL_LOGE(TAG, "Block id %" PRId32 " out of range (%" PRIu32 " blocks); dropping block",
                      block_id, (uint32_t)image_ctx->progress.bitmap.block_count);
            break;
        }

        /* Check if block is processed */
        if (mqtt_downloader_bitmap_is_processed(&image_ctx->progress.bitmap, block_id)) {
            OSAL_LOGW(TAG, "Block %" PRIu32 " already processed", block_id);
            return;
        }

        /* Write data to partition */
        image_handler_ctx_t *image_handler_ctx = &image_ctx->image_handler_ctx;
        bool resume_pending = false;
#if CONFIG_RMNG_OTA_RESUME
        resume_pending = ctx->resume_pending;
#endif
        if (image_handler_ctx->handle == NULL && image_handler_begin(ctx->ota_data->file_signature, ctx->ota_data->filesize, ctx->ft_handler, ctx->ota_data->fw_version, ctx->ota_data->file_md5, resume_pending, 0, image_handler_ctx) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to begin image handler");
            break;
        }
        if (image_handler_write_chunk(image_handler_ctx, g_mqtt_downloader_chunk_buffer, data_length, block_id * MQTT_DOWNLOADER_BLOCK_SIZE) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to write chunk to image handler");
            break;
        }

        /* Update progress */
        mqtt_downloader_bitmap_set_processed(&image_ctx->progress.bitmap, block_id);
        if (image_progress_add_bytes(&image_ctx->progress.tracking_ctx, data_length) != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to add bytes to image progress");
            break;
        }

        /* Call the block request on success handler */
        if (block_request_on_success() != ESP_RMAKER_OK) {
            OSAL_LOGE(TAG, "Failed to handle block request on success");
            break;
        }

        /* Success case */
        return;
    } while (0);

    /* Failure case */
    block_request_on_failure_work_queue_task(NULL);
}

static esp_rmaker_error_t subscribe_to_stream_topics(void)
{
    /* Record subscribe intent: governs reconnect-retry behaviour. */
    g_mqtt_downloader_ctx.sub_intended = true;

    /* Subscribe to the data topic */
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_OTA,
        .sub = MQTT_CHANNEL_SUB_OTA_STREAM_DATA_SUBSCRIBE,
    };

    osal_err_t status = esp_rmaker_mqtt_impl.subscribe(
                            &channel,
                            g_mqtt_downloader_ctx.aws_ctx.topicStreamData,
                            g_mqtt_downloader_ctx.aws_ctx.topicStreamDataLength,
                            on_mqtt_message_received,
                            QoS1,
                            &g_mqtt_downloader_ctx
                        );

    if (status != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to subscribe to stream data topic: %d", status);
        return ESP_RMAKER_FAIL;
    }

    OSAL_LOGI(TAG, "Subscribed to topic: %.*s",
              (int)g_mqtt_downloader_ctx.aws_ctx.topicStreamDataLength,
              g_mqtt_downloader_ctx.aws_ctx.topicStreamData);

    return ESP_RMAKER_OK;
}

static esp_rmaker_error_t unsubscribe_from_stream_topics(void)
{
    /* Drop subscribe intent and cancel any pending retry BEFORE issuing unsubscribe,
     * so a late SUBACK-fail or reconnect cannot re-arm the retry. */
    g_mqtt_downloader_ctx.sub_intended = false;
    esp_rmaker_backoff_reset(&g_sub_retry_ctx, MQTT_DOWNLOADER_SUB_RETRY_BASE_DELAY_MS);

    /* Unsubscribe from the data topic */
    osal_mqtt_event_loop_channel_t channel = {
        .main = MQTT_CHANNEL_MAIN_OTA,
        .sub = MQTT_CHANNEL_SUB_OTA_STREAM_DATA_UNSUBSCRIBE,
    };
    osal_err_t status = esp_rmaker_mqtt_impl.unsubscribe(
                            &channel,
                            g_mqtt_downloader_ctx.aws_ctx.topicStreamData,
                            g_mqtt_downloader_ctx.aws_ctx.topicStreamDataLength,
                            QoS1
                        );

    if (status != OSAL_ERR_OK) {
        OSAL_LOGW(TAG, "Failed to unsubscribe from stream data topic: %d", status);
    }

    return ESP_RMAKER_OK;
}

static void cleanup_downloader(void)
{
    /* Unregister event handlers first so a late MQTT event cannot race
     * with the teardown below. */
    unregister_mqtt_event_handlers();

    /* Unsubscribe from MQTT topics (clears sub_intended + cancels pending retry). */
    unsubscribe_from_stream_topics();

    /* Reset context */
    block_request_deinit(&g_mqtt_downloader_ctx.block_request_ctx);
    mqtt_downloader_bitmap_deinit(&g_mqtt_downloader_ctx.current_image_ctx.progress.bitmap);
    request_deinit(&g_mqtt_downloader_ctx.current_image_ctx.progress.request);
    memset(&g_mqtt_downloader_ctx, 0, sizeof(mqtt_downloader_ctx_t));

    /* Free the per-block scratch buffer (separate static, not part of ctx). */
    if (g_mqtt_downloader_chunk_buffer) {
        free(g_mqtt_downloader_chunk_buffer);
        g_mqtt_downloader_chunk_buffer = NULL;
    }
}

/* --- Subscription retry ------------------------------------------------------ */

static void sub_retry_sched_cb(void *unused)
{
    (void)unused;
    /* Scheduler context; dispatch to work queue so the subscribe call
     * runs on a safe task context. */
    esp_rmaker_error_t err = esp_rmaker_work_queue_add_task(sub_retry_work, NULL);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to enqueue sub retry work: %d; rescheduling", err);
        esp_rmaker_backoff_retry(&g_sub_retry_ctx, sub_retry_sched_cb, NULL);
    }
}

static void sub_retry_work(void *unused)
{
    (void)unused;
    if (!g_mqtt_downloader_ctx.sub_intended) {
        /* Raced with unsubscribe/cleanup; nothing to do. */
        return;
    }
    if (subscribe_to_stream_topics() != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Sub retry: local subscribe call failed; scheduling next attempt");
        esp_rmaker_backoff_retry(&g_sub_retry_ctx, sub_retry_sched_cb, NULL);
    }
    /* On success: SUBACK handler resets the backoff. If SUBACK never arrives, the
     * block-request timeout path will eventually fail the download. */
}

static void on_rmaker_mqtt_event(void *arg, osal_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    switch ((esp_rmaker_common_event_t)id) {
    case RMAKER_MQTT_EVENT_DISCONNECTED:
        /* Cancel any pending retry; keep sub_intended so CONNECTED re-arms it. */
        esp_rmaker_backoff_reset(&g_sub_retry_ctx, MQTT_DOWNLOADER_SUB_RETRY_BASE_DELAY_MS);
        break;

    case RMAKER_MQTT_EVENT_CONNECTED:
        if (g_mqtt_downloader_ctx.sub_intended) {
            /* Kick off an immediate attempt (current delay, no jitter/increment). */
            esp_rmaker_backoff_fire(&g_sub_retry_ctx, sub_retry_sched_cb, NULL);
        }
        break;

    case RMAKER_MQTT_EVENT_SUBSCRIBED: {
        const osal_mqtt_event_loop_data_on_complete_t *d = data;
        if (!d) {
            return;
        }
        if (d->channel.main != MQTT_CHANNEL_MAIN_OTA ||
                d->channel.sub != MQTT_CHANNEL_SUB_OTA_STREAM_DATA_SUBSCRIBE) {
            return;
        }
        if (d->status == OSAL_ERR_OK) {
            /* Fresh start for any future retry budget. */
            esp_rmaker_backoff_reset(&g_sub_retry_ctx, MQTT_DOWNLOADER_SUB_RETRY_BASE_DELAY_MS);
        } else if (g_mqtt_downloader_ctx.sub_intended) {
            OSAL_LOGW(TAG, "Stream SUBACK failed (status=%d); scheduling retry", d->status);
            esp_rmaker_backoff_retry(&g_sub_retry_ctx, sub_retry_sched_cb, NULL);
        }
        break;
    }

    default:
        break;
    }
}

static esp_rmaker_error_t register_mqtt_event_handlers(void)
{
    osal_err_t err;
    err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, on_rmaker_mqtt_event, NULL);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register CONNECTED handler: %d", err);
        return ESP_RMAKER_FAIL;
    }
    err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_DISCONNECTED, on_rmaker_mqtt_event, NULL);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register DISCONNECTED handler: %d", err);
        osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, on_rmaker_mqtt_event);
        return ESP_RMAKER_FAIL;
    }
    err = osal_event_handler_register(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_SUBSCRIBED, on_rmaker_mqtt_event, NULL);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "Failed to register SUBSCRIBED handler: %d", err);
        osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, on_rmaker_mqtt_event);
        osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_DISCONNECTED, on_rmaker_mqtt_event);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

static void unregister_mqtt_event_handlers(void)
{
    osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_CONNECTED, on_rmaker_mqtt_event);
    osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_DISCONNECTED, on_rmaker_mqtt_event);
    osal_event_handler_unregister(RMAKER_COMMON_EVENT, RMAKER_MQTT_EVENT_SUBSCRIBED, on_rmaker_mqtt_event);
}

/* --- Auto-resume ------------------------------------------------------------- */

#if CONFIG_RMNG_OTA_RESUME
/**
 * @brief Build the resume descriptor for the current MQTT job.
 */
static void resume_build_desc(esp_rmaker_ota_resume_desc_t *desc, const esp_rmaker_ota_data_t *ota_data)
{
    memset(desc, 0, sizeof(*desc));
    strncpy(desc->md5_hex, ota_data->file_md5, ESP_RMAKER_OTA_MD5_HEX_LEN);
    desc->md5_hex[ESP_RMAKER_OTA_MD5_HEX_LEN] = '\0';
    desc->filesize = (uint32_t)ota_data->filesize;
    desc->transport = ESP_RMAKER_OTA_TRANSPORT_MQTT;
    desc->block_size = (uint32_t)MQTT_DOWNLOADER_BLOCK_SIZE;
}

/**
 * @brief Attempt to resume the download: if the job declares a file_md5 and NVS holds a
 * matching bitmap, overlay it so already-received blocks are not re-requested. Best-effort:
 * any mismatch/absence leaves the fresh all-unprocessed bitmap (full download) and clears
 * stale state. Must be called after mqtt_downloader_bitmap_init().
 */
static void resume_attempt(void)
{
    mqtt_downloader_ctx_t *ctx = &g_mqtt_downloader_ctx;
    if (ctx->ota_data->file_md5 == NULL || ctx->ota_data->file_md5[0] == '\0') {
        OSAL_LOGD(TAG, "No MD5 in job document -> resume disabled, fresh download");
        return; /* resume disabled for this job */
    }
    ctx->resume_enabled = true;
    resume_build_desc(&ctx->resume_desc, ctx->ota_data);
    OSAL_LOGD(TAG, "Job MD5=%s filesize=%" PRIu32 " block_size=%" PRIu32,
              ctx->resume_desc.md5_hex, ctx->resume_desc.filesize, ctx->resume_desc.block_size);

    esp_rmaker_ota_resume_desc_t loaded;
    uint8_t *tracker = NULL;
    size_t tracker_len = 0;
    esp_rmaker_error_t load_err = esp_rmaker_ota_nvs_resume_load(&loaded, (void **)&tracker, &tracker_len);
    if (load_err != ESP_RMAKER_OK) {
        OSAL_LOGD(TAG, "No tracker in NVS (load=%d) -> fresh download", load_err);
        return; /* nothing persisted */
    }
    OSAL_LOGD(TAG, "NVS tracker: md5=%s filesize=%" PRIu32 " transport=%u block_size=%" PRIu32 " tracker_len=%u",
              loaded.md5_hex, loaded.filesize, (unsigned)loaded.transport, loaded.block_size, (unsigned)tracker_len);

    mqtt_downloader_bitmap_t *bitmap = &ctx->current_image_ctx.progress.bitmap;
    size_t cell_count = (bitmap->block_count + 7) / 8;
    if (!esp_rmaker_ota_nvs_resume_matches(&loaded, &ctx->resume_desc) || tracker_len != cell_count) {
        OSAL_LOGD(TAG, "Resume tracker mismatch (matches=%d, tracker_len=%u vs cell_count=%u) -> fresh download",
                  (int)esp_rmaker_ota_nvs_resume_matches(&loaded, &ctx->resume_desc), (unsigned)tracker_len, (unsigned)cell_count);
        free(tracker);
        esp_rmaker_ota_nvs_resume_clear(); /* stale or mismatched image: discard */
        return;
    }

    /* Count usefully-partial progress before committing the overlay (bit set == unprocessed).
     * Popcount per cell (cell_count bytes) rather than per block. The last cell may hold padding
     * bits beyond block_count (set to 1 by bitmap_init, never cleared); mask them out so they
     * don't inflate the count. */
    size_t unprocessed = 0;
    for (size_t c = 0; c < cell_count; c++) {
        uint8_t cell = tracker[c];
        if (c == cell_count - 1 && (bitmap->block_count % 8) != 0) {
            cell &= (uint8_t)((1u << (bitmap->block_count % 8)) - 1); /* keep only real-block bits */
        }
        unprocessed += (size_t)__builtin_popcount(cell);
    }
    if (unprocessed == 0 || unprocessed == bitmap->block_count) {
        OSAL_LOGD(TAG, "Resume tracker not usefully partial (unprocessed=%u/%u) -> fresh download",
                  (unsigned)unprocessed, (unsigned)bitmap->block_count);
        free(tracker);
        esp_rmaker_ota_nvs_resume_clear();
        return;
    }

    memcpy(bitmap->bitmap, tracker, cell_count);
    free(tracker);
    bitmap->unprocessed_block_count = unprocessed;
    ctx->resume_pending = true;
    size_t received_blocks = bitmap->block_count - unprocessed;
    uint32_t resume_offset = (uint32_t)(received_blocks * (size_t)MQTT_DOWNLOADER_BLOCK_SIZE);
    if (resume_offset > ctx->ota_data->filesize) {
        resume_offset = ctx->ota_data->filesize;
    }
    OSAL_LOGI(TAG, "Resume match -> resuming: %u/%u blocks already received",
              (unsigned)received_blocks, (unsigned)bitmap->block_count);
    osal_event_post(RMAKER_OTA_EVENT, RMAKER_OTA_EVENT_RESUMED,
                    &resume_offset, sizeof(resume_offset), OSAL_MAX_DELAY);
}

/**
 * @brief Persist the current bitmap as the resume tracker. Call only on batch boundaries
 * to bound NVS wear.
 */
static void resume_persist(void)
{
    mqtt_downloader_ctx_t *ctx = &g_mqtt_downloader_ctx;
    if (!ctx->resume_enabled) {
        return;
    }
    mqtt_downloader_bitmap_t *bitmap = &ctx->current_image_ctx.progress.bitmap;
    if (bitmap->bitmap == NULL) {
        return;
    }
    size_t cell_count = (bitmap->block_count + 7) / 8;
    esp_rmaker_error_t err = esp_rmaker_ota_nvs_resume_save(&ctx->resume_desc, bitmap->bitmap, cell_count);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGW(TAG, "Failed to persist resume tracker: %d", err);
    } else {
        OSAL_LOGD(TAG, "Persisted resume tracker: %u/%u blocks done (md5=%s)",
                  (unsigned)(bitmap->block_count - bitmap->unprocessed_block_count), (unsigned)bitmap->block_count, ctx->resume_desc.md5_hex);
    }
}
#endif /* CONFIG_RMNG_OTA_RESUME */

/* Public function definitions **********************************************/

esp_rmaker_error_t esp_rmaker_ota_mqtt_validate_image_ref(const char *image_ref, size_t image_ref_len)
{
    if (image_ref == NULL || image_ref_len == 0) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (image_ref_len > STREAM_NAME_MAX_LEN) {
        OSAL_LOGE(TAG, "MQTT image ref len %d exceeds STREAM_NAME_MAX_LEN %d", (int)image_ref_len, (int)STREAM_NAME_MAX_LEN);
        return ESP_RMAKER_INVALID_ARG;
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_ota_mqtt_cb(esp_rmaker_ota_handle_t handle, esp_rmaker_ota_data_t *ota_data, const esp_rmaker_ota_ft_ctx_t *ft_handler)
{
    esp_rmaker_error_t err;

    if (!handle || !ota_data) {
        OSAL_LOGE(TAG, "Invalid parameters");
        return ESP_RMAKER_INVALID_ARG;
    }

    if (g_mqtt_downloader_ctx.active) {
        OSAL_LOGW(TAG, "Download already in progress");
        return ESP_RMAKER_INVALID_STATE;
    }

    if (!ota_data->stream_id) {
        OSAL_LOGE(TAG, "No stream ID provided");
        return ESP_RMAKER_INVALID_ARG;
    }

    /* Defense in depth: re-validate stream id length here even though the orchestrator
     * should already have called the validator. The AWS MQTT file-downloader copies the
     * stream name into a STREAM_NAME_MAX_LEN-sized stack buffer with no bounds check. */
    if (esp_rmaker_ota_mqtt_validate_image_ref(ota_data->stream_id, strlen(ota_data->stream_id)) != ESP_RMAKER_OK) {
        return ESP_RMAKER_INVALID_ARG;
    }

    OSAL_LOGI(TAG, "Starting MQTT image download");
    OSAL_LOGI(TAG, "Stream ID: %s", ota_data->stream_id);
    OSAL_LOGI(TAG, "File size: %d bytes", ota_data->filesize);

    /* Initialize context */
    memset(&g_mqtt_downloader_ctx, 0, sizeof(mqtt_downloader_ctx_t));
    g_mqtt_downloader_ctx.ft_handler = ft_handler;
    g_mqtt_downloader_ctx.ota_data = ota_data;
    g_mqtt_downloader_ctx.active = true;
    /* A stream can carry several files; request and accept only the one the job targets. */
    g_mqtt_downloader_ctx.current_image_ctx.file_id = ota_data->file_id;

    /* Allocate the per-block scratch buffer (prefers SPIRAM). Freed in
     * cleanup_downloader, which every failure path below also reaches. */
    g_mqtt_downloader_chunk_buffer =
        (uint8_t *)OSAL_MALLOC_EXTRAM(MQTT_DOWNLOADER_BLOCK_SIZE);
    if (!g_mqtt_downloader_chunk_buffer) {
        OSAL_LOGE(TAG, "Failed to allocate %d-byte chunk buffer",
                  (int)MQTT_DOWNLOADER_BLOCK_SIZE);
        err = ESP_RMAKER_NO_MEM;
        goto esp_rmaker_ota_mqtt_cb_fail;
    }

    /* Get thing name */
    char *thing_name = NULL;
    err = esp_rmaker_credentials_get_thing_name(&thing_name);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get thing name");
        goto esp_rmaker_ota_mqtt_cb_fail;
    }

    /* Initialize AWS MQTT File Downloader context */
    MQTTFileDownloaderStatus_t aws_status = mqttDownloader_init(
            &g_mqtt_downloader_ctx.aws_ctx,
            ota_data->stream_id,
            strlen(ota_data->stream_id),
            thing_name,
            strlen(thing_name),
            MQTT_DOWNLOADER_DATA_TYPE
                                            );

    free(thing_name);

    if (aws_status != MQTTFileDownloaderSuccess) {
        OSAL_LOGE(TAG, "Failed to initialize AWS MQTT downloader: %d", aws_status);
        err = ESP_RMAKER_FAIL;
        goto esp_rmaker_ota_mqtt_cb_fail;
    }

    /* Calculate total blocks and initialize block request context */
    mqtt_downloader_block_request_ctx_t *block_request_ctx = &g_mqtt_downloader_ctx.block_request_ctx;
    uint32_t total_blocks = (ota_data->filesize + MQTT_DOWNLOADER_BLOCK_SIZE - 1) / MQTT_DOWNLOADER_BLOCK_SIZE;
    uint32_t max_retries = (total_blocks / MQTT_DOWNLOADER_MAX_RETRIES_DIVIDE_FACTOR);
    err = block_request_init(block_request_ctx, max_retries);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize block request context");
        goto esp_rmaker_ota_mqtt_cb_fail;
    }
    OSAL_LOGI(TAG, "Total blocks to download: %" PRIu32 ", max retries: %" PRIu32, total_blocks, max_retries);

    /* Initialize bitmap */
    err = mqtt_downloader_bitmap_init(total_blocks, &g_mqtt_downloader_ctx.current_image_ctx.progress.bitmap);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize bitmap");
        goto esp_rmaker_ota_mqtt_cb_fail;
    }

#if CONFIG_RMNG_OTA_RESUME
    /* Best-effort: overlay any persisted progress so received blocks are not re-requested. */
    resume_attempt();
#endif

    /* Initialize progress tracking context */
    if (image_progress_init(&g_mqtt_downloader_ctx.current_image_ctx.progress.tracking_ctx, ota_data->filesize) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initialize progress tracking context");
        goto esp_rmaker_ota_mqtt_cb_fail;
    }

#if CONFIG_RMNG_OTA_RESUME
    /* On resume, account already-received blocks so progress reflects true completion
     * instead of restarting from 0%. */
    if (g_mqtt_downloader_ctx.resume_pending) {
        mqtt_downloader_bitmap_t *bm = &g_mqtt_downloader_ctx.current_image_ctx.progress.bitmap;
        size_t received_blocks = bm->block_count - bm->unprocessed_block_count;
        size_t received_bytes = received_blocks * (size_t)MQTT_DOWNLOADER_BLOCK_SIZE;
        if (received_bytes > (size_t)ota_data->filesize) {
            received_bytes = (size_t)ota_data->filesize;
        }
        image_progress_seed(&g_mqtt_downloader_ctx.current_image_ctx.progress.tracking_ctx, (uint32_t)received_bytes);
    }
#endif

    /* Register MQTT event handlers BEFORE the first subscribe call so that a
     * SUBACK-failure or reconnect during init is caught by the retry context. */
    err = register_mqtt_event_handlers();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to register MQTT event handlers");
        goto esp_rmaker_ota_mqtt_cb_fail;
    }

    /* Subscribe to MQTT topics */
    err = subscribe_to_stream_topics();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to subscribe to MQTT topics");
        report_and_cleanup(IMAGE_HANDLER_STATUS_FAILED_STREAM_SUBSCRIPTION);
        return err;
    }

    /* Start download by requesting first block */
    err = block_request_send_next();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to start download");
        goto esp_rmaker_ota_mqtt_cb_fail;
    }

    return ESP_RMAKER_OK;

esp_rmaker_ota_mqtt_cb_fail:
    cleanup_downloader();
    return err;
}
