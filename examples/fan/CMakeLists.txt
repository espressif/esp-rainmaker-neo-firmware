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
    project(fan)

    # Normal CMake Platform ###
else ()
    project(fan C ASM)

    add_executable(${PROJECT_NAME} main/app_main.c main/app_driver.c main/fan_sim.c)

    set(RMNG_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

    # ~~~
    # Order matters:
    #   - esp_rmaker_neo / esp_rmaker_neo_ota first: esp_rmaker_neo transitively adds
    #     osal and esp_rmaker_neo_ota exports RMNG_OTA_POSIX_* to this scope.
    #   - app_event_loop next: it FATAL_ERRORs unless the esp_rmaker_neo/esp_rmaker_neo_ota
    #     targets exist.
    #   - app_entry / app_led / app_button / app_network last: they depend on osal targets.
    # ~~~
    add_subdirectory(${RMNG_ROOT}/components/esp_rmaker_neo components/esp_rmaker_neo)
    add_subdirectory(${RMNG_ROOT}/components/esp_rmaker_neo_ota components/esp_rmaker_neo_ota)
    add_subdirectory(${RMNG_ROOT}/examples/common/app_event_loop components/app_event_loop)
    add_subdirectory(${RMNG_ROOT}/examples/common/app_entry components/app_entry)
    add_subdirectory(${RMNG_ROOT}/examples/common/app_led components/app_led)
    add_subdirectory(${RMNG_ROOT}/examples/common/app_reset components/app_reset)
    add_subdirectory(${RMNG_ROOT}/examples/common/app_button components/app_button)
    add_subdirectory(${RMNG_ROOT}/examples/common/app_network components/app_network)

    set(FAN_LIBS
        esp_rmaker_neo
        esp_rmaker_neo_ota
        app_event_loop
        app_entry
        app_led
        app_button
        app_network
    )
    if (TARGET osal)
        list(APPEND FAN_LIBS osal)
    endif ()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FAN_LIBS})

    # Rename the bootloader to match the project name
    set_target_properties(${RMNG_OTA_POSIX_BOOTLOADER_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    # Build the main application directly to the partitions directory as the initial partition image
    set_target_properties(
        ${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${RMNG_OTA_POSIX_PARTITION_FOLDER}
                                   OUTPUT_NAME ${RMNG_OTA_POSIX_DEFAULT_FIRST_PARTITION}
    )
endif ()
