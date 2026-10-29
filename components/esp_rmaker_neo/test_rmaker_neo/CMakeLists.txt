# include internal RMNG headers for testing of internal components
include(${CMAKE_CURRENT_LIST_DIR}/../sources.cmake)

set(TEST_ESP_RMAKER_NEO_SRCS
    # All test sources
    "tests_src/test_node_model.c"
    "tests_src/test_introspect.c"
    "tests_src/test_timeseries.c"
    "tests_src/chal_resp/test_pb_c.c"
    "tests_src/local_ctrl/test_local_ctrl_endpoints.c"
    "tests_src/local_ctrl/test_local_ctrl_sec2_cache.c"
    "tests_src/claim/test_claim.c"
    "tests_src/credentials/test_migration.c"
    "tests_src/util/test_checksum_impl.c"
    "tests_src/util/test_trigger_codec.c"
    "tests_src/core/test_core_pre_prov.c"
    "tests_src/core/test_core_node_lifecycle.c"
    "tests_src/core/test_core_start_stop.c"
    "tests_src/core/test_core_online_status.c"
    "tests_src/core/test_core_sign_challenge.c"
    "tests_src/test_core.c"
    "tests_src/test_mqtt_topics.c"
    "tests_src/test_local_config.c"
    "tests_src/test_val.c"
    "tests_src/test_state_changes.c"
    "tests_src/test_state_changes_get_all.c"
    "tests_src/test_state_changes_subgroup.c"
    "tests_src/test_event_loop.c"
    "tests_src/network/test_network_common.c"
    "tests_src/network/test_notify.c"
    "tests_src/cloud/test_cloud_event_timesync.c"
    "tests_src/cloud/test_cloud_manager_inbox.c"
    "tests_src/cloud/test_cloud_event_server_config.c"
    "tests_src/services/test_schedules.c"
    "tests_src/services/test_schedule_port.c"
    "tests_src/services/test_automation.c"
    # Test suite
    "test_all.c"
)
set(TEST_ESP_RMAKER_NEO_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}" ${RMNG_DATA_MODEL_INCLUDE_DIRS})
set(TEST_ESP_RMAKER_NEO_PRIV_INCLUDE_DIRS
    ${RMNG_PRIV_INCLUDE_DIRS} ${RMNG_DATA_MODEL_PRIV_INCLUDE_DIRS} ${RMNG_PRIV_STUB_HOST_CTRL_INCLUDE_DIR}
    ${RMNG_SRC_DIR} # Allow access to internal RMNG sources for testing
)
set(TEST_ESP_RMAKER_NEO_PRIV_REQUIRES ${RMNG_NAME} ${RMNG_PRIV_REQUIRES})

if (CONFIG_ESP_RMAKER_NEO_BRIDGE_ENABLE)
    list(APPEND TEST_ESP_RMAKER_NEO_SRCS "tests_src/test_bridge.c" "tests_src/test_bridge_child_nvs.c"
         "tests_src/test_bridge_accessors.c" "tests_src/test_node_config_pending.c"
    )
endif ()

# ESP-IDF platform ###
if (ESP_PLATFORM)
    # esp_rmaker_neo's own dirs (priv_include, stubs, src) are attached after register: IDF's component validation
    # inspects register-time properties only, and would otherwise warn about this component borrowing them.
    idf_component_register(
        SRCS ${TEST_ESP_RMAKER_NEO_SRCS}
        INCLUDE_DIRS "."
        PRIV_REQUIRES ${TEST_ESP_RMAKER_NEO_PRIV_REQUIRES} unity
    )
    target_include_directories(${COMPONENT_LIB} PUBLIC ${RMNG_DATA_MODEL_INCLUDE_DIRS})
    target_include_directories(${COMPONENT_LIB} PRIVATE ${TEST_ESP_RMAKER_NEO_PRIV_INCLUDE_DIRS})

    # Normal CMake platform ###
