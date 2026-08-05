set(RMNG_OTA_REQUIRES "osal" "esp_rmaker_neo_common")
set(RMNG_OTA_PRIV_REQUIRES "mbedtls")
set(RMNG_OTA_OPT_PRIV_REQUIRES "")

if (CONFIG_RMNG_OTA_TRANSPORT_MQTT)
    if (ESP_PLATFORM AND CONFIG_RMNG_OTA_MQTT_DATA_TYPE_CBOR)
        list(APPEND RMNG_OTA_OPT_PRIV_REQUIRES "espressif__cbor")
    endif ()
endif ()
