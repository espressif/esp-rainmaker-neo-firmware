# RainMaker camera glue for the RainMaker Neo SDK: camera device + KVS WebRTC signaling bring-up with
# IoT-Credential-Provider credentials. Shared by the standalone (P4) and split-mode (C6) camera examples. ESP-IDF only:
# the KVS media plane has no POSIX port.

idf_component_register(
    SRCS "src/rmaker_camera.c" "src/credentials_provider.c" "src/rmaker_aws_credentials.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_rmaker_neo espressif__rmaker_aws_sign
    PRIV_REQUIRES esp_rmaker_neo_common
)
