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
    project(ota-custom)

    # Normal CMake Platform ###
else ()
    project(ota-custom C ASM)

    # The custom OTA mock (custom-filetype handler + custom job callback) is inlined into this example's main sources.
    add_executable(${PROJECT_NAME} main/app_main.c main/ota_custom_mock.c)

    set(RMNG_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

    # ~~~
    # Order matters:
    #   - esp_rmaker_neo / esp_rmaker_neo_ota first: esp_rmaker_neo transitively adds
    #     osal and esp_rmaker_neo_common, and esp_rmaker_neo_ota exports
    #     RMNG_OTA_POSIX_* to this scope.
    #   - app_event_loop next: FATAL_ERRORs unless the esp_rmaker_neo/esp_rmaker_neo_ota
    #     targets exist.
    #   - app_entry / app_led / app_network last: they depend on osal targets.
    # ~~~
    add_subdirectory(${RMNG_ROOT}/components/esp_rmaker_neo components/esp_rmaker_neo)
    add_subdirectory(${RMNG_ROOT}/components/esp_rmaker_neo_ota components/esp_rmaker_neo_ota)
    add_subdirectory(${RMNG_ROOT}/examples/common/app_event_loop components/app_event_loop)
    add_subdirectory(${RMNG_ROOT}/examples/common/app_entry components/app_entry)
    add_subdirectory(${RMNG_ROOT}/examples/common/app_led components/app_led)
    add_subdirectory(${RMNG_ROOT}/examples/common/app_network components/app_network)

    # esp_rmaker_neo_common provides the crypto / base64 / NVS utils the inlined mock uses.
    set(OTA_CUSTOM_LIBS
        esp_rmaker_neo
        esp_rmaker_neo_ota
        esp_rmaker_neo_common
        app_event_loop
        app_entry
        app_led
        app_network
    )
    if (TARGET osal)
        list(APPEND OTA_CUSTOM_LIBS osal)
    endif ()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OTA_CUSTOM_LIBS})

    # Rename the bootloader to match the project name
    set_target_properties(${RMNG_OTA_POSIX_BOOTLOADER_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    # Build the main application directly to the partitions directory as the initial partition image
    set_target_properties(
        ${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${RMNG_OTA_POSIX_PARTITION_FOLDER}
                                   OUTPUT_NAME ${RMNG_OTA_POSIX_DEFAULT_FIRST_PARTITION}
    )
endif ()
