set(SWITCH_SRCS "app_main.c" "app_driver.c")
set(SWITCH_PRIV_REQUIRES
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

idf_component_register(SRCS ${SWITCH_SRCS} PRIV_REQUIRES ${SWITCH_PRIV_REQUIRES})
