/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_data_model.h
 * @brief Data model for ESP RainMaker Neo.
 */

#ifndef __ESP_RMAKER_DATA_MODEL_H__
#define __ESP_RMAKER_DATA_MODEL_H__

/* Includes *******************************************************/

/* Standard includes. */
#include <stdint.h>
#include <stdbool.h>

/* Handle includes. */
#include "esp_rmaker_handle.h"

/* Node includes. */
#include "esp_rmaker_node.h"

/* State includes. */
#include "esp_rmaker_state.h"

/* Public types *******************************************************/

/** Param property flags */
typedef enum {
    PROP_FLAG_READ = (1 << 0),          //!< Parameter can be read from the cloud
    PROP_FLAG_WRITE = (1 << 1),         //!< Parameter can be written from the cloud
    PROP_FLAG_TIME_SERIES = (1 << 2),   //!< Parameter is a time series (not cumulative)
    PROP_FLAG_TS_CUMULATIVE = (1 << 3), //!< Parameter is a time series (cumulative)
    PROP_FLAG_INDEXED = (1 << 4),       //!< Parameter value is also in the indexed shadow
    PROP_FLAG_PERSIST = (1 << 5),       //!< Parameter value is persisted across reboots
} esp_rmaker_param_property_flags_t;

/**  ESP RainMaker Neo Device Handle */
typedef esp_rmaker_handle_t esp_rmaker_device_t;

/** ESP RainMaker Neo Parameter Handle */
typedef esp_rmaker_handle_t esp_rmaker_param_t;

/* --- Write request callbacks --- */

/** Write request Context */
typedef struct {
    /** Source of request */
    esp_rmaker_req_src_t src;
} esp_rmaker_write_ctx_t;

/** Parameter write request payload */
typedef struct {
    /** Parameter handle */
    esp_rmaker_param_t *param;
    /** Value to write */
    esp_rmaker_param_val_t val;
} esp_rmaker_param_write_req_t;

/**
 * @brief Callback for bulk parameter value write requests.
 *
 *
 * This callback is recommended over esp_rmaker_device_write_cb_t since it gives all values of a given device together,
 * which will help if the parameters are related to each other.
 *
 * The callback should call the esp_rmaker_param_update_and_report() API if the new value is to be set
 * and reported back.
 *
 * @param[in] device Device handle.
 * @param[in] write_req Array of parameter write request payloads.
 * @param[in] count Count of parameters and their values passed to this callback
 * @param[in] priv_data Pointer to the private data passed while creating the device.
 * @param[in] ctx Context associated with the request.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
typedef esp_rmaker_error_t (*esp_rmaker_device_bulk_write_cb_t)(const esp_rmaker_device_t *device, const esp_rmaker_param_write_req_t write_req[],
        uint8_t count, void *priv_data, esp_rmaker_write_ctx_t *ctx);

/**
 * @brief Callback for parameter value write requests.
 *
 * The callback should call the esp_rmaker_param_update_and_report() API if the new value is to be set
 * and reported back.
 *
 * @param[in] device Device handle.
 * @param[in] param Parameter handle.
 * @param[in] val Pointer to @ref esp_rmaker_param_val_t. Use appropriate elements as per the value type.
 * @param[in] priv_data Pointer to the private data passed while creating the device.
 * @param[in] ctx Context associated with the request.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
typedef esp_rmaker_error_t (*esp_rmaker_device_write_cb_t)(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx);

/* --- Read request callbacks --- */


/** Read request context */
typedef struct {
    /** Source of request */
    esp_rmaker_req_src_t src;
} esp_rmaker_read_ctx_t;

