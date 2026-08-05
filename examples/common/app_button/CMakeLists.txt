set(APP_BUTTON_NAME "app_button")

set(APP_BUTTON_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")

# ESP-IDF Platform ###
if (ESP_PLATFORM)
    set(APP_BUTTON_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/esp/app_button.c")
    # osal_err.h is part of the public app_button.h surface, so osal stays public. app_reset is used only by
    # app_button.c and appears nowhere in the public header, so it is private -- the same public/private split
    # app_network makes.
    set(APP_BUTTON_REQUIRES "button" "osal")
    set(APP_BUTTON_PRIV_REQUIRES "app_reset")

    idf_component_register(
        SRCS ${APP_BUTTON_SRCS}
        INCLUDE_DIRS ${APP_BUTTON_INCLUDE_DIRS}
        REQUIRES ${APP_BUTTON_REQUIRES}
        PRIV_REQUIRES ${APP_BUTTON_PRIV_REQUIRES}
    )
    set(APP_BUTTON_NAME ${COMPONENT_TARGET})

    # Normal CMake Platform ###
else ()
    # No physical button on the host: build the no-op stub against the shared interface header.
    add_library(${APP_BUTTON_NAME} STATIC "${CMAKE_CURRENT_LIST_DIR}/src/posix/app_button_posix.c")
    target_include_directories(${APP_BUTTON_NAME} PUBLIC ${APP_BUTTON_INCLUDE_DIRS})
    foreach (LIBRARY osal app_reset)
        if (NOT TARGET ${LIBRARY})
            message(FATAL_ERROR "Target '${LIBRARY}' not found. Ensure it is added before this component (app_button).")
        endif ()
    endforeach ()
    # app_reset is PRIVATE for the same reason as PRIV_REQUIRES above; a static library still forwards it at link time,
    # so consumers need no change.
    target_link_libraries(
        ${APP_BUTTON_NAME}
        PUBLIC osal
        PRIVATE app_reset
    )
endif ()
