set(CONTROLLER_SRCS "app_main.c" "controller_cli.c")

set(CONTROLLER_PRIV_REQUIRES
    # RainMaker Neo SDK
    "esp_rmaker_neo"
    "esp_rmaker_neo_ota"
    # osal: error strings and the esp_console API
    "osal"
    # Network (provisioning + network start)
    "app_network"
    "esp_wifi"
    # Event loop
    "app_event_loop"
    # Platform entry-point shim (provides app_main)
    "app_entry"
    # esp_console: native here, an osal shim on POSIX
    "console"
)

idf_component_register(SRCS ${CONTROLLER_SRCS} PRIV_REQUIRES ${CONTROLLER_PRIV_REQUIRES})
