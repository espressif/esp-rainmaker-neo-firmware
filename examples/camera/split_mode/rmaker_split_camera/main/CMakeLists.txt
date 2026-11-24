idf_component_register(
    SRCS "app_main.c" "power_save_handler.c"
         # From esp_hosted 3.x: CP light sleep ships as example code, not as part of the component.
         "cp_light_sleep_controller.c" "host_ps_integration.c"
    PRIV_REQUIRES
        # RainMaker Neo SDK
        "esp_rmaker_neo"
        "esp_rmaker_neo_common"
        # Network (provisioning + network start)
        "app_network"
        "esp_wifi"
        # Event loop
        "app_event_loop"
        # Platform entry-point shim (provides app_main)
        "app_entry"
        # Camera glue + the bridge to the streaming chip
        "rmaker_camera"
        "esp_webrtc_utils"
        "app_webrtc"
        "signaling_bridge_adapter"
        "webrtc_bridge"
        # This chip is the esp_hosted co-processor of the streaming P4
        "esp_hosted"
        "esp_pm"
        # esp_hosted links its SDIO slave transport against this without declaring it
        "esp_driver_sdio"
        # NETWORK_PROV_END, for the C5 BLE clock release
        "network_provisioning"
)