/**
 * @brief Callback for bulk parameter value reads
 *
 * The callback should call the esp_rmaker_param_update_and_report() API if the new value is to be set
 * and reported back.
 *
 * @note Currently, the read callback never gets invoked as the communication between clients (mobile phones, CLI, etc.)
 * and node is asynchronous. So, the read request does not reach the node. This callback may however be used in future.
 *
 * @param[in] device Device handle.
 * @param[in] params Array of Parameter handles.
 * @param[in] count Count of parameters passed to this callback.
 * @param[in] priv_data Pointer to the private data passed while creating the device.
 * @param[in] ctx Context associated with the request.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
typedef esp_rmaker_error_t (*esp_rmaker_device_bulk_read_cb_t)(const esp_rmaker_device_t *device, const esp_rmaker_param_t *params[],
        uint8_t count, void *priv_data, esp_rmaker_read_ctx_t *ctx);

/**
 * @brief Callback for parameter value reads
 *
 * The callback should call the esp_rmaker_param_update_and_report() API if the new value is to be set
 * and reported back.
 *
 * @note Currently, the read callback never gets invoked as the communication between clients (mobile phones, CLI, etc.)
 * and node is asynchronous. So, the read request does not reach the node. This callback may however be used in future.
 *
 * @param[in] device Device handle.
 * @param[in] param Parameter handle.
 * @param[in] priv_data Pointer to the private data passed while creating the device.
 * @param[in] ctx Context associated with the request.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
typedef esp_rmaker_error_t (*esp_rmaker_device_read_cb_t)(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        void *priv_data, esp_rmaker_read_ctx_t *ctx);

/* Public function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Clear all stored values for a node and its devices.
 *
 * This function clears the NVS storage for all devices attached to the node.
 * It should be called before esp_rmaker_node_deinit() to ensure that persistent
 * parameter values are properly cleared from storage.
 *
 * @param[in] node Node handle.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if node handle is NULL.
 * @return ESP_RMAKER_FAIL if clearing stored values for any device fails.
 */
esp_rmaker_error_t esp_rmaker_node_clear_stored_values(const esp_rmaker_node_t *node);

/**
 * @brief Get device by id
 *
 * Get handle for a device based on the id.
 *
 * @param[in] node Node handle.
 * @param[in] device_id Device id to search.
 *
 * @return Device handle on success.
 * @return NULL in case of failure.
 */
esp_rmaker_device_t *esp_rmaker_node_get_device_by_id(const esp_rmaker_node_t *node, const char *device_id);

/**
 * @brief Add a device to a node
 *
 * @param[in] node Node handle.
 * @param[in] device Device handle.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_node_add_device(const esp_rmaker_node_t *node, const esp_rmaker_device_t *device);

/**
 * @brief Remove a device from a node
 *
 * Does not delete the device.
 *
 * @param[in] node Node handle.
 * @param[in] device Device handle.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_node_remove_device(const esp_rmaker_node_t *node, const esp_rmaker_device_t *device);

/* --- Device --- */

/**
 * @brief Create a Device
 *
 * This API will create a virtual "Device".
 * This could be something like a Switch, Lightbulb, etc.
 *
 * @note The device created needs to be added to a node using esp_rmaker_node_add_device().
 *
 * @param[in] dev_id The unique device id. Must not contain '.', which is reserved as the data
 *                   model path separator.
 * @param[in] type Optional device type. Can be kept NULL.
 * @param[in] priv_data (Optional) Private data associated with the device. This will be passed to callbacks.
 * It should stay allocated throughout the lifetime of the device.
 *
 * @return Device handle on success.
 * @return NULL in case of any error.
 */
esp_rmaker_device_t *esp_rmaker_device_create(const char *dev_id, const char *type, void *priv_data);

/**
 * @brief Create a Service
 *
 * This API will create a "Service". It is exactly same like a device in terms of structure and so, all
 * APIs for device are also valid for a service.
 * A service could be something like OTA, diagnostics, etc.
 *
 * @note Id of a service should not clash with id of a device.
 * @note The service created needs to be added to a node using esp_rmaker_node_add_device().
 *
 * @param[in] serv_id The unique service id. Must not contain '.', which is reserved as the data
 *                    model path separator.
 * @param[in] type Optional service type. Can be kept NULL.
 * @param[in] priv_data (Optional) Private data associated with the service. This will be passed to callbacks.
 * It should stay allocated throughout the lifetime of the device.
 *
 * @return Device handle on success.
 * @return NULL in case of any error.
 */
esp_rmaker_device_t *esp_rmaker_service_create(const char *serv_id, const char *type, void *priv_data);

/**
 * @brief Delete a Device/Service
 *
 * This API will delete a device created using esp_rmaker_device_create().
 *
 * @note The device should first be removed from the node using esp_rmaker_node_remove_device() before deleting.
 *
 * @param[in] device Device handle.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_device_delete(const esp_rmaker_device_t *device);

/**
 * @brief Clear stored values for a device.
 *
 * This function clears the NVS storage for the specified device, removing all
 * persistent parameter values. It should be called before esp_rmaker_device_delete()
 * to ensure that persistent parameter values are properly cleared from storage.
 *
 * @param[in] device Device handle.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if device handle or device id is NULL.
 * @return ESP_RMAKER_FAIL if NVS operations fail.
 */
