set(OTA_SIM_SRCS "app_main.c")
set(OTA_SIM_PRIV_REQUIRES
    # RMNG SDK + OTA
    "esp_rmaker_neo"
    "esp_rmaker_neo_ota"
    # Network (ESP-only bring-up + RMNG provisioning)
    "app_network"
    "esp_wifi"
    # Platform entry-point shim (provides app_main)
    "app_entry"
)

idf_component_register(SRCS ${OTA_SIM_SRCS} PRIV_REQUIRES ${OTA_SIM_PRIV_REQUIRES})
