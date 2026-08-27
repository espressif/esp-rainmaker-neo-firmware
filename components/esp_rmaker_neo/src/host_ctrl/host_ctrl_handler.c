/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file host_ctrl_handler.c
 * @brief Host control handler.
 */

/* Includes ****************************************************************/

/* External I/O common includes */
#include "osal_ext_io.h"

/* Standard includes */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>

/* Constants */
#include "esp_rmaker_host_ctrl_constants.h"
#include "esp_rmaker_host_ctrl.h"
#include "sysinfo.h"
#include "constants/network.h"
#include "sdkconfig.h"
#include "esp_rmaker_standard_services.h"

/* Platform common includes */
#include "osal_log.h"
#include "osal_event_group.h"
#include "osal_time.h"

/* Heap monitor common includes */
#include "osal_heap_monitor.h"

/* RMNG includes */
#include "host_ctrl_processing.h"
#include "data_model/dm_handlers.h"
#include "esp_rmaker_flow.h"
#include "core_internal.h"
#include "node_internal.h"
#include "esp_rmaker_val.h"
#include "local_config.h"
#include "util/esp_rmaker_crypto.h"
#include "util/esp_rmaker_convert_hex.h"
#include "network/shadows.h"
#include "network/mqtt_channels.h"
#include "network/mqtt_control.h"
#include "network/cloud/manager.h"

/* Event flags includes */
#include "event_flags.h"

/* Time control includes */
#include "osal_timesync.h"
#include "osal_time_control.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
#include "bridge_handlers.h"
#endif

/* Pre-processor definitions **************************************************/

/* Upper bound for the wait timeout in milliseconds */
#define RMAKER_HOST_CTRL_WAIT_TIMEOUT_MS_MAX 120000

/* Private variables *********************************************************/

/**
 * @brief The tag for logging.
 */
static const char *TAG = "rmng_hc_handler";

/* Private function declarations *********************************************/

/**
 * @brief Reset the RainMaker Neo node.
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
extern esp_rmaker_error_t __esp_rmaker_host_ctrl_reset(void);
extern esp_rmaker_error_t __esp_rmaker_host_ctrl_reset_keep_nvs(void);

/* --- Handlers --- */

/**
 * @brief Handle the ping command.
 * @note The buffer is in the format: "<random char * RMAKER_HOST_CTRL_VAL_PING_LENGTH>|".
 * @note The response is in the format: "<same random characters>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_ping(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Handle the start command.
 * @note The buffer is in the format: "<timeout in milliseconds>|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_start(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Handle the stop command.
 * @note The buffer is in the format: "<timeout in milliseconds>|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_stop(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Handle the reset command.
 */
static void __handle_reset(void);

/**
 * @brief Handle the kill command.
 */
static void __handle_kill(void);

/**
 * @brief Adds a tag to the node model. No update is performed.
 * @note The buffer is in the format: "<tag_name>|<tag_value>|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_add_tag(uint8_t *buffer, size_t buffer_length);

/**
 * @note this command is not supported yet.
 * @brief Removes a tag from the node model. No update is performed.
 * @note The buffer is in the format: "<tag_name>|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_remove_tag(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Updates a tag in the node model.
 * @note The buffer is in the format: "<tag_name>|<tag_value>|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_update_tag(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Updates the timezone, in IANA format.
 * @note The buffer is in the format: "<timezone>|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_update_timezone(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Updates the schedule version.
 * @note The buffer is in the format: "<param_data_type><schedule_version>|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_update_local_config_sched_ver(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Updates the trigger version.
 * @note The buffer is in the format: "<param_data_type><trigger_version>|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_update_local_config_trigger_ver(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Sets local control HTTP port (before local_ctrl service enable).
 */
