set(APP_LED_NAME "app_led")

set(APP_LED_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")
set(APP_LED_PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/priv_include")

# ESP-IDF Platform ###
if (ESP_PLATFORM)
    set(APP_LED_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/esp/common.c")
    set(APP_LED_REQUIRES "osal") # osal_err.h is part of the public app_led.h surface
    set(APP_LED_PRIV_REQUIRES "led_strip" "esp_driver_ledc")

    # WS2812 ###
    if (CONFIG_APP_LED_TYPE_WS2812)
        list(APPEND APP_LED_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/esp/ws2812_impl.c")

        # LEDC ###
    elseif (CONFIG_APP_LED_TYPE_LEDC)
        list(APPEND APP_LED_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/esp/ledc_impl.c")
    endif ()

    idf_component_register(
        SRCS ${APP_LED_SRCS}
        INCLUDE_DIRS ${APP_LED_INCLUDE_DIRS}
        PRIV_INCLUDE_DIRS ${APP_LED_PRIV_INCLUDE_DIRS}
        REQUIRES ${APP_LED_REQUIRES}
        PRIV_REQUIRES ${APP_LED_PRIV_REQUIRES}
    )
    set(APP_LED_NAME ${COMPONENT_TARGET})

    # Normal CMake Platform ###
else ()
    # No physical LED on the host: build the no-op stub against the shared interface header.
    add_library(${APP_LED_NAME} STATIC "${CMAKE_CURRENT_LIST_DIR}/src/posix/app_led_posix.c")
    target_include_directories(${APP_LED_NAME} PUBLIC ${APP_LED_INCLUDE_DIRS})
    if (NOT TARGET osal)
        message(FATAL_ERROR "Target 'osal' not found. Ensure it is added before this component (app_led).")
    endif ()
    target_link_libraries(${APP_LED_NAME} PUBLIC osal)
endif ()
