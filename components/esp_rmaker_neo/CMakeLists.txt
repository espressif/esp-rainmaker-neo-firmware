include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

# Add pre-processor checks to enforce minimum stack sizes for the SDK.
list(APPEND RMNG_SRCS ${RMNG_CHECKS_SRCS})

# Normal CMake Platform ###
if (NOT ESP_PLATFORM)
    # Register esp_rmaker_neo and all children Kconfigs
    include(${CMAKE_CURRENT_LIST_DIR}/kconfig_reg.cmake)

    # Configure CMake variables from the config file
    rmng_menuconfig_configure_cmake_variables(
        COMPONENT ${RMNG_NAME}
        DEFAULTS_FILE ${SDKCONFIG_DEFAULTS}
        OUTPUT_VARIABLE rmng_sdkconfig_cmake_file
    )

    # Include the generated cmake config file in global scope
    if (rmng_sdkconfig_cmake_file)
        include(${rmng_sdkconfig_cmake_file})
    endif ()
endif ()

# Get TEST_RMNG value from RMNG_TESTING
if (NOT DEFINED TEST_RMNG)
    set(TEST_RMNG OFF)
endif ()
if (DEFINED CONFIG_RMNG_TESTING)
    set(TEST_RMNG ${CONFIG_RMNG_TESTING})
endif ()
message(STATUS "TEST_RMNG: ${TEST_RMNG}")

if (TEST_RMNG AND NOT ESP_PLATFORM)
    # Set up testing early
    if (TEST_RMNG)
        # Use virtual scheduler
        set(OSAL_USING_VIRTUAL_SCHEDULER ON)

        include(${CMAKE_CURRENT_LIST_DIR}/../../third_party/install_scripts/unity_make_available.cmake)
        unity_make_available()

        # Enable testing
        enable_testing()
    endif ()
endif ()

# Add remote control support
if (NOT DEFINED RMNG_HOST_CTRL)
    set(RMNG_HOST_CTRL OFF)
endif ()
if (DEFINED CONFIG_RMNG_HOST_CTRL)
    set(RMNG_HOST_CTRL ${CONFIG_RMNG_HOST_CTRL})
endif ()
message(STATUS "RMNG_HOST_CTRL: ${RMNG_HOST_CTRL}")
if (RMNG_HOST_CTRL)
    message(STATUS "Adding RMNG remote support")

    # Add sources and include directories
    list(APPEND RMNG_SRCS ${RMNG_HOST_CTRL_SRCS})
    list(APPEND RMNG_PRIV_INCLUDE_DIRS ${RMNG_HOST_CTRL_PRIV_INCLUDE_DIRS})

    # Use virtual scheduler and include osal/ext-io
    set(OSAL_USING_VIRTUAL_SCHEDULER ON)
    set(OSAL_INCLUDE_EXT_IO ON)
else ()
    # include all necessary stubs
    list(APPEND RMNG_PRIV_INCLUDE_DIRS ${RMNG_PRIV_STUB_HOST_CTRL_INCLUDE_DIR})
endif ()

# Add heap monitor support
if (NOT DEFINED RMNG_HEAP_MONITORING)
    set(RMNG_HEAP_MONITORING OFF)
endif ()
if (DEFINED CONFIG_RMNG_HEAP_MONITORING)
    set(RMNG_HEAP_MONITORING ${CONFIG_RMNG_HEAP_MONITORING})
endif ()
message(STATUS "RMNG_HEAP_MONITORING: ${RMNG_HEAP_MONITORING}")

if (RMNG_HEAP_MONITORING)
    # Use heap monitor
    set(OSAL_INCLUDE_HEAP_MONITOR ON)
endif ()

# Add MQTT budgeting support
if (NOT DEFINED RMNG_MQTT_ENABLE_BUDGETING)
    set(RMNG_MQTT_ENABLE_BUDGETING OFF)
endif ()
if (DEFINED CONFIG_ESP_RMAKER_MQTT_ENABLE_BUDGETING)
    set(RMNG_MQTT_ENABLE_BUDGETING ${CONFIG_ESP_RMAKER_MQTT_ENABLE_BUDGETING})
endif ()
message(STATUS "RMNG_MQTT_ENABLE_BUDGETING: ${RMNG_MQTT_ENABLE_BUDGETING}")