static void __handle_update_local_config_local_ctrl_http_port(uint8_t *buffer, size_t buffer_length);
static void __handle_update_local_config_local_ctrl_pop(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Waits for event flags to be set.
 * @note The buffer is in the format: "<flag><flag>...|<timeout in milliseconds>|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_wait_flags(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Clears event flags.
 * @note The buffer is in the format: "<flag><flag>...|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_clear_flags(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the current time as a UNIX timestamp.
 * @note The buffer is not used.
 * @note The response is in the format: "<current_time>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_current_time(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the current time as milliseconds since the Unix epoch.
 * @note Uses osal_get_time_ms (same clock as latency recv_ts).
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_current_time_ms(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the current timezone, in IANA format.
 * @note The buffer is not used.
 * @note The response is in the format: "<current_timezone>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_current_timezone(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the thing name from the node model.
 * @note The buffer is not used.
 * @note The response is in the format: "<thing_name>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_thing_name(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the signature for a challenge.
 * @note The buffer is in the format: "<challenge>".
 * @note The response is in the format: "<signature>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_signature(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the indexed shadow from the cloud.
 * @note We have to do it from the firmware because only the node has permission to retrieve the indexed shadow.
 * @note The buffer is not used.
 * @note The response is in the format: "<indexed_shadow, JSON payload>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_indexed_shadow(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the named shadow from the cloud.
 * @note We have to do it from the firmware because only the node has permission to retrieve the named shadow without group information, i.e., "params-".
 * @note The buffer is not used.
 * @note The response is in the format: "<named_shadow, JSON payload>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_named_shadow(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the tag value from the node model.
 * @note The buffer is in the format: "<tag_name>|".
 * @note The response is in the format: "<tag_value>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_tag_value(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the current group info set.
 * @note The buffer is not used.
 * @note The response is in the format: "<group_info>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_group_info(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the current Alexa enabled state.
 * @note The buffer is not used.
 * @note The response is in the format: "<alexa_enabled>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_alexa_enabled(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the current GVA enabled state.
 * @note The buffer is not used.
 * @note The response is in the format: "<gva_enabled>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_gva_enabled(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the current SmartThings enabled state.
 * @note The buffer is not used.
 * @note The response is in the format: "<st_enabled>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_st_enabled(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the current trigger version.
 * @note The buffer is not used.
 * @note The response is in the format: "<trigger_version>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_trigger_version(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the current schedule version.
 * @note The buffer is not used.
 * @note The response is in the format: "<schedule_version>".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_sched_version(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Gets the current heap status.
 * @note The buffer is not used.
 * @note The response is in the format: "<total_size>|<allocated_size>|<free_size>|<largest_block_size>|<lowest_free_size>", all in bytes.
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_get_heap_status(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Sets the time manually.
 * @note The buffer is in the format: "<UNIX timestamp>|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_time_control_set_time(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Advances the time manually.
 * @note The buffer is in the format: "<number of seconds>|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_time_control_advance_time(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Forces all network operations (connect, send, recv) to fail for testing purposes.
 * @note The buffer is not used.
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_mqtt_control_force_network_failure(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Restores default network operations settings.
 * @note The buffer is not used.
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_mqtt_control_restore_network_default(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Forces all MQTT operations (publish, subscribe, unsubscribe) to fail.
 * @note The buffer is not used.
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_mqtt_control_force_operations_failure(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Restores default MQTT operations (publish, subscribe, unsubscribe) default settings.
 * @note The buffer is not used.
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_mqtt_control_restore_operations_default(uint8_t *buffer, size_t buffer_length);

/**
 * @brief Sends the cloud events.
 * @note The buffer is in the format: "<cloud_event_1><cloud_event_2>...|".
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 */
static void __handle_cloud_control_send_cloud_events(uint8_t *buffer, size_t buffer_length);

/* --- Processing --- */

/**
 * @brief Get the tag pair pointers from the buffer.
 * @param[in] tag_pair_start The start pointer of the tag pair.
 * @param[in] tag_pair_length The length of the tag pair.
 * @param[out] p_tag_name The pointer to the tag name.
 * @param[out] p_tag_value The pointer to the tag value.
 * @return True if the tag pair pointers are valid, false otherwise.
 */
static bool __get_tag_pair_pointers(uint8_t *tag_pair_start, size_t tag_pair_length, char **p_tag_name, char **p_tag_value);

/**
 * @brief Get the event flags from the buffer.
 * @param[in] event_flags_start The start pointer of the event flags.
 * @param[in] event_flags_end The end pointer of the event flags.
 * @return The event flags.
 */
static osal_event_group_bits_t __get_event_flags(char *event_flags_start, char *event_flags_end);

/* Private function definitions **********************************************/

/* --- Handlers --- */

static void __handle_ping(uint8_t *buffer, size_t buffer_length)
{
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for ping");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    // Check for 8 character length
    char *ping_chars = (char *) buffer;
    if (delimiters[0] - ping_chars != RMAKER_HOST_CTRL_VAL_PING_LENGTH) {
        OSAL_LOGE(TAG, "Invalid random characters length for ping: expected %d", RMAKER_HOST_CTRL_VAL_PING_LENGTH);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    // Send response
    OSAL_LOGD(TAG, "Ping command received: %s", ping_chars);
    char *target_type = esp_rmaker_host_ctrl_get_target_type();
    char payload[RMAKER_HOST_CTRL_VAL_PING_LENGTH + strlen(target_type) + 2];
    size_t payload_length = snprintf(payload, sizeof(payload), "%s%c%s", ping_chars, RMAKER_HOST_CTRL_DELIMITER_CHAR, target_type);
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, payload, payload_length);
}

static void __handle_start(uint8_t *buffer, size_t buffer_length)
{
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for start");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    int timeout_ms = atoi((char *) buffer);
    if (timeout_ms > RMAKER_HOST_CTRL_WAIT_TIMEOUT_MS_MAX) {
        OSAL_LOGE(TAG, "Timeout too long: %d", timeout_ms);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    esp_rmaker_error_t err = esp_rmaker_start();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to start RMNG");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    err = esp_rmaker_event_flags_wait(ESP_RMAKER_EVENT_FLAGS_STARTED, timeout_ms);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to wait for RMNG to start. RMNG may not have started.");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_TIMEOUT);
        return;
    }

    OSAL_LOGI(TAG, "RMNG started");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_stop(uint8_t *buffer, size_t buffer_length)
{
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for stop");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    int timeout_ms = atoi((char *) buffer);
    if (timeout_ms > RMAKER_HOST_CTRL_WAIT_TIMEOUT_MS_MAX) {
        OSAL_LOGE(TAG, "Timeout too long: %d", timeout_ms);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    esp_rmaker_error_t err = esp_rmaker_stop();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to initiate stop of RMNG");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    err = esp_rmaker_event_flags_wait(ESP_RMAKER_EVENT_FLAGS_STOPPED, timeout_ms);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to wait for RMNG to stop. RMNG may not have stopped.");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_TIMEOUT);
        return;
    }

    OSAL_LOGI(TAG, "RMNG stopped");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_reset(void)
{
    esp_rmaker_host_ctrl_data_model_on_reset();

    esp_rmaker_error_t err = __esp_rmaker_host_ctrl_reset();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to reset RMNG");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    OSAL_LOGI(TAG, "RMNG reset");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_reset_keep_nvs(void)
{
    /* Cold-reboot simulation. Does NOT run the data-model adapter's
     * on_reset (that would wipe the local device tree); just performs a
     * full node deinit + reinit with NVS untouched. */
    esp_rmaker_error_t err = __esp_rmaker_host_ctrl_reset_keep_nvs();
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to reset RMNG (keep_nvs)");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    OSAL_LOGI(TAG, "RMNG reset (NVS preserved)");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_kill(void)
{
    OSAL_LOGI(TAG, "RMNG kill command received");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);

    esp_rmaker_host_ctrl_kill();
}

static void __handle_add_tag(uint8_t *buffer, size_t buffer_length)
{
    /* Get node */
    const esp_rmaker_node_t *node = esp_rmaker_get_node();

    /* Add tag */
    char *tag_name, *tag_value;
    if (!__get_tag_pair_pointers(buffer, buffer_length, &tag_name, &tag_value)) {
        OSAL_LOGE(TAG, "Invalid tag pair");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    esp_rmaker_error_t err = esp_rmaker_node_add_tag(node, tag_name, tag_value);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to add tag: %s", tag_name);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    // Send response
    OSAL_LOGI(TAG, "Tag added: %s: %s", tag_name, tag_value);
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_remove_tag(uint8_t *buffer, size_t buffer_length)
{
    /* There is no way to remove a tag from the node model */
    OSAL_LOGE(TAG, "Remove tag command is not supported yet");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
}

static void __handle_update_tag(uint8_t *buffer, size_t buffer_length)
{
    /* Get node */
    const esp_rmaker_node_t *node = esp_rmaker_get_node();

    /* Update tag */
    char *tag_name, *tag_value;
    if (!__get_tag_pair_pointers(buffer, buffer_length, &tag_name, &tag_value)) {
        OSAL_LOGE(TAG, "Invalid tag pair");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    esp_rmaker_error_t err = esp_rmaker_node_update_tag(node, tag_name, tag_value);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update tag: %s", tag_name);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    // Send response
    OSAL_LOGI(TAG, "Tag updated: %s: %s", tag_name, tag_value);
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_update_timezone(uint8_t *buffer, size_t buffer_length)
{
    /* Get delimiters */
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for update timezone");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Update timezone */
    const char *timezone = (char *) buffer;
    esp_rmaker_error_t err = osal_timesync_set_timezone(timezone);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update timezone: %s", timezone);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    // Send response
    OSAL_LOGI(TAG, "Timezone updated: %s", timezone);
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_update_local_config_sched_ver(uint8_t *buffer, size_t buffer_length)
{
    /* Get delimiters */
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for update local config sched ver");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Get version value */
    esp_rmaker_param_val_t version_value;
    if (!esp_rmaker_host_ctrl_get_param_value((char *) buffer, delimiters[0], &version_value)) {
        OSAL_LOGE(TAG, "Invalid version value");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    if (version_value.type != RMAKER_VAL_TYPE_INTEGER) {
        OSAL_LOGE(TAG, "Invalid version value type: %d", version_value.type);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_TYPE_MISMATCH);
        return;
    }

    /* Update schedule version */
    esp_rmaker_error_t err = esp_rmaker_local_config_set_sched_ver(version_value.val.i);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update schedule version: %d", version_value.val.i);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    // Send response
    OSAL_LOGI(TAG, "Schedule version updated: %d", version_value.val.i);
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_update_local_config_trigger_ver(uint8_t *buffer, size_t buffer_length)
{
    /* Get delimiters */
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for update local config trigger ver");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Get version value */
    esp_rmaker_param_val_t version_value;
    if (!esp_rmaker_host_ctrl_get_param_value((char *) buffer, delimiters[0], &version_value)) {
        OSAL_LOGE(TAG, "Invalid version value");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    if (version_value.type != RMAKER_VAL_TYPE_INTEGER) {
        OSAL_LOGE(TAG, "Invalid version value type: %d", version_value.type);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_TYPE_MISMATCH);
        return;
    }

    /* Update trigger version */
    esp_rmaker_error_t err = esp_rmaker_local_config_set_trigger_ver(version_value.val.i);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to update trigger version: %d", version_value.val.i);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    // Send response
    OSAL_LOGI(TAG, "Trigger version updated: %d", version_value.val.i);
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_update_local_config_local_ctrl_http_port(uint8_t *buffer, size_t buffer_length)
{
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for update local control HTTP port");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    esp_rmaker_param_val_t port_value;
    if (!esp_rmaker_host_ctrl_get_param_value((char *) buffer, delimiters[0], &port_value)) {
        OSAL_LOGE(TAG, "Invalid local control HTTP port value");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    if (port_value.type != RMAKER_VAL_TYPE_INTEGER) {
        OSAL_LOGE(TAG, "Invalid local control HTTP port type: %d", port_value.type);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_TYPE_MISMATCH);
        return;
    }

    esp_rmaker_error_t err = esp_rmaker_local_ctrl_set_http_port_from_host_ctrl(port_value.val.i);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to set local control HTTP port: %d", port_value.val.i);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    OSAL_LOGI(TAG, "Local control HTTP port set to: %d", port_value.val.i);
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

/* Set the PoP the local endpoints service will use, before it is enabled.
 *
 * A device in the field carries a PoP from manufacturing data (printed on it), which the
 * client already has. Tests have no such channel to the generated-and-stored-in-NVS PoP,
 * so this lets the harness pin a known one and exercise the same PoP-backed security
 * schemes a real device uses. */
static void __handle_update_local_config_local_ctrl_pop(uint8_t *buffer, size_t buffer_length)
{
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for update local control PoP");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    esp_rmaker_param_val_t pop_value;
    if (!esp_rmaker_host_ctrl_get_param_value((char *) buffer, delimiters[0], &pop_value)) {
        OSAL_LOGE(TAG, "Invalid local control PoP value");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    if (pop_value.type != RMAKER_VAL_TYPE_STRING) {
        OSAL_LOGE(TAG, "Invalid local control PoP type: %d", pop_value.type);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_TYPE_MISMATCH);
        return;
    }

    if (esp_rmaker_local_ctrl_set_pop(pop_value.val.s) != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to set local control PoP");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    OSAL_LOGI(TAG, "Local control PoP set");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_wait_flags(uint8_t *buffer, size_t buffer_length)
{
    /* Get delimiters */
    char *delimiters[2];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 2)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 2 for wait");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    osal_event_group_bits_t event_flags = __get_event_flags((char *) buffer, delimiters[0]);
    if (event_flags == 0) {
        OSAL_LOGE(TAG, "Invalid event flags");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Wait for event flags */
    int timeout_ms = atoi(delimiters[0] + 1);
    if (timeout_ms > RMAKER_HOST_CTRL_WAIT_TIMEOUT_MS_MAX) {
        OSAL_LOGE(TAG, "Timeout too long: %d", timeout_ms);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    OSAL_LOGI(TAG, "Waiting for event flags: 0x%" PRIx32 " for %d ms", (uint32_t) event_flags, timeout_ms);

    esp_rmaker_error_t err = esp_rmaker_event_flags_wait(event_flags, timeout_ms);
    if (err == ESP_RMAKER_TIMEOUT) {
        OSAL_LOGE(TAG, "Timed out waiting for event flags");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_TIMEOUT);
        return;
    } else if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to wait for event flags");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    // Send response
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_clear_flags(uint8_t *buffer, size_t buffer_length)
{
    /* Get delimiters */
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for clear flags");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Clear flags */
    osal_event_group_bits_t event_flags = __get_event_flags((char *) buffer, delimiters[0]);
    if (event_flags == 0) {
        OSAL_LOGE(TAG, "Invalid event flags");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    esp_rmaker_event_flags_clear(event_flags);

    // Send response
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_get_current_time(uint8_t *buffer, size_t buffer_length)
{
    time_t current_time = osal_get_time(NULL);
    char payload[20];
    snprintf(payload, sizeof(payload), "%ld", (long) current_time);
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, payload, strlen(payload));
}

static void __handle_get_current_time_ms(uint8_t *buffer, size_t buffer_length)
{
    (void) buffer;
    (void) buffer_length;
    uint64_t current_time_ms = osal_get_time_ms(NULL);
    char payload[32];
    snprintf(payload, sizeof(payload), "%" PRIu64, (uint64_t) current_time_ms);
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, payload, strlen(payload));
}

static void __handle_get_current_timezone(uint8_t *buffer, size_t buffer_length)
{
    /* Get current timezone */
    char *timezone = osal_timesync_get_timezone();
    if (timezone == NULL) {
        OSAL_LOGE(TAG, "Failed to get current timezone");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    /* Send response */
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, timezone, strlen(timezone));
    free(timezone);
}

static void __handle_get_thing_name(uint8_t *buffer, size_t buffer_length)
{
    /* Get thing name */
    char *thing_name = NULL;
    esp_rmaker_error_t err = esp_rmaker_credentials_get_thing_name(&thing_name);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get thing name");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    /* Send response */
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, thing_name, strlen(thing_name));
    free(thing_name);
}

static void __handle_get_signature(uint8_t *buffer, size_t buffer_length)
{
    /* Get delimiters */
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for get signature");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    esp_rmaker_error_t err;
    /* Get signature */
    uint8_t *signature = NULL;
    size_t sig_len = 0;
    OSAL_LOGI(TAG, "signing challenge: %s", (char *) buffer);
    err = esp_rmaker_core_sign_challenge(buffer, buffer_length - 1, &signature, &sig_len);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to get signature");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    size_t payload_length = sig_len * 2;
    char payload[payload_length + 1];
    err = esp_rmaker_convert_bytes_to_hex(signature, sig_len, payload, payload_length + 1);
    free(signature);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to convert signature to hex");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    /* Send response */
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, payload, payload_length);
}

static void __handle_get_indexed_shadow(uint8_t *buffer, size_t buffer_length)
{
    /* Get indexed shadow */
    char *indexed_shadow = esp_rmaker_indexed_shadow_get_reported(RMAKER_NETWORK_SHADOWS_GET_TIMEOUT_MS);
    if (indexed_shadow == NULL) {
        OSAL_LOGE(TAG, "Failed to get indexed shadow");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    /* Send response */
    OSAL_LOGI(TAG, "Indexed shadow: %s, length: %d", indexed_shadow, (int)strlen(indexed_shadow));
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, indexed_shadow, strlen(indexed_shadow));

    free(indexed_shadow);
}

static void __handle_get_named_shadow(uint8_t *buffer, size_t buffer_length)
{
    /* Get named shadow */
    char *named_shadow = esp_rmaker_named_shadow_get_reported(RMAKER_NETWORK_SHADOWS_GET_TIMEOUT_MS);
    if (named_shadow == NULL) {
        OSAL_LOGE(TAG, "Failed to get named shadow");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    /* Send response */
    OSAL_LOGI(TAG, "Named shadow: %s, length: %d", named_shadow, (int)strlen(named_shadow));
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, named_shadow, strlen(named_shadow));

    free(named_shadow);
}

static void __handle_get_tag_value(uint8_t *buffer, size_t buffer_length)
{
    /* Get delimiters */
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for get tag value");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Get node */
    const esp_rmaker_node_t *node = esp_rmaker_get_node();

    /* Get tag name */
    char *tag_name = (char *) buffer;
    esp_rmaker_tag_t *tag = esp_rmaker_node_get_tag_by_name(node, tag_name);
    if (tag == NULL) {
        OSAL_LOGE(TAG, "Tag not found: %s", tag_name);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return;
    }

    /* Get tag value */
    esp_rmaker_tag_t *tag_value = esp_rmaker_node_get_tag_by_name(node, tag_name);
    if (tag_value == NULL) {
        OSAL_LOGE(TAG, "Failed to get tag value: %s", tag_name);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND);
        return;
    }

    /* Send response */
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, tag->value, strlen(tag->value));
}

static void __handle_get_group_info(uint8_t *buffer, size_t buffer_length)
{
    char *group_info_str = esp_rmaker_local_config_get_group_info_str();
    if (group_info_str == NULL) {
        // Send empty string
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
        return;
    }

    size_t group_info_str_len = strlen(group_info_str);
    if (group_info_str_len == 0) {
        // Send empty string
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
        free(group_info_str);
        return;
    }

    // Send group info string
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, group_info_str, group_info_str_len);
    free(group_info_str);
}

static void __handle_get_alexa_enabled(uint8_t *buffer, size_t buffer_length)
{
    bool alexa_enabled = esp_rmaker_local_config_get_alexa_en();
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, alexa_enabled ? "1" : "0", 1);
}

static void __handle_get_gva_enabled(uint8_t *buffer, size_t buffer_length)
{
    bool gva_enabled = esp_rmaker_local_config_get_gva_en();
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, gva_enabled ? "1" : "0", 1);
}

static void __handle_get_st_enabled(uint8_t *buffer, size_t buffer_length)
{
    bool st_enabled = esp_rmaker_local_config_get_st_en();
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, st_enabled ? "1" : "0", 1);
}

static void __handle_get_sched_version(uint8_t *buffer, size_t buffer_length)
{
    int sched_version = esp_rmaker_local_config_get_sched_ver();
    char payload[15];
    snprintf(payload, sizeof(payload), "%d", sched_version);
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, payload, strlen(payload));
}

static void __handle_get_heap_status(uint8_t *buffer, size_t buffer_length)
{
    osal_heap_monitor_common_status_t status;
    if (!osal_heap_monitor_common_get_status(&status)) {
        OSAL_LOGE(TAG, "Failed to get heap status");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    char payload[55]; // 10 digits for each size + 4 delimiters + 1 null terminator (we don't expect to see any value above ~10GB)
    snprintf(payload, sizeof(payload), "%" PRIu32 "|%" PRIu32 "|%" PRIu32 "|%" PRIu32 "|%" PRIu32, (uint32_t)status.total_size, (uint32_t)status.allocated_size, (uint32_t)status.free_size, (uint32_t)status.largest_block_size, (uint32_t)status.lowest_free_size);
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, payload, strlen(payload));
}

static void __handle_get_trigger_version(uint8_t *buffer, size_t buffer_length)
{
    int trigger_version = esp_rmaker_local_config_get_trigger_ver();
    char payload[15];
    snprintf(payload, sizeof(payload), "%d", trigger_version);
    esp_rmaker_host_ctrl_send_response_with_payload(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK, payload, strlen(payload));
}

static void __handle_time_control_set_time(uint8_t *buffer, size_t buffer_length)
{
    /* Get delimiters */
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for time control set time");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Get time */
    const char *time_str = (const char *) buffer;

    int time = atoi(time_str);
    if (time <= 0) {
        OSAL_LOGE(TAG, "Invalid time: %s", time_str);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    if (!osal_time_control_set_time(time)) {
        OSAL_LOGE(TAG, "Failed to set time: %s", time_str);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    osal_timesync_print_current_time();
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_time_control_advance_time(uint8_t *buffer, size_t buffer_length)
{
    /* Get delimiters */
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for time control advance time");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Get seconds */
    const char *seconds_str = (const char *) buffer;

    int seconds = atoi(seconds_str);
    if (seconds == 0) {
        OSAL_LOGE(TAG, "Invalid seconds: %s", seconds_str);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
    if (!osal_time_control_advance_time(seconds)) {
        OSAL_LOGE(TAG, "Failed to advance time: %s", seconds_str);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    OSAL_LOGI(TAG, "Advanced time by %s seconds", seconds_str);
    osal_timesync_print_current_time();
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_mqtt_control_force_network_failure(uint8_t *buffer, size_t buffer_length)
{
    /* Force all network operations (connect, send, recv) to fail */
    if (network_mqtt_control_force_network_failure() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to force all network operations to fail");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    OSAL_LOGI(TAG, "Forced all network operations to fail");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_mqtt_control_restore_network_default(uint8_t *buffer, size_t buffer_length)
{
    /* Restore default network operations settings */
    if (network_mqtt_control_restore_network_default() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to restore default network operations settings");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    OSAL_LOGI(TAG, "Restored default network operations settings");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_mqtt_control_force_operations_failure(uint8_t *buffer, size_t buffer_length)
{
    /* Force all MQTT operations (publish, subscribe, unsubscribe) to fail */
    if (network_mqtt_control_force_operations_failure() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to force all MQTT operations to fail");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    OSAL_LOGI(TAG, "Forced all MQTT operations to fail");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_mqtt_control_restore_operations_default(uint8_t *buffer, size_t buffer_length)
{
    /* Restore default MQTT operations (publish, subscribe, unsubscribe) settings */
    if (network_mqtt_control_restore_operations_default() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to restore default MQTT settings");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    OSAL_LOGI(TAG, "Restored default MQTT settings");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_mqtt_control_disconnect(uint8_t *buffer, size_t buffer_length)
{
    if (network_mqtt_control_disconnect() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to disconnect MQTT");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }
    OSAL_LOGI(TAG, "MQTT disconnected");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_mqtt_control_connect(uint8_t *buffer, size_t buffer_length)
{
    if (network_mqtt_control_connect() != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to connect MQTT");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }
    OSAL_LOGI(TAG, "MQTT connected");
    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

static void __handle_cloud_control_send_cloud_events(uint8_t *buffer, size_t buffer_length)
{
    /* Get delimiters */
    char *delimiters[1];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(buffer, buffer_length, delimiters, 1)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 1 for cloud control send cloud events");
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Make event buffer */
    char *start = (char *) buffer, *end = delimiters[0];
    size_t event_buffer_size = end - start;
    esp_rmaker_cloud_event_t event_buffer[event_buffer_size];
    memset(event_buffer, 0, event_buffer_size * sizeof(esp_rmaker_cloud_event_t));

    /* Send cloud events */
    int event_count = 0;
    for (char *p = start; p < end; p++) {
        switch (*p) {
        case RMAKER_HOST_CTRL_CLOUD_CONTROL_CHAR_EVENT_getSchedVer:
            esp_rmaker_cloud_event_getSchedVer(&event_buffer[0]);
            break;
        case RMAKER_HOST_CTRL_CLOUD_CONTROL_CHAR_EVENT_getTriggerVer:
            esp_rmaker_cloud_event_getTriggerVer(&event_buffer[0]);
            break;
        default:
            OSAL_LOGE(TAG, "Invalid cloud event type: %c", *p);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
            return;
        }
        event_count++;
    }

    /* Send events */
    esp_rmaker_error_t err = esp_rmaker_cloud_manager_send(&esp_rmaker_topic_ctx_self, event_buffer, event_count, MQTT_CHANNEL_SUB_CLOUD_MANAGER_SEND);
    if (err != ESP_RMAKER_OK) {
        OSAL_LOGE(TAG, "Failed to send cloud events: %d", err);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR);
        return;
    }

    esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_OK);
}

/* --- Processing --- */

static bool __get_tag_pair_pointers(uint8_t *tag_pair_start, size_t tag_pair_length, char **p_tag_name, char **p_tag_value)
{
    if (tag_pair_length <= 1) {
        OSAL_LOGE(TAG, "Invalid tag pair length: %d", (int)tag_pair_length);
        return false;
    }

    char *delimiters[2];
    if (!esp_rmaker_host_ctrl_find_and_nullify_delimiters(tag_pair_start, tag_pair_length, delimiters, 2)) {
        OSAL_LOGE(TAG, "Invalid delimiters count: expected 2 for tag pair");
        return false;
    }

    char *tag_name_start = (char *) tag_pair_start, *tag_name_end = delimiters[0];
    char *tag_value_start = delimiters[0] + 1, *tag_value_end = delimiters[1];

    if (tag_name_end - tag_name_start < 1) {
        OSAL_LOGE(TAG, "tag name is empty");
        return false;
    }

    if (tag_value_end - tag_value_start < 1) {
        OSAL_LOGE(TAG, "tag value is empty");
        return false;
    }

    *p_tag_name = tag_name_start;
    *p_tag_value = tag_value_start;

    return true;
}

static osal_event_group_bits_t __get_event_flags(char *event_flags_start, char *event_flags_end)
{
    /* Wait for event flags */
    osal_event_group_bits_t event_flags = 0;
    char *c = event_flags_start, *end = event_flags_end;
    while (c < end) {
        switch (*c) {
        case RMAKER_HOST_CTRL_FLAG_CHAR_ONLINE:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_ONLINE;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_STARTED:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_STARTED;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_STOPPED:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_STOPPED;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_STATE_REPORTED:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_STATE_REPORTED;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_TIMESERIES_REPORTED:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_TIMESERIES_REPORTED;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_NODE_CONFIG_SENT:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_NODE_CONFIG_SENT;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_NOTIFICATION_SENT:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_NOTIFICATION_SENT;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_STATE_STARTED_LISTENING:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_STATE_STARTED_LISTENING;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_GROUP_INFO:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_GROUP_INFO_RECEIVED;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_ALEXA_ENABLED:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_ALEXA_ENABLED_RECEIVED;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_GVA_ENABLED:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_GVA_ENABLED_RECEIVED;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_ST_ENABLED:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_ST_ENABLED_RECEIVED;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_SCHED_VERSION:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_SCHED_VERSION_RECEIVED;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_SCHED_DETAILS:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_SCHED_DETAILS_RECEIVED;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_TRIGGER_VERSION:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_TRIGGER_VERSION_RECEIVED;
            break;
        case RMAKER_HOST_CTRL_FLAG_CHAR_TRIGGER_DETAILS:
            event_flags |= ESP_RMAKER_EVENT_FLAGS_TRIGGER_DETAILS_RECEIVED;
            break;
        default:
            OSAL_LOGE(TAG, "Invalid waitable character encountered: %c", *c);
            return 0;
        }
        c++;
    }

    return event_flags;
}
/* --- Handler delegation --- */

void __esp_rmaker_host_ctrl_handle_buffer(uint8_t *buffer, size_t buffer_length)
{
    OSAL_LOGD(TAG, "Received buffer command: %.*s", (int)buffer_length, (char *)buffer);

    /* Attempt to handle the buffer as a data model command */
    if (esp_rmaker_host_ctrl_data_model_handle_buffer(buffer, buffer_length)) {
        return;
    }

    char command = buffer[0];

    /* Handle single character commands here */
    switch (command) {
    case RMAKER_HOST_CTRL_COMMAND_CHAR_RESET:
        __handle_reset();
        return;
    case RMAKER_HOST_CTRL_COMMAND_CHAR_RESET_KEEP_NVS:
        __handle_reset_keep_nvs();
        return;
    case RMAKER_HOST_CTRL_COMMAND_CHAR_KILL:
        __handle_kill();
        return;
    }

    /* Handle payload commands here */
    if (buffer_length <= 1) {
        OSAL_LOGE(TAG, "Invalid buffer length: %d for command: %c", (int) buffer_length, command);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    esp_rmaker_host_ctrl_payload_handler_t handler = NULL;
    uint8_t *payload = buffer + 1;
    size_t payload_length = buffer_length - 1;
    char payload_type;
    switch (command) {
    case RMAKER_HOST_CTRL_COMMAND_CHAR_START:
        handler = __handle_start;
        break;
    case RMAKER_HOST_CTRL_COMMAND_CHAR_STOP:
        handler = __handle_stop;
        break;
    case RMAKER_HOST_CTRL_COMMAND_CHAR_PING:
        handler = __handle_ping;
        break;
    case RMAKER_HOST_CTRL_COMMAND_CHAR_ADD:
        payload_type = payload[0];
        payload++; payload_length--;
        if (payload_type == RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_TAG) {
            handler = __handle_add_tag;
        } else {
            OSAL_LOGE(TAG, "Invalid payload type: %c", payload_type);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
            return;
        }
        break;

    case RMAKER_HOST_CTRL_COMMAND_CHAR_REMOVE:
        payload_type = payload[0];
        payload++; payload_length--;
        if (payload[0] == RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_TAG) {
            handler = __handle_remove_tag;
        } else {
            OSAL_LOGE(TAG, "Invalid payload type: %c", payload_type);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
            return;
        }
        break;

    case RMAKER_HOST_CTRL_COMMAND_CHAR_UPDATE:
        payload_type = payload[0];
        payload++; payload_length--;
        if (payload_type == RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_TAG) {
            handler = __handle_update_tag;
        } else if (payload_type == RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_TIMEZONE) {
            handler = __handle_update_timezone;
        } else if (payload_type == RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_LOCAL_CONFIG) {
            payload_type = payload[0];
            payload++; payload_length--;
            if (payload_type == RMAKER_HOST_CTRL_LOCAL_CONFIG_CHAR_SCHED_VER) {
                handler = __handle_update_local_config_sched_ver;
            } else if (payload_type == RMAKER_HOST_CTRL_LOCAL_CONFIG_CHAR_TRIGGER_VER) {
                handler = __handle_update_local_config_trigger_ver;
            } else if (payload_type == RMAKER_HOST_CTRL_LOCAL_CONFIG_CHAR_LOCAL_CTRL_HTTP_PORT) {
                handler = __handle_update_local_config_local_ctrl_http_port;
            } else if (payload_type == RMAKER_HOST_CTRL_LOCAL_CONFIG_CHAR_LOCAL_CTRL_POP) {
                handler = __handle_update_local_config_local_ctrl_pop;
            } else {
                OSAL_LOGE(TAG, "Invalid payload type for local config: %c", payload_type);
                esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
                return;
            }
        } else {
            OSAL_LOGE(TAG, "Invalid payload type: %c", payload_type);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
            return;
        }
        break;

    case RMAKER_HOST_CTRL_COMMAND_CHAR_WAIT_FLAGS:
        handler = __handle_wait_flags;
        break;

    case RMAKER_HOST_CTRL_COMMAND_CHAR_CLEAR_FLAGS:
        handler = __handle_clear_flags;
        break;

    case RMAKER_HOST_CTRL_COMMAND_CHAR_GET:
        payload_type = payload[0];

        if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_SIGNATURE) {
            handler = __handle_get_signature;
            payload++; payload_length--;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_CURRENT_TIME) {
            __handle_get_current_time(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_CURRENT_TIME_MS) {
            __handle_get_current_time_ms(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_CURRENT_TIMEZONE) {
            __handle_get_current_timezone(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_THING_NAME) {
            __handle_get_thing_name(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_INDEXED_SHADOW) {
            __handle_get_indexed_shadow(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_NAMED_SHADOW) {
            __handle_get_named_shadow(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_TAG_VALUE) {
            handler = __handle_get_tag_value;
            payload++; payload_length--;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_GROUP_INFO) {
            __handle_get_group_info(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_ALEXA_ENABLED) {
            __handle_get_alexa_enabled(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_GVA_ENABLED) {
            __handle_get_gva_enabled(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_ST_ENABLED) {
            __handle_get_st_enabled(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_SCHED_VERSION) {
            __handle_get_sched_version(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_TRIGGER_VERSION) {
            __handle_get_trigger_version(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_GETTABLE_CHAR_HEAP_STATUS) {
            __handle_get_heap_status(NULL, 0);
            return;
        } else {
            OSAL_LOGE(TAG, "Invalid payload type: %c", payload_type);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
            return;
        }
        break;

    case RMAKER_HOST_CTRL_COMMAND_CHAR_TIME_CONTROL:
        payload_type = payload[0];
        payload++; payload_length--;

        if (payload_type == RMAKER_HOST_CTRL_TIME_CONTROL_CHAR_SET) {
            handler = __handle_time_control_set_time;
        } else if (payload_type == RMAKER_HOST_CTRL_TIME_CONTROL_CHAR_ADVANCE) {
            handler = __handle_time_control_advance_time;
        } else {
            OSAL_LOGE(TAG, "Invalid payload type: %c", payload_type);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
            return;
        }
        break;

    case RMAKER_HOST_CTRL_COMMAND_CHAR_MQTT_CONTROL:
        payload_type = payload[0];
        payload++; payload_length--;
        if (payload_type == RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_NETWORK_FAILURE) {
            __handle_mqtt_control_force_network_failure(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_NETWORK_RESTORE) {
            __handle_mqtt_control_restore_network_default(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_OPERATIONS_FAILURE) {
            __handle_mqtt_control_force_operations_failure(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_OPERATIONS_RESTORE) {
            __handle_mqtt_control_restore_operations_default(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_DISCONNECT) {
            __handle_mqtt_control_disconnect(NULL, 0);
            return;
        } else if (payload_type == RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_CONNECT) {
            __handle_mqtt_control_connect(NULL, 0);
            return;
        } else {
            OSAL_LOGE(TAG, "Invalid payload type: %c", payload_type);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
            return;
        }
        break;

    case RMAKER_HOST_CTRL_COMMAND_CHAR_CLOUD_CONTROL:
        payload_type = payload[0];
        payload++; payload_length--;
        if (payload_type == RMAKER_HOST_CTRL_CLOUD_CONTROL_CHAR_SEND) {
            handler = __handle_cloud_control_send_cloud_events;
        } else {
            OSAL_LOGE(TAG, "Invalid payload type: %c", payload_type);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
            return;
        }
        break;

#ifdef CONFIG_RMNG_BRIDGE_ENABLED
    case RMAKER_HOST_CTRL_COMMAND_CHAR_BRIDGE: {
        if (payload_length < 1) {
            OSAL_LOGE(TAG, "Bridge command missing sub-command char");
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
            return;
        }
        char sub = (char)payload[0];
        esp_rmaker_host_ctrl_bridge_handle(sub, payload + 1, payload_length - 1);
        return;
    }
#endif

    default:
        OSAL_LOGE(TAG, "Invalid command: %c", buffer[0]);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }

    /* Handle command payload if handler is set */
    if (handler != NULL) {
        if (payload_length <= 1) {
            OSAL_LOGE(TAG, "Invalid payload length: %d for command: %c", (int) payload_length, buffer[0]);
            esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
            return;
        }

        handler(payload, payload_length);
    } else {
        OSAL_LOGE(TAG, "Invalid command: %c", buffer[0]);
        esp_rmaker_host_ctrl_send_response(RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID);
        return;
    }
}