esp_rmaker_error_t esp_rmaker_device_clear_stored_values(const esp_rmaker_device_t *device);

/**
 * @brief Add a parameter to a device/service
 *
 * @param[in] device Device handle.
 * @param[in] param Parameter handle.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_device_add_param(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param);

/**
 * @brief Add a Device attribute
 *
 * @note Device attributes are reported only once after a boot-up as part of the node
 * configuration.
 * Eg. Serial Number
 *
 * @param[in] device Device handle.
 * @param[in] attr_name Name of the attribute.
 * @param[in] val Value of the attribute.
 *
 * @return ESP_RMAKER_OK if the attribute was added successfully.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_device_add_attribute(const esp_rmaker_device_t *device, const char *attr_name, const char *val);

/**
 * @brief Assign a primary parameter
 *
 * Assign a parameter (already added using esp_rmaker_device_add_param()) as a primary parameter,
 * which can be used by clients (phone apps specifically) to give prominence to it.
 *
 * @param[in] device Device handle.
 * @param[in] param Parameter handle.
 *
 * @return ESP_RMAKER_OK if the parameter was assigned as the primary successfully.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_device_assign_primary_param(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param);

/**
 * @brief Add callbacks for a device/service
 *
 * Add read/write callbacks for a device that will be invoked as per requests received from the cloud (or other paths
 * as may be added in future).
 *
 * @note A device is created with a built-in bulk write callback that fans a bulk request out to
 *       this single-parameter callback, one parameter at a time. Registering your own bulk
 *       callback with esp_rmaker_device_add_bulk_cb() replaces that default, after which the
 *       callback registered here is no longer invoked.
 *
 * @param[in] device Device handle.
 * @param[in] write_cb Write callback. NULL clears any previously registered callback.
 * @param[in] read_cb Read callback. NULL clears any previously registered callback.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if device is NULL.
 */
esp_rmaker_error_t esp_rmaker_device_add_cb(const esp_rmaker_device_t *device, esp_rmaker_device_write_cb_t write_cb, esp_rmaker_device_read_cb_t read_cb);

/**
 * @brief Add bulk callbacks for a device/service
 *
 * Add bulk read/write callbacks for a device that will be invoked as per requests received from the cloud (or other paths
 * as may be added in future).
 *
 * This is an improvement over the earlier callbacks registered using esp_rmaker_device_add_cb() so that all parameters
 * received in a single request are passed to the callback together, instead of one by one.
 *
 * @note This replaces the default bulk callback installed at device creation, which fans requests
 *       out to the single-parameter callback of esp_rmaker_device_add_cb(). Register either one or
 *       the other, not both.
 *
 * @param[in] device Device handle.
 * @param[in] write_cb Bulk Write callback. NULL clears any previously registered callback,
 *                     including the default one.
 * @param[in] read_cb Bulk Read callback. NULL clears any previously registered callback.
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if device is NULL.
 */
esp_rmaker_error_t esp_rmaker_device_add_bulk_cb(const esp_rmaker_device_t *device, esp_rmaker_device_bulk_write_cb_t write_cb, esp_rmaker_device_bulk_read_cb_t read_cb);

/**
 * @brief Get device id from handle
 *
 * @param[in] device Device handle.
 *
 * @return NULL terminated device id string on success.
 * @return NULL in case of failure.
 */
char *esp_rmaker_device_get_id(const esp_rmaker_device_t *device);

/**
 * @brief Get Device Private data from handle
 *
 * @param[in] device Device handle.
 *
 * @return void type of pointer on success.
 * @return NULL if no private data found.
 */
void *esp_rmaker_device_get_priv_data(const esp_rmaker_device_t *device);

/**
 * @brief Get device type from handle
 *
 * @param[in] device Device handle.
 *
 * @return NULL terminated device type string on success.
 * @return NULL in case of failure, or if the type wasn't provided while creating the device.
 */
char *esp_rmaker_device_get_type(const esp_rmaker_device_t *device);

/**
 * @brief Get parameter by type
 *
 * Get handle for a parameter based on the type.
 *
 * @note If there are multiple parameters with the same type, this will return the first one. The API
 * esp_rmaker_device_get_param_by_id() can be used to get a specific parameter, because the parameter
 * ids in a device are unique.
 *
 * @param[in] device Device handle.
 * @param[in] param_type Parameter type to search.
 *
 * @return Parameter handle on success.
 * @return NULL in case of failure.
 */
