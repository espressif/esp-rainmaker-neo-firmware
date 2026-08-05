set(LIGHT_SRCS "app_main.c" "app_driver.c")
set(LIGHT_PRIV_REQUIRES
    # RMNG SDK
    "esp_rmaker_neo"
    "esp_rmaker_neo_ota"
    # Network (provisioning + the RMNG pre-provisioning bring-up)
    "app_network"
    "esp_wifi"
    # Event loop
    "app_event_loop"
    # Peripheral drivers
    "app_led"
    "app_button"
    # Platform entry-point shim (provides app_main)
    "app_entry"
)

idf_component_register(SRCS ${LIGHT_SRCS} PRIV_REQUIRES ${LIGHT_PRIV_REQUIRES})
