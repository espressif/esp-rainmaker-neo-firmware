set(FAN_SRCS "app_main.c" "app_driver.c" "fan_sim.c")
set(FAN_PRIV_REQUIRES
    # RainMaker Neo SDK
    "esp_rmaker_neo"
    "esp_rmaker_neo_ota"
    # Network (provisioning + the RainMaker Neo pre-provisioning bring-up)
    "app_network"
    # Event loop
    "app_event_loop"
    # Peripheral drivers
    "app_led"
    "app_button"
    # Platform entry-point shim (provides app_main)
    "app_entry"
)

idf_component_register(SRCS ${FAN_SRCS} PRIV_REQUIRES ${FAN_PRIV_REQUIRES})
