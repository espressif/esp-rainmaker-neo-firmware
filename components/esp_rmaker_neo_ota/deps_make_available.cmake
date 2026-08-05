# Locates Jobs-for-AWS-IoT, coreJSON, and aws-iot-core-mqtt-file-streams (used by esp_rmaker_neo_ota on ESP-IDF and
# POSIX) inside the esp-aws-iot checkout, exporting _RMNG_JOBS_SRC / _RMNG_COREJSON_SRC / _RMNG_MQTT_STREAMS_SRC. Safe
# to include from any consumer (sources.cmake, mqtt_downloader.cmake); include_guard keeps it running once per directory
# scope.

include_guard(DIRECTORY)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/esp_aws_iot.cmake")

set(_RMNG_JOBS_SRC "${RMNG_ESP_AWS_IOT_LIBS_DIR}/Jobs-for-AWS-IoT-embedded-sdk/Jobs-for-AWS-IoT-embedded-sdk")
set(_RMNG_COREJSON_SRC "${RMNG_ESP_AWS_IOT_LIBS_DIR}/coreJSON/coreJSON")
set(_RMNG_MQTT_STREAMS_SRC
    "${RMNG_ESP_AWS_IOT_LIBS_DIR}/aws-iot-core-mqtt-file-streams-embedded-c/aws-iot-core-mqtt-file-streams-embedded-c"
)

message(STATUS "rmng-sdk: Using esp_rmaker_neo_ota AWS IoT dependencies from esp-aws-iot (${RMNG_ESP_AWS_IOT_DIR})")