else ()
    set(TEST_ESP_RMAKER_NEO_TARGET esp_rmaker_neo_tests)
    add_executable(${TEST_ESP_RMAKER_NEO_TARGET} ${TEST_ESP_RMAKER_NEO_SRCS} test_runner_posix.c)
    target_include_directories(${TEST_ESP_RMAKER_NEO_TARGET} PUBLIC ${TEST_ESP_RMAKER_NEO_INCLUDE_DIRS})
    target_include_directories(${TEST_ESP_RMAKER_NEO_TARGET} PRIVATE ${TEST_ESP_RMAKER_NEO_PRIV_INCLUDE_DIRS})
    # The migration flow tests stage OTA slot states, which only the POSIX shim exposes.
    target_include_directories(
        ${TEST_ESP_RMAKER_NEO_TARGET} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../osal/ota/priv_include"
    )

    # Link dependencies
    target_link_libraries(${TEST_ESP_RMAKER_NEO_TARGET} PRIVATE ${TEST_ESP_RMAKER_NEO_PRIV_REQUIRES} unity::framework)

    add_test(NAME ${TEST_ESP_RMAKER_NEO_TARGET} COMMAND ${TEST_ESP_RMAKER_NEO_TARGET})

    # The migration rollback stub replaces the real function, which exits the process. Linux: linker --wrap. Apple: a
    # stub library with the real symbol name, inserted at load time.
    if (APPLE)
        add_library(
            migration_test_stubs_apple SHARED
            "${CMAKE_CURRENT_LIST_DIR}/tests_src/credentials/migration_test_stubs_apple.c"
        )
        target_include_directories(
            migration_test_stubs_apple PRIVATE $<TARGET_PROPERTY:osal,INTERFACE_INCLUDE_DIRECTORIES>
        )
        # g_test_migration_rollback_count is defined in test_migration.c. Resolve at load time.
        target_link_options(migration_test_stubs_apple PRIVATE "LINKER:-undefined,dynamic_lookup")
        target_link_options(${TEST_ESP_RMAKER_NEO_TARGET} PRIVATE "LINKER:-flat_namespace,-interposable")
        add_dependencies(${TEST_ESP_RMAKER_NEO_TARGET} migration_test_stubs_apple)
        set_tests_properties(
            ${TEST_ESP_RMAKER_NEO_TARGET}
            PROPERTIES ENVIRONMENT
                       "DYLD_INSERT_LIBRARIES=$<TARGET_FILE:migration_test_stubs_apple>;DYLD_FORCE_FLAT_NAMESPACE=1"
        )
    else ()
        target_link_options(
            ${TEST_ESP_RMAKER_NEO_TARGET} PRIVATE "LINKER:--wrap=osal_ota_mark_app_invalid_rollback_and_reboot"
        )
    endif ()

    # Add post-build factory NVS partition message
    set(RMNG_TOOL_FACTORY_NVS_GEN_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../tools/factory_nvs_gen)
    get_filename_component(RMNG_TOOL_FACTORY_NVS_GEN_DIR ${RMNG_TOOL_FACTORY_NVS_GEN_DIR} ABSOLUTE)
    set(TEST_ESP_RMAKER_NEO_POST_BUILD_MESSAGE_LINE1 "----------------------------------------------")
    set(TEST_ESP_RMAKER_NEO_POST_BUILD_MESSAGE_LINE2
        "Provide a factory NVS partition for the 'test_rmaker_neo' component by:"
    )
    set(TEST_ESP_RMAKER_NEO_POST_BUILD_MESSAGE_LINE3
        "1. Using the tool at ${RMNG_TOOL_FACTORY_NVS_GEN_DIR} to generate the required binary."
    )
    set(TEST_ESP_RMAKER_NEO_POST_BUILD_MESSAGE_LINE4
        "2. Copying the generated 'nvs_persistent' folder to ${CMAKE_BINARY_DIR}/components/esp_rmaker_neo/test_rmaker_neo."
    )
    set(TEST_ESP_RMAKER_NEO_POST_BUILD_MESSAGE_LINE5 "----------------------------------------------")
    add_custom_command(
        TARGET ${TEST_ESP_RMAKER_NEO_TARGET}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${TEST_ESP_RMAKER_NEO_POST_BUILD_MESSAGE_LINE1}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${TEST_ESP_RMAKER_NEO_POST_BUILD_MESSAGE_LINE2}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${TEST_ESP_RMAKER_NEO_POST_BUILD_MESSAGE_LINE3}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${TEST_ESP_RMAKER_NEO_POST_BUILD_MESSAGE_LINE4}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${TEST_ESP_RMAKER_NEO_POST_BUILD_MESSAGE_LINE5}"
    )
endif ()
