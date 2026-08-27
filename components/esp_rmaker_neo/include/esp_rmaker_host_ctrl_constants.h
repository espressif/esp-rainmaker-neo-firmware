/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_host_ctrl_constants.h
 * @brief Remote control constants.
 */

#ifndef __ESP_RMAKER_HOST_CTRL_CONSTANTS_H__
#define __ESP_RMAKER_HOST_CTRL_CONSTANTS_H__

#include "sdkconfig.h"

/* Markers ****************************************************************/

#define RMAKER_HOST_CTRL_END_CHAR                      '\r'
#define RMAKER_HOST_CTRL_DELIMITER_CHAR                '|'

/* Commands ****************************************************************/

#define RMAKER_HOST_CTRL_COMMAND_CHAR_PING             'p'
#define RMAKER_HOST_CTRL_VAL_PING_LENGTH                8

#define RMAKER_HOST_CTRL_COMMAND_CHAR_START            's'
#define RMAKER_HOST_CTRL_COMMAND_CHAR_STOP             'S'
#define RMAKER_HOST_CTRL_COMMAND_CHAR_RESET            'r'
#define RMAKER_HOST_CTRL_COMMAND_CHAR_RESET_KEEP_NVS   'R' // Full node deinit + reinit, NVS preserved (simulates cold reboot)
#define RMAKER_HOST_CTRL_COMMAND_CHAR_KILL             'k'

#define RMAKER_HOST_CTRL_COMMAND_CHAR_ADD              '+'
#define RMAKER_HOST_CTRL_COMMAND_CHAR_REMOVE           '-'
#define RMAKER_HOST_CTRL_COMMAND_CHAR_UPDATE           'u'
#define RMAKER_HOST_CTRL_COMMAND_CHAR_GET              'g'

#define RMAKER_HOST_CTRL_COMMAND_CHAR_WAIT_FLAGS       'w'
#define RMAKER_HOST_CTRL_COMMAND_CHAR_CLEAR_FLAGS      'c'

#define RMAKER_HOST_CTRL_COMMAND_CHAR_TIME_CONTROL     't'
#define RMAKER_HOST_CTRL_COMMAND_CHAR_MQTT_CONTROL     'm'
#define RMAKER_HOST_CTRL_COMMAND_CHAR_CLOUD_CONTROL    'C'

#define RMAKER_HOST_CTRL_COMMAND_CHAR_BRIDGE           'B' // Bridge multiplexed command (per-child operations)

/* Bridge subcommands ********************************************************/

/* Sub-commands under RMAKER_HOST_CTRL_COMMAND_CHAR_BRIDGE. The payload that
 * follows is documented per sub-command in the bridge handler. Handles
 * are 32-bit unsigned integers encoded as decimal ASCII in the payload. */

#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_ADD_CHILD         'a' // add_child: <suffix>|<bridge_local_id>|<timeout_ms>|
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_ADD_CHILD_NO_ACK  'A' // add_child_no_ack: <suffix>|<bridge_local_id>| (returns immediately after esp_rmaker_bridge_add_child; caller polls list_children to confirm READY)
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_REMOVE_CHILD      'r' // remove_child: <handle>|<timeout_ms>|
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_MARK_ONLINE       'o' // mark_online: <handle>|<0|1>|
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_THING_NAME  'n' // child_thing_name: <handle>|
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_LOCAL_ID    'l' // child_local_id: <handle>|
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_GROUP_INFO  'G' // child_group_info: <handle>|
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_LIST_CHILDREN     'L' // list_children: <start>|<count>| (paginated; count clamped firmware-side)
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_COMMIT_DEVICES    'c' // commit_devices: <handle>|
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_FILL_INFO   'i' // child_fill_info: <handle>|<name>|<type>|<fw_version>|<model>|
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_ADD_PARAM   '+' // child_add_param: <handle>| + self add_param fields
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_UPDATE_PARAM 'u' // child_update_param: <handle>|<device_id>|<param_id>|<typed_value>|
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_GET_PARAM   'p' // child_get_param: <handle>|<device_id>|<param_id>|
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_WAIT_FLAGS  'w' // child_wait_flags: <handle>|<flag_chars>|<timeout_ms>|
#define RMAKER_HOST_CTRL_BRIDGE_SUB_CHAR_CHILD_CLEAR_FLAGS 'x' // child_clear_flags: <handle>|<flag_chars>|

