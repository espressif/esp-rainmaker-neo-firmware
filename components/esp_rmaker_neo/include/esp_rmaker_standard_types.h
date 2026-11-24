/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_standard_types.h
 * @brief Standard types for RainMaker Neo.
 */

#ifndef __ESP_RMAKER_STANDARD_TYPES_H__
#define __ESP_RMAKER_STANDARD_TYPES_H__

/********** STANDARD UI TYPES **********/

#define ESP_RMAKER_UI_TOGGLE            "esp.ui.toggle"
#define ESP_RMAKER_UI_SLIDER            "esp.ui.slider"
#define ESP_RMAKER_UI_DROPDOWN          "esp.ui.dropdown"
#define ESP_RMAKER_UI_TEXT              "esp.ui.text"
#define ESP_RMAKER_UI_HUE_SLIDER        "esp.ui.hue-slider"
#define ESP_RMAKER_UI_HUE_CIRCLE        "esp.ui.hue-circle"
#define ESP_RMAKER_UI_PUSHBUTTON        "esp.ui.push-btn-big"
#define ESP_RMAKER_UI_TRIGGER           "esp.ui.trigger"
#define ESP_RMAKER_UI_HIDDEN            "esp.ui.hidden"
#define ESP_RMAKER_UI_QR_SCAN           "esp.ui.qr-scan"

/** @brief Standard UI hint a param's config may carry. */
typedef enum {
    RMAKER_UI_TYPE_NONE = 0,   /**< The config reported no ui_type. */
    RMAKER_UI_TYPE_UNKNOWN,    /**< A ui_type string this SDK does not map. */
    RMAKER_UI_TYPE_TOGGLE,     /**< ESP_RMAKER_UI_TOGGLE */
    RMAKER_UI_TYPE_SLIDER,     /**< ESP_RMAKER_UI_SLIDER */
    RMAKER_UI_TYPE_DROPDOWN,   /**< ESP_RMAKER_UI_DROPDOWN */
    RMAKER_UI_TYPE_TEXT,       /**< ESP_RMAKER_UI_TEXT */
    RMAKER_UI_TYPE_HUE_SLIDER, /**< ESP_RMAKER_UI_HUE_SLIDER */
    RMAKER_UI_TYPE_HUE_CIRCLE, /**< ESP_RMAKER_UI_HUE_CIRCLE */
    RMAKER_UI_TYPE_PUSHBUTTON, /**< ESP_RMAKER_UI_PUSHBUTTON */
    RMAKER_UI_TYPE_TRIGGER,    /**< ESP_RMAKER_UI_TRIGGER */
    RMAKER_UI_TYPE_HIDDEN,     /**< ESP_RMAKER_UI_HIDDEN */
    RMAKER_UI_TYPE_QR_SCAN,    /**< ESP_RMAKER_UI_QR_SCAN */
} esp_rmaker_ui_type_t;

/********** STANDARD UNIT TOKENS **********/

/* The canonical unit a param's values are reported in, for esp_rmaker_param_add_unit().
 * These are machine tokens, not display symbols: a client owns the symbol and its
 * localisation. A device declares what it actually reports in rather than converting to
 * a house unit, so a sensor that genuinely measures in fahrenheit says so and no client
 * has to relabel it. The set is open: this list is the convention, not a closed
 * vocabulary, esp_rmaker_param_add_unit() accepts any token, and a client that cannot
 * express one must degrade rather than guess. */

#define ESP_RMAKER_UNIT_CELSIUS         "celsius"
#define ESP_RMAKER_UNIT_FAHRENHEIT      "fahrenheit"
#define ESP_RMAKER_UNIT_KELVIN          "kelvin"
#define ESP_RMAKER_UNIT_LUX             "lux"
#define ESP_RMAKER_UNIT_PERCENT         "percent"
#define ESP_RMAKER_UNIT_KPA             "kpa"
#define ESP_RMAKER_UNIT_DEGREES         "degrees"
#define ESP_RMAKER_UNIT_SECONDS         "seconds"
#define ESP_RMAKER_UNIT_MINUTES         "minutes"
#define ESP_RMAKER_UNIT_CUBIC_METERS_PER_HOUR   "cubic_meters_per_hour"

/********** STANDARD PARAM TYPES **********/

