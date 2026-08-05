set(APP_NETWORK_NAME "app_network")

set(APP_NETWORK_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")

# ESP-IDF Platform ###
if (ESP_PLATFORM)
    set(srcs "src/app_network_neo.c" "src/app_network_provision.c")
    set(pub_req rmaker_app_network esp_event osal)
    # app_network_provision.c drives the RMNG pre-provisioning flow (esp_rmaker_flow.h) and the provisioning manager;
    # neither appears in the public header, so both are private.
    set(priv_req esp_rmaker_neo network_provisioning)

    if (CONFIG_APP_NETWORK_OVER_WIFI)
        list(APPEND opt_priv_req "esp_wifi")
        list(APPEND srcs "src/wifi_internal.c")
    endif ()

    if (CONFIG_APP_NETWORK_OVER_THREAD)
        list(APPEND opt_priv_req "openthread")
        list(APPEND srcs "src/thread_internal.c")
    endif ()

    idf_component_register(
        SRCS ${srcs}
        INCLUDE_DIRS "include"
        REQUIRES ${pub_req}
        PRIV_REQUIRES ${priv_req}
    )
    set(APP_NETWORK_NAME ${COMPONENT_TARGET})

    # network_provisioning/manager.h pulls openthread headers on Thread-capable targets, but only lists openthread in
    # its own PRIV_REQUIRES.
    if (CONFIG_NETWORK_PROV_NETWORK_TYPE_THREAD)
        list(APPEND opt_priv_req "openthread")
    endif ()

    if (opt_priv_req)
        idf_component_optional_requires(PRIVATE ${opt_priv_req})
    endif ()

    # Normal CMake Platform ###
else ()
    # No network provisioning on the host: build the no-op stub against the same public header the ESP sources
    # implement, so example code calls app_network_provision() unconditionally on both platforms.
    add_library(${APP_NETWORK_NAME} STATIC "${CMAKE_CURRENT_LIST_DIR}/src/posix/app_network_posix.c")
    target_include_directories(${APP_NETWORK_NAME} PUBLIC ${APP_NETWORK_INCLUDE_DIRS})
    if (NOT TARGET osal)
        message(FATAL_ERROR "Target 'osal' not found. Ensure it is added before this component (app_network).")
    endif ()
    target_link_libraries(${APP_NETWORK_NAME} PUBLIC osal)
endif ()
