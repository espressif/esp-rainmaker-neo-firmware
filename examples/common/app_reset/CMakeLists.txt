set(APP_RESET_NAME "app_reset")

set(APP_RESET_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")

# ESP-IDF Platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS "${CMAKE_CURRENT_LIST_DIR}/src/esp/app_reset.c"
        INCLUDE_DIRS ${APP_RESET_INCLUDE_DIRS}
        # button is public: button_handle_t is part of app_reset.h. osal likewise, for osal_err_t.
        REQUIRES "button" "osal"
    )
    set(APP_RESET_NAME ${COMPONENT_TARGET})

    # The resets go through RMNG's system-control API and the network credential reset, neither of which appears in the
    # public header. Only needed when the feature is compiled in.
    if (CONFIG_APP_RESET_ENABLED)
        idf_component_optional_requires(PRIVATE esp_rmaker_neo app_network)
    endif ()

    # Normal CMake Platform ###
else ()
    # No physical button on the host: build the no-op stub against the shared interface header.
    add_library(${APP_RESET_NAME} STATIC "${CMAKE_CURRENT_LIST_DIR}/src/posix/app_reset_posix.c")
    target_include_directories(${APP_RESET_NAME} PUBLIC ${APP_RESET_INCLUDE_DIRS})
    if (NOT TARGET osal)
        message(FATAL_ERROR "Target 'osal' not found. Ensure it is added before this component (app_reset).")
    endif ()
    target_link_libraries(${APP_RESET_NAME} PUBLIC osal)
endif ()
