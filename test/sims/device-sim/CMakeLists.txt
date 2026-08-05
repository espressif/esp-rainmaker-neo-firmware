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
    project(device-sim)

    # Normal CMake Platform ###
else ()
    project(device-sim C ASM)

    add_executable(${PROJECT_NAME} main/app_main.c)

    set(RMNG_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

    # Layer the POSIX defaults (RMNG remote on, console off, coreMQTT) under any -DSDKCONFIG_DEFAULTS the caller (e.g.
    # the itest harness) prepends, so the caller's values win. Must precede add_subdirectory(esp_rmaker_neo), which
    # reads it.
    list(PREPEND SDKCONFIG_DEFAULTS ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.posix)

    # esp_rmaker_neo first: it transitively adds osal (and its internal subtargets). app_entry FATAL_ERRORs unless
    # esp_rmaker_neo exists.
    add_subdirectory(${RMNG_ROOT}/components/esp_rmaker_neo components/esp_rmaker_neo)
    add_subdirectory(${RMNG_ROOT}/examples/common/app_entry components/app_entry)

    target_link_libraries(${PROJECT_NAME} PRIVATE esp_rmaker_neo app_entry)
endif ()
