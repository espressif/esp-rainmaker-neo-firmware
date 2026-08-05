set(TEMP_SENSOR_SRCS "app_main.c" "app_driver.c" "temp_sim.c")
set(TEMP_SENSOR_PRIV_REQUIRES
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
    # Platform entry-point shim (provides app_main)
    "app_entry"
)

idf_component_register(SRCS ${TEMP_SENSOR_SRCS} PRIV_REQUIRES ${TEMP_SENSOR_PRIV_REQUIRES})