/* Max children returned in a single list_children page. */
#define RMAKER_HOST_CTRL_BRIDGE_LIST_PAGE_MAX 32

/* Payloads ****************************************************************/

#define RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_PARAM        'p'
#define RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_SERVICES     's'
#define RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_TAG          't'
#define RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_TIMEZONE     'z' // Timezone
#define RMAKER_HOST_CTRL_PAYLOAD_TYPE_CHAR_LOCAL_CONFIG 'l' // Local configuration

/* Services ****************************************************************/

#define RMAKER_HOST_CTRL_SERVICE_CHAR_TIMEZONE         't' // Timezone service
#define RMAKER_HOST_CTRL_SERVICE_CHAR_LATENCY          'l' // Latency service
#define RMAKER_HOST_CTRL_SERVICE_CHAR_LOCAL_CTRL       'c' // Local control service
#define RMAKER_HOST_CTRL_SERVICE_CHAR_ON_NETWORK_CHAL_RESP 'o' // On-network challenge response service

/* Local configuration keys ****************************************************************/

#define RMAKER_HOST_CTRL_LOCAL_CONFIG_CHAR_SCHED_VER    's' // Schedule version
#define RMAKER_HOST_CTRL_LOCAL_CONFIG_CHAR_TRIGGER_VER  'r' // Trigger version
#define RMAKER_HOST_CTRL_LOCAL_CONFIG_CHAR_LOCAL_CTRL_HTTP_PORT 'h' // Local control HTTP port (before local_ctrl enable; POSIX/tests)
#define RMAKER_HOST_CTRL_LOCAL_CONFIG_CHAR_LOCAL_CTRL_POP 'p' // Local control PoP (before local_ctrl enable; tests)

/* Gettables ****************************************************************/

#define RMAKER_HOST_CTRL_GETTABLE_CHAR_CURRENT_TIME     'T' // Current time
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_CURRENT_TIME_MS  'm' // Current time (Unix epoch ms, same clock as latency recv_ts)
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_CURRENT_TIMEZONE 'z' // Current timezone
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_THING_NAME       'n' // Thing name
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_SIGNATURE        'c' // Signature for a challenge
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_INDEXED_SHADOW   'i' // Indexed shadow (reported)
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_NAMED_SHADOW     'N' // Named shadow (reported)
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_PARAM            'p' // Parameter
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_TAG_VALUE        't' // Tag value
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_GROUP_INFO       'g' // Group information
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_ALEXA_ENABLED    'a' // Alexa enabled
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_GVA_ENABLED      'v' // GVA (Google Voice Assistant) enabled
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_ST_ENABLED       'h' // SmartThings enabled
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_SCHED_VERSION    's' // Schedule version
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_TRIGGER_VERSION  'r' // Trigger version
#define RMAKER_HOST_CTRL_GETTABLE_CHAR_HEAP_STATUS      'H' // Heap status

/* Flags ****************************************************************/

#define RMAKER_HOST_CTRL_FLAG_CHAR_ONLINE                  'c' // Registered as 'online'
#define RMAKER_HOST_CTRL_FLAG_CHAR_STARTED                 '+' // Started
#define RMAKER_HOST_CTRL_FLAG_CHAR_STOPPED                 '-' // Stopped
#define RMAKER_HOST_CTRL_FLAG_CHAR_STATE_REPORTED          '^' // State reported
#define RMAKER_HOST_CTRL_FLAG_CHAR_TIMESERIES_REPORTED     'T' // Timeseries reported
#define RMAKER_HOST_CTRL_FLAG_CHAR_NODE_CONFIG_SENT        '{' // Node configuration sent
#define RMAKER_HOST_CTRL_FLAG_CHAR_NOTIFICATION_SENT       'n' // Notification sent
#define RMAKER_HOST_CTRL_FLAG_CHAR_STATE_STARTED_LISTENING 'l' // State started listening
#define RMAKER_HOST_CTRL_FLAG_CHAR_GROUP_INFO              'g' // Group information received
#define RMAKER_HOST_CTRL_FLAG_CHAR_ALEXA_ENABLED           'a' // Alexa enabled received
#define RMAKER_HOST_CTRL_FLAG_CHAR_GVA_ENABLED             'v' // GVA enabled received
#define RMAKER_HOST_CTRL_FLAG_CHAR_ST_ENABLED              'h' // SmartThings enabled received
#define RMAKER_HOST_CTRL_FLAG_CHAR_SCHED_VERSION           's' // Schedule version received
#define RMAKER_HOST_CTRL_FLAG_CHAR_SCHED_DETAILS           'S' // Schedule details received
#define RMAKER_HOST_CTRL_FLAG_CHAR_TRIGGER_VERSION         'r' // Trigger version received
#define RMAKER_HOST_CTRL_FLAG_CHAR_TRIGGER_DETAILS         'R' // Trigger details received