#define ESP_RMAKER_PARAM_NAME           "esp.param.name"
#define ESP_RMAKER_PARAM_POWER          "esp.param.power"
#define ESP_RMAKER_PARAM_BRIGHTNESS     "esp.param.brightness"
#define ESP_RMAKER_PARAM_DIM            "esp.param.dim"
#define ESP_RMAKER_PARAM_HUE            "esp.param.hue"
#define ESP_RMAKER_PARAM_SATURATION "esp.param.saturation"
#define ESP_RMAKER_PARAM_INTENSITY      "esp.param.intensity"
#define ESP_RMAKER_PARAM_CCT            "esp.param.cct"
#define ESP_RMAKER_PARAM_SPEED          "esp.param.speed"
#define ESP_RMAKER_PARAM_DIRECTION      "esp.param.direction"
#define ESP_RMAKER_PARAM_TEMPERATURE    "esp.param.temperature"
#define ESP_RMAKER_PARAM_OTA_STATUS     "esp.param.ota_status"
#define ESP_RMAKER_PARAM_OTA_INFO       "esp.param.ota_info"
#define ESP_RMAKER_PARAM_OTA_URL        "esp.param.ota_url"
#define ESP_RMAKER_PARAM_TIMEZONE       "esp.param.tz"
#define ESP_RMAKER_PARAM_TIMEZONE_POSIX       "esp.param.tz_posix"
#define ESP_RMAKER_PARAM_TIMESTAMP      "esp.param.timestamp"
#define ESP_RMAKER_PARAM_SCHEDULES      "esp.param.schedules"
#define ESP_RMAKER_PARAM_SCENES         "esp.param.scenes"
#define ESP_RMAKER_PARAM_REBOOT         "esp.param.reboot"
#define ESP_RMAKER_PARAM_NETWORK_RESET  "esp.param.network-reset"
#define ESP_RMAKER_PARAM_FACTORY_RESET  "esp.param.factory-reset"
#define ESP_RMAKER_PARAM_LOCAL_CONTROL_POP      "esp.param.local_control_pop"
#define ESP_RMAKER_PARAM_LOCAL_CONTROL_TYPE     "esp.param.local_control_type"
#define ESP_RMAKER_PARAM_LOCAL_CONTROL_USERNAME "esp.param.local_control_username"
#define ESP_RMAKER_PARAM_TOGGLE         "esp.param.toggle"
#define ESP_RMAKER_PARAM_RANGE          "esp.param.range"
#define ESP_RMAKER_PARAM_MODE           "esp.param.mode"
#define ESP_RMAKER_PARAM_BLINDS_POSITION     "esp.param.blinds-position"
#define ESP_RMAKER_PARAM_GARAGE_POSITION     "esp.param.garage-position"
#define ESP_RMAKER_PARAM_LIGHT_MODE     "esp.param.light-mode"
#define ESP_RMAKER_PARAM_AC_MODE        "esp.param.ac-mode"
#define ESP_RMAKER_PARAM_ADD_ZIGBEE_DEVICE     "esp.param.add_zigbee_device"


/********** STANDARD DEVICE TYPES **********/

#define ESP_RMAKER_DEVICE_SWITCH        "esp.device.switch"
#define ESP_RMAKER_DEVICE_LIGHTBULB     "esp.device.lightbulb"
#define ESP_RMAKER_DEVICE_FAN           "esp.device.fan"
#define ESP_RMAKER_DEVICE_TEMP_SENSOR   "esp.device.temperature-sensor"
#define ESP_RMAKER_DEVICE_LIGHT         "esp.device.light"
#define ESP_RMAKER_DEVICE_OUTLET        "esp.device.outlet"
#define ESP_RMAKER_DEVICE_PLUG          "esp.device.plug"
#define ESP_RMAKER_DEVICE_SOCKET        "esp.device.socket"
#define ESP_RMAKER_DEVICE_LOCK          "esp.device.lock"
#define ESP_RMAKER_DEVICE_BLINDS_INTERNAL   "esp.device.blinds-internal"
#define ESP_RMAKER_DEVICE_BLINDS_EXTERNAL   "esp.device.blinds-external"
#define ESP_RMAKER_DEVICE_GARAGE_DOOR   "esp.device.garage-door"
#define ESP_RMAKER_DEVICE_GARAGE_LOCK   "esp.device.garage-door-lock"
#define ESP_RMAKER_DEVICE_SPEAKER       "esp.device.speaker"
#define ESP_RMAKER_DEVICE_AIR_CONDITIONER   "esp.device.air-conditioner"
#define ESP_RMAKER_DEVICE_THERMOSTAT    "esp.device.thermostat"
#define ESP_RMAKER_DEVICE_TV            "esp.device.tv"
#define ESP_RMAKER_DEVICE_WASHER        "esp.device.washer"
#define ESP_RMAKER_DEVICE_OTHER         "esp.device.other"
#define ESP_RMAKER_DEVICE_ZIGBEE_GATEWAY   "esp.device.zigbee_gateway"
#define ESP_RMAKER_DEVICE_THREAD_BR        "esp.device.thread-br"

/********** STANDARD SERVICE TYPES **********/
#define ESP_RMAKER_SERVICE_OTA          "esp.service.ota"
#define ESP_RMAKER_SERVICE_TIME         "esp.service.time"
#define ESP_RMAKER_SERVICE_SCHEDULE     "esp.service.schedule"
#define ESP_RMAKER_SERVICE_SCENES       "esp.service.scenes"
#define ESP_RMAKER_SERVICE_SYSTEM       "esp.service.system"
#define ESP_RMAKER_SERVICE_LOCAL_CONTROL    "esp.service.local_control"

#endif /* __ESP_RMAKER_STANDARD_TYPES_H__ */
