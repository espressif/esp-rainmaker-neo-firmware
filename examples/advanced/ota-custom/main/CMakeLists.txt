set(OTA_CUSTOM_SRCS "app_main.c" "ota_custom_mock.c")
set(OTA_CUSTOM_PRIV_REQUIRES
    # RMNG SDK
    "esp_rmaker_neo"
    "esp_rmaker_neo_ota"
    # crypto / base64 / NVS utils used by the inlined custom OTA mock
    "esp_rmaker_neo_common"
    # Network (provisioning + the RMNG pre-provisioning bring-up)
    "app_network"
    "esp_wifi"
    # Event loop
    "app_event_loop"
    # Peripheral drivers
    "app_led"
    # Platform entry-point shim (provides app_main)
    "app_entry"
)

idf_component_register(SRCS ${OTA_CUSTOM_SRCS} PRIV_REQUIRES ${OTA_CUSTOM_PRIV_REQUIRES})