/* Data types ****************************************************************/

#define RMAKER_HOST_CTRL_DATA_TYPE_CHAR_INT            'i'
#define RMAKER_HOST_CTRL_DATA_TYPE_CHAR_FLOAT          'f'
#define RMAKER_HOST_CTRL_DATA_TYPE_CHAR_BOOLEAN        'b'
#define RMAKER_HOST_CTRL_DATA_TYPE_CHAR_STRING         's'
#define RMAKER_HOST_CTRL_DATA_TYPE_CHAR_OBJECT         'o'
#define RMAKER_HOST_CTRL_DATA_TYPE_CHAR_ARRAY          'a'

/* Properties ****************************************************************/

#define RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_READ          'R' // Readable
#define RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_WRITE         'W' // Writable
#define RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_INDEXED       'I' // Indexed
#define RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_PERSIST       'P' // Persist
#define RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_TIME_SERIES   'T' // Time series
#define RMAKER_HOST_CTRL_PROPERTY_CHAR_PARAM_TS_CUMULATIVE 'C' // Time series cumulative

/* Time control **************************************************************/

#define RMAKER_HOST_CTRL_TIME_CONTROL_CHAR_SET         's' // Set time
#define RMAKER_HOST_CTRL_TIME_CONTROL_CHAR_ADVANCE     'a' // Advance time

/* MQTT control **************************************************************/

#define RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_NETWORK_FAILURE     's' // Force all network operations (connect, send, recv) to fail
#define RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_NETWORK_RESTORE     'S' // Restore default network operations (connect, send, recv) settings
#define RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_OPERATIONS_FAILURE  'o' // Force all MQTT operations (publish, subscribe, unsubscribe) to fail
#define RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_OPERATIONS_RESTORE  'O' // Restore default MQTT operations (publish, subscribe, unsubscribe) settings
#define RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_DISCONNECT          'd' // Disconnect MQTT (esp_rmaker_mqtt_impl.disconnect)
#define RMAKER_HOST_CTRL_MQTT_CONTROL_CHAR_CONNECT             'D' // Connect MQTT (esp_rmaker_mqtt_impl.connect)

/* Cloud control **************************************************************/

#define RMAKER_HOST_CTRL_CLOUD_CONTROL_CHAR_SEND       '^' // Send cloud event

#define RMAKER_HOST_CTRL_CLOUD_CONTROL_CHAR_EVENT_getSchedVer   's' // getSchedVer cloud event
#define RMAKER_HOST_CTRL_CLOUD_CONTROL_CHAR_EVENT_getTriggerVer 'r' // getTriggerVer cloud event

/* Response codes ************************************************************/

#define RMAKER_HOST_CTRL_RESPONSE_CHAR_OK              'o' // OK
#define RMAKER_HOST_CTRL_RESPONSE_CHAR_ERROR           'e' // Internal error
#define RMAKER_HOST_CTRL_RESPONSE_CHAR_INVALID         'i' // Invalid command
#define RMAKER_HOST_CTRL_RESPONSE_CHAR_TYPE_MISMATCH   't' // Type mismatch
#define RMAKER_HOST_CTRL_RESPONSE_CHAR_NOT_FOUND       'n' // Not found
#define RMAKER_HOST_CTRL_RESPONSE_CHAR_TIMEOUT         'w' // Timeout

#endif /* __ESP_RMAKER_HOST_CTRL_CONSTANTS_H__ */