esp_rmaker_param_t *esp_rmaker_device_get_param_by_type(const esp_rmaker_device_t *device, const char *param_type);

/**
 * @brief Get parameter by id
 *
 * Get handle for a parameter based on the id.
 *
 * @param[in] device Device handle.
 * @param[in] param_id Parameter id to search.
 *
 * @return Parameter handle on success.
 * @return NULL in case of failure.
 */
esp_rmaker_param_t *esp_rmaker_device_get_param_by_id(const esp_rmaker_device_t *device, const char *param_id);

/* --- Parameter --- */

/**
 * @brief Create a Parameter
 *
 * Parameter can be something like Temperature, Outlet state, Lightbulb brightness, etc.
 *
 * Any changes should be reported using the esp_rmaker_param_update_and_report() API.
 * Any remote changes will be reported to the application via the device callback, if registered.
 *
 * @note The parameter created needs to be added to a device using esp_rmaker_device_add_param().
 * Parameter id should be unique in a given device.
 *
 * @param[in] param_id Id of the parameter.
 * @param[in] type Optional parameter type. Can be kept NULL.
 * @param[in] val Value of the parameter. This also specifies the type that will be assigned
 * to this parameter. You can use esp_rmaker_bool(), esp_rmaker_int(), esp_rmaker_float()
 * or esp_rmaker_str() functions as the argument here. Eg, esp_rmaker_bool(true).
 * @param[in] properties Properties of the parameter, which will be a logical OR of flags in
 *  @ref esp_rmaker_param_property_flags_t.
 *
 * @return Parameter handle on success.
 * @return NULL in case of failure.
 */
esp_rmaker_param_t *esp_rmaker_param_create(const char *param_id, const char *type,
        esp_rmaker_param_val_t val, uint8_t properties);

/**
 * @brief Add bounds for an integer/float parameter
 *
 * This can be used to add bounds (min/max values) for a given integer parameter. Eg. brightness
 * will have bounds as 0 and 100 if it is a percentage.
 * Eg. esp_rmaker_param_add_bounds(brightness_param, esp_rmaker_int(0), esp_rmaker_int(100), esp_rmaker_int(5));
 *
 * @note The RainMaker Neo core does not check the bounds. It is up to the application to handle it.
 *
 * @param[in] param Parameter handle.
 * @param[in] min Minimum allowed value.
 * @param[in] max Maximum allowed value.
 * @param[in] step Minimum stepping (set to 0 if no specific value is desired).
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_param_add_bounds(const esp_rmaker_param_t *param,
        esp_rmaker_param_val_t min, esp_rmaker_param_val_t max, esp_rmaker_param_val_t step);

/**
 * @brief Add a UI Type to a parameter
 *
 * This will be used by the Phone apps (or other clients) to render appropriate UI for the given
 * parameter. Please refer the RainMaker Neo documentation for supported UI Types.
 *
 * @param[in] param Parameter handle.
 * @param[in] ui_type String describing the UI Type.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_param_add_ui_type(const esp_rmaker_param_t *param, const char *ui_type);

/**
 * @brief Add max count for an array parameter
 *
 * This can be used to put a limit on the maximum number of elements in an array.
 *
 * @note The RainMaker Neo core does not check the values. It is up to the application to handle it.
 *
 * @param[in] param Parameter handle.
 * @param[in] count Max number of elements allowed in the array.
 *
 * @return ESP_RMAKER_OK on success.
 * @return error in case of failure.
 */
esp_rmaker_error_t esp_rmaker_param_add_array_max_count(const esp_rmaker_param_t *param, int count);

/**
 * @brief Get parameter id from handle
 *
 * @param[in] param Parameter handle.
 *
 * @return NULL terminated parameter id string on success.
 * @return NULL in case of failure.
 */
char *esp_rmaker_param_get_id(const esp_rmaker_param_t *param);

/**
 * @brief Get parameter type from handle
 *
 * @param[in] param Parameter handle.
 *
 * @return NULL terminated parameter type string on success.
 * @return NULL in case of failure, or if the type wasn't provided while creating the parameter.
 */
char *esp_rmaker_param_get_type(const esp_rmaker_param_t *param);

/**
 * @brief Get parameter value
 *
 * This gives the parameter value that is stored in the RainMaker Neo core.
 *
 * @note This does not call any explicit functions to read value from hardware/driver.
 *
 * @param[in] param Parameter handle
 *
 * @return Pointer to parameter value on success.
 * @return NULL in case of failure.
 */