if (RMNG_MQTT_ENABLE_BUDGETING)
    list(APPEND RMNG_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/network/mqtt_budget.c")
endif ()

# Add data model support
list(APPEND RMNG_SRCS ${RMNG_DATA_MODEL_SRCS})
list(APPEND RMNG_INCLUDE_DIRS ${RMNG_DATA_MODEL_INCLUDE_DIRS})
list(APPEND RMNG_PRIV_INCLUDE_DIRS ${RMNG_DATA_MODEL_PRIV_INCLUDE_DIRS})

# Add bridge support
if (CONFIG_RMNG_BRIDGE_ENABLED)
    if (RMNG_HOST_CTRL)
        # Bridge remote handlers. Gated here rather than alongside the other host_ctrl sources because
        # CONFIG_RMNG_BRIDGE_ENABLED is not readable until the generated sdkconfig is included above.
        list(APPEND RMNG_SRCS ${CMAKE_CURRENT_LIST_DIR}/src/host_ctrl/bridge_handlers.c)
    endif ()
    list(
        APPEND
        RMNG_SRCS
        ${CMAKE_CURRENT_LIST_DIR}/src/bridge/esp_rmaker_bridge.c
        ${CMAKE_CURRENT_LIST_DIR}/src/bridge/bridge_events.c
        ${CMAKE_CURRENT_LIST_DIR}/src/bridge/bridge_subscriber.c
        ${CMAKE_CURRENT_LIST_DIR}/src/bridge/bridge_child_nvs.c
        ${CMAKE_CURRENT_LIST_DIR}/src/bridge/bridge_child_triggers_nvs.c
        ${CMAKE_CURRENT_LIST_DIR}/src/bridge/bridge_child_scheds_nvs.c
        ${CMAKE_CURRENT_LIST_DIR}/src/node/node_config_pending.c
    )
endif ()

# ESP-IDF Platform ###
if (ESP_PLATFORM)

    # Add ESP remote source
    if (RMNG_HOST_CTRL)
        list(APPEND RMNG_SRCS ${CMAKE_CURRENT_LIST_DIR}/src/host_ctrl/host_ctrl_esp.c)
    endif ()
    list(APPEND RMNG_SRCS ${CMAKE_CURRENT_LIST_DIR}/src/prov_helpers/prov_helpers_esp.c)
    list(APPEND RMNG_PRIV_REQUIRES network_provisioning)

    idf_component_register(
        SRCS ${RMNG_SRCS}
        REQUIRES ${RMNG_REQUIRES}
        PRIV_REQUIRES ${RMNG_PRIV_REQUIRES}
        INCLUDE_DIRS ${RMNG_INCLUDE_DIRS}
        PRIV_INCLUDE_DIRS ${RMNG_PRIV_INCLUDE_DIRS}
    )

    # No esp_schedule wiring here: the managed component registers itself and the osal port is installed at runtime via
    # esp_schedule_init_with_config(), so nothing has to be injected into its target at build time.

    if (CONFIG_NETWORK_PROV_NETWORK_TYPE_THREAD)
        idf_component_optional_requires(PRIVATE openthread)
    endif ()

    # Linker flags for socket mocks
    if (RMNG_HOST_CTRL)
        idf_component_optional_requires(PRIVATE socket_mocks)
        target_link_options(
            ${COMPONENT_LIB} PUBLIC "-Wl,--wrap=lwip_connect" "-Wl,--wrap=lwip_send" "-Wl,--wrap=lwip_recv"
        )
    endif ()
    # Normal CMake Platform ###
else ()
    list(APPEND RMNG_SRCS ${CMAKE_CURRENT_LIST_DIR}/src/prov_helpers/prov_helpers_posix.c)

    # Add POSIX remote source
    if (RMNG_HOST_CTRL)
        list(APPEND RMNG_SRCS ${CMAKE_CURRENT_LIST_DIR}/src/host_ctrl/host_ctrl_posix.c)

        # Add socket mocks support
        list(APPEND RMNG_PRIV_REQUIRES socket_mocks)
    endif ()

    # Add sources and include directories
    add_library(${RMNG_NAME} STATIC ${RMNG_SRCS})

    target_include_directories(${RMNG_NAME} PUBLIC ${RMNG_INCLUDE_DIRS})
    target_include_directories(${RMNG_NAME} PRIVATE ${RMNG_PRIV_INCLUDE_DIRS})

    # Testing remainder setup
    if (TEST_RMNG)
        include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/testing.cmake)
        add_subdirectory(test_rmaker_neo)
        testing_enable_coverage(esp_rmaker_neo)
        get_filename_component(RMNG_SDK_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
        testing_add_gcovr_coverage_target(
            NAME rmng_coverage
            ROOT_DIR "${RMNG_SDK_REPO_ROOT}"
            TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}"
            OUTPUT_DIR "${CMAKE_BINARY_DIR}/rmng-coverage"
            FILTERS "^components/esp_rmaker_neo(/|$)"
        )
    endif ()

    # Add dependencies. esp_rmaker_neo_common also adds osal.
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../esp_rmaker_neo_common ${CMAKE_BINARY_DIR}/lib/esp_rmaker_neo_common)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/overrides/esp_schedule ${CMAKE_BINARY_DIR}/overrides/esp_schedule)
    if (RMNG_CONSOLE_ENABLED)
        add_subdirectory(
            ${CMAKE_CURRENT_LIST_DIR}/overrides/rmaker_console ${CMAKE_BINARY_DIR}/overrides/rmaker_console
        )
    endif ()
    if (RMNG_HOST_CTRL)
        add_subdirectory(
            ${CMAKE_CURRENT_LIST_DIR}/fault_injection/socket_mocks ${CMAKE_BINARY_DIR}/fault_injection/socket_mocks
        )
        if (NOT APPLE)
            # NOTE: These flags must propagate to the EXECUTABLE that links this library
            target_link_options(
                ${RMNG_NAME}
                PUBLIC
                "LINKER:--wrap=connect"
                "LINKER:--wrap=send"
                "LINKER:--wrap=recv"
                "LINKER:--whole-archive"
                "$<TARGET_OBJECTS:socket_mocks>"
                "LINKER:--no-whole-archive"
            )
        endif ()
    endif ()

    # Setup menuconfig and config includes
    rmng_setup_menuconfig(COMPONENT ${RMNG_NAME})

    # Add third-party libraries (without unit tests). The install scripts fetch the sources when needed, so there is no
    # ordering dependency on another consumer having fetched them first.
    include(${CMAKE_CURRENT_LIST_DIR}/../../third_party/install_scripts/mbedtls.cmake)
    mbedtls_make_available()
    include(${CMAKE_CURRENT_LIST_DIR}/../../third_party/install_scripts/protobuf-c.cmake)
    protobuf_c_make_available()

    # Link dependencies
    target_link_libraries(${RMNG_NAME} PUBLIC ${RMNG_REQUIRES})
    target_link_libraries(${RMNG_NAME} PRIVATE ${RMNG_PRIV_REQUIRES})
endif ()
