include("${CMAKE_CURRENT_LIST_DIR}/deps_make_available.cmake")

include("${_RMNG_JOBS_SRC}/jobsFilePaths.cmake")

include("${_RMNG_COREJSON_SRC}/jsonFilePaths.cmake")

# OTA component
set(RMNG_OTA_NAME esp_rmaker_neo_ota)

set(RMNG_OTA_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/src/esp_rmaker_ota.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/ota_jobs.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/ota_status.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/ota_nvs.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/esp_rmaker_ota_error_reasons.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/esp_rmaker_ota_status_details.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/ota_timeout_handler.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/ota_filetype_handler_internal.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/ota_signature_verify.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/image/ota_image_handler.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/image/ota_image_progress.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/util/ota_partition.c"
    "${JOBS_SOURCES}"
    "${OTA_HANDLER_SOURCES}"
    "${JSON_SOURCES}"
)

set(RMNG_OTA_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")

set(RMNG_OTA_PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/priv_include" "${JOBS_INCLUDE_PUBLIC_DIRS}"
                               "${OTA_HANDLER_INCLUDES}" "${JSON_INCLUDE_PUBLIC_DIRS}"
)

if (CONFIG_RMNG_OTA_TRANSPORT_MQTT)
    include("${CMAKE_CURRENT_LIST_DIR}/mqtt_downloader.cmake")
    list(APPEND RMNG_OTA_SRCS "${RMNG_MQTT_DOWNLOADER_SRCS}")
    list(APPEND RMNG_OTA_PRIV_INCLUDE_DIRS "${RMNG_MQTT_DOWNLOADER_PRIV_INCLUDE_DIRS}")
    if (NOT ESP_PLATFORM)
        # Add tinyCBOR sources for POSIX builds
        include("${CMAKE_CURRENT_LIST_DIR}/tinycbor.cmake")
        list(APPEND RMNG_OTA_SRCS "${TINYCBOR_SOURCES}")
        list(APPEND RMNG_OTA_INCLUDE_DIRS "${TINYCBOR_INCLUDE_DIRS}")
    endif ()
endif ()
