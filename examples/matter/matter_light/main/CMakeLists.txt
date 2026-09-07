set(MATTER_LIGHT_SRCS "app_main.c" "app_driver.c")
set(MATTER_LIGHT_PRIV_REQUIRES
    # RainMaker Neo SDK
    "esp_rmaker_neo"
    "esp_rmaker_neo_ota"
    "esp_rmaker_neo_matter_mirror"
    # Network (provisioning + the RainMaker Neo pre-provisioning bring-up)
    "app_network"
    "esp_wifi"
    # Event loop
    "app_event_loop"
    # Peripheral drivers
    "app_led"
    "app_button"
    # Matter Identify effect patterns
    "app_matter_effects"
    # Platform entry-point shim (provides app_main)
    "app_entry"
)

idf_component_register(SRCS ${MATTER_LIGHT_SRCS} PRIV_REQUIRES ${MATTER_LIGHT_PRIV_REQUIRES})
