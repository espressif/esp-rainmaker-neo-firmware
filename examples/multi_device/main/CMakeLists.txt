set(MULTI_SRCS "app_main.c" "app_driver.c" "fan_sim.c" "temp_sim.c")
set(MULTI_PRIV_REQUIRES
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

idf_component_register(SRCS ${MULTI_SRCS} PRIV_REQUIRES ${MULTI_PRIV_REQUIRES})
