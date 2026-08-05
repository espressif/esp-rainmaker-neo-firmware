# Setup for using MQTT to download OTA files

# Ensure the AWS IoT dependency locations (_RMNG_MQTT_STREAMS_SRC) are defined regardless of include order (idempotent
# via include_guard in deps_make_available.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/deps_make_available.cmake")

include("${_RMNG_MQTT_STREAMS_SRC}/mqttFileDownloaderFilePaths.cmake")

set(RMNG_MQTT_DOWNLOADER_SRCS
    "${MQTT_FILE_DOWNLOADER_SOURCES}" "${CMAKE_CURRENT_LIST_DIR}/src/image/ota_mqtt_downloader_bitmap.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/image/ota_mqtt_downloader.c"
)
set(RMNG_MQTT_DOWNLOADER_INCLUDE_DIRS "${MQTT_FILE_DOWNLOADER_INCLUDES}")
set(RMNG_MQTT_DOWNLOADER_PRIV_INCLUDE_DIRS "${MQTT_FILE_DOWNLOADER_INCLUDES}"
                                           "${CMAKE_CURRENT_LIST_DIR}/config/mqtt_download"
)

# ignore compiler warnings for MqttFileDownloader
set_source_files_properties(
    "${_RMNG_MQTT_STREAMS_SRC}/source/MQTTFileDownloader_cbor.c" PROPERTIES COMPILE_FLAGS
                                                                            "-Wno-incompatible-pointer-types"
)
set_source_files_properties(
    "${_RMNG_MQTT_STREAMS_SRC}/source/MQTTFileDownloader.c" PROPERTIES COMPILE_FLAGS "-Wno-format"
)
