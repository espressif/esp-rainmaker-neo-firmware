idf_component_register(
    SRCS "app_main.c"
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
        # Camera glue + KVS WebRTC media plane
        "rmaker_camera"
        "kvs_webrtc"
        "media_stream"
        "esp_webrtc_utils"
        "app_webrtc"
)
