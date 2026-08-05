cmake_minimum_required(VERSION 3.16)

# Define the firmware version (done before project())
set(PROJECT_VER
    "1.0.0"
    CACHE STRING "Firmware version"
)

# ESP-IDF Platform ###
if (ESP_PLATFORM)
    # Force minimal build
    set(COMPONENTS main)

    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(ota-sim)

    # Normal CMake Platform ###
else ()
    project(ota-sim C ASM)

    add_executable(${PROJECT_NAME} main/app_main.c)

    set(RMNG_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

    # Layer the POSIX defaults (OTA transport/resume, coreMQTT) under any -DSDKCONFIG_DEFAULTS the caller (e.g. the
    # itest harness) prepends, so the caller's values win. Must precede add_subdirectory(esp_rmaker_neo), which reads
    # it.
    list(PREPEND SDKCONFIG_DEFAULTS ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.posix)

    # esp_rmaker_neo / esp_rmaker_neo_ota first: esp_rmaker_neo transitively adds osal; esp_rmaker_neo_ota exports
    # RMNG_OTA_POSIX_* to this scope. app_entry FATAL_ERRORs unless esp_rmaker_neo exists.
    add_subdirectory(${RMNG_ROOT}/components/esp_rmaker_neo components/esp_rmaker_neo)
    add_subdirectory(${RMNG_ROOT}/components/esp_rmaker_neo_ota components/esp_rmaker_neo_ota)
    add_subdirectory(${RMNG_ROOT}/examples/common/app_entry components/app_entry)

    target_link_libraries(${PROJECT_NAME} PRIVATE esp_rmaker_neo esp_rmaker_neo_ota app_entry)

    # Run the mock bootloader as ${PROJECT_NAME}; drop the app into the partitions folder as the first partition image
    # (from esp_rmaker_neo_ota, PARENT_SCOPE).
    set_target_properties(${RMNG_OTA_POSIX_BOOTLOADER_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    set_target_properties(
        ${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${RMNG_OTA_POSIX_PARTITION_FOLDER}
                                   OUTPUT_NAME ${RMNG_OTA_POSIX_DEFAULT_FIRST_PARTITION}
    )
endif ()
