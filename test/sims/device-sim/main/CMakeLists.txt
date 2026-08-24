set(DEVICE_SIM_SRCS "app_main.c")
set(DEVICE_SIM_PRIV_REQUIRES
    # RainMaker Neo SDK (provides the remote control)
    "esp_rmaker_neo"
    # Network (ESP-only bring-up + RainMaker Neo provisioning)
    "app_network"
    "esp_wifi"
    # Platform entry-point shim (provides app_main)
    "app_entry"
)

idf_component_register(SRCS ${DEVICE_SIM_SRCS} PRIV_REQUIRES ${DEVICE_SIM_PRIV_REQUIRES})