esp_rmaker_param_val_t *esp_rmaker_param_get_val(esp_rmaker_param_t *param);

/**
 * @brief Update a parameter
 *
 * This will update the value of a parameter with ESP RainMaker Neo core and schedule a delayed report
 * of all changed parameters. The report will be sent after a configurable delay (CONFIG_RMAKER_STATE_REPORT_DELAY_MS).
 * This can be used when multiple parameters need to be reported together, as multiple calls will
 * coalesce into a single report.
 *
 * Eg. If x parameters are to be reported, this API can be used for all x parameters.
 * All parameters updated within the delay window will be included in a single report.
 *
 * Alternatively, if immediate reporting is needed, use esp_rmaker_param_update_and_report().
 *
 * @note If the new value equals the current value, the function returns ESP_RMAKER_OK without making any changes.
 * @note If PROP_FLAG_TIME_SERIES or PROP_FLAG_TS_CUMULATIVE is set, the value will be added to the timeseries queue.
 * @note The parameter value will be stored to NVS if PROP_FLAG_PERSIST is set.
 * @note The function validates that the new value is within bounds (if bounds are set) and that the value type matches.
 * @note Automation triggers are checked and fired if the update causes any trigger conditions to be met.
 *
 * Sample:
 *
 * @code
 * esp_rmaker_param_update(param1, esp_rmaker_float(10.2));
 * esp_rmaker_param_update(param2, esp_rmaker_int(55));
 * esp_rmaker_param_update(param3, esp_rmaker_int(95));
 * // All three parameters will be reported together after the delay
 * @endcode
 *
 * @param[in] param Parameter handle.
 * @param[in] val New value of the parameter.
 *
 * @return ESP_RMAKER_OK if the parameter was updated successfully, or if the value was already equal.
 * @return ESP_RMAKER_INVALID_ARG if param is NULL, value is out of bounds, value type is invalid, or value comparison fails.
 * @return ESP_RMAKER_FAIL if memory allocation fails (for string/object/array types).
 */
esp_rmaker_error_t esp_rmaker_param_update(const esp_rmaker_param_t *param, esp_rmaker_param_val_t val);

/**
 * @brief Update and report a parameter
 *
 * Calling this API will update the parameter and immediately report all changed parameters
 * to ESP RainMaker Neo cloud. This cancels any scheduled delayed reports and sends the report right away.
 * This should be used whenever there is any local change that needs immediate reporting.
 *
 * @note This function calls esp_rmaker_param_update() internally, so all the same behaviors apply
 * (timeseries, persistence, validation, etc.), but with immediate reporting instead of scheduled reporting.
 * @note If the parameter update fails, the function returns the update error without attempting to report.
 * @note If the parameter update succeeds but the report fails, the function returns the report error.
 *
 * @param[in] param Parameter handle.
 * @param[in] val New value of the parameter.
 *
 * @return ESP_RMAKER_OK if the parameter was updated and reported successfully.
 * @return ESP_RMAKER_INVALID_ARG if param is NULL or the update fails with INVALID_ARG.
 * @return ESP_RMAKER_FAIL if the update fails with FAIL, or if the report fails.
 */
esp_rmaker_error_t esp_rmaker_param_update_and_report(const esp_rmaker_param_t *param, esp_rmaker_param_val_t val);

/**
 * @brief Update and notify a parameter
 *
 * This updates the parameter (like esp_rmaker_param_update(), so the new value is reported to
 * ESP RainMaker Neo cloud with the next state report) and additionally triggers a notification
 * on the phone apps (if enabled), published on the notify topic of the node owning the parameter.
 *
 * @warning The notification payload currently carries no parameter identity: the cloud is told
 *          only that this node has an alert, so the phone app shows a generic
 *          "Node <node ID> has an alert". Carrying the parameter ID/value through to the app
 *          needs a corresponding cloud-side change.
 *
 * @note This should be used only when some local change requires explicit notification even when the
 * phone app is in background, not otherwise.
 * Eg. Alarm got triggered, temperature exceeded some threshold, etc.
 *
 * @param[in] param Parameter handle.
 * @param[in] val New value of the parameter.
 *
 * @return ESP_RMAKER_OK if the parameter was updated successfully. The notification is
 *         best-effort: a notify failure is logged but does not change the return value.
 * @return error in case the parameter update itself failed.
 */
esp_rmaker_error_t esp_rmaker_param_update_and_notify(const esp_rmaker_param_t *param, esp_rmaker_param_val_t val);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_DATA_MODEL_H__ */
