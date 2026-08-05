# include internal RMNG headers for testing of internal components
include(${CMAKE_CURRENT_LIST_DIR}/../sources.cmake)

set(RMNG_TEST_SRCS
    # All test sources
    "tests_src/test_node_model.c"
    "tests_src/test_timeseries.c"
    "tests_src/chal_resp/test_pb_c.c"
    "tests_src/local_ctrl/test_local_ctrl_endpoints.c"
    "tests_src/local_ctrl/test_local_ctrl_sec2_cache.c"
    "tests_src/claim/test_claim.c"
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
    "tests_src/services/test_schedules.c"
    "tests_src/services/test_schedule_port.c"
    "tests_src/services/test_automation.c"
    # Test suite
    "test_all.c"
)
set(RMNG_TEST_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}" ${RMNG_DATA_MODEL_INCLUDE_DIRS})
set(RMNG_TEST_PRIV_INCLUDE_DIRS
    ${RMNG_PRIV_INCLUDE_DIRS} ${RMNG_DATA_MODEL_PRIV_INCLUDE_DIRS} ${RMNG_PRIV_STUB_HOST_CTRL_INCLUDE_DIR}
    ${RMNG_SRC_DIR} # Allow access to internal RMNG sources for testing
)
set(RMNG_TEST_PRIV_REQUIRES ${RMNG_NAME} ${RMNG_PRIV_REQUIRES})

if (CONFIG_RMNG_BRIDGE_ENABLED)
    list(APPEND RMNG_TEST_SRCS "tests_src/test_bridge.c" "tests_src/test_bridge_child_nvs.c"
         "tests_src/test_bridge_accessors.c" "tests_src/test_node_config_pending.c"
    )
endif ()

# ESP-IDF platform ###
if (ESP_PLATFORM)
    # esp_rmaker_neo's own dirs (priv_include, stubs, src) are attached after register: IDF's component validation
    # inspects register-time properties only, and would otherwise warn about this component borrowing them.
    idf_component_register(
        SRCS ${RMNG_TEST_SRCS}
        INCLUDE_DIRS "."
        PRIV_REQUIRES ${RMNG_TEST_PRIV_REQUIRES} unity
    )
    target_include_directories(${COMPONENT_LIB} PUBLIC ${RMNG_DATA_MODEL_INCLUDE_DIRS})
    target_include_directories(${COMPONENT_LIB} PRIVATE ${RMNG_TEST_PRIV_INCLUDE_DIRS})

    # Normal CMake platform ###
else ()
    set(RMNG_TEST_NAME rmng_tests)
    add_executable(${RMNG_TEST_NAME} ${RMNG_TEST_SRCS} test_runner_posix.c)
    target_include_directories(${RMNG_TEST_NAME} PUBLIC ${RMNG_TEST_INCLUDE_DIRS})
    target_include_directories(${RMNG_TEST_NAME} PRIVATE ${RMNG_TEST_PRIV_INCLUDE_DIRS})

    # Link dependencies
    target_link_libraries(${RMNG_TEST_NAME} PRIVATE ${RMNG_TEST_PRIV_REQUIRES} unity::framework)

    add_test(NAME ${RMNG_TEST_NAME} COMMAND ${RMNG_TEST_NAME})

    # Add post-build factory NVS partition message
    set(RMNG_TOOL_FACTORY_NVS_GEN_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../tools/factory_nvs_gen)
    get_filename_component(RMNG_TOOL_FACTORY_NVS_GEN_DIR ${RMNG_TOOL_FACTORY_NVS_GEN_DIR} ABSOLUTE)
    set(RMNG_TEST_POST_BUILD_MESSAGE_LINE1 "----------------------------------------------")
    set(RMNG_TEST_POST_BUILD_MESSAGE_LINE2 "Provide a factory NVS partition for the 'test_rmaker_neo' component by:")
    set(RMNG_TEST_POST_BUILD_MESSAGE_LINE3
        "1. Using the tool at ${RMNG_TOOL_FACTORY_NVS_GEN_DIR} to generate the required binary."
    )
    set(RMNG_TEST_POST_BUILD_MESSAGE_LINE4
        "2. Copying the generated 'nvs_persistent' folder to ${CMAKE_BINARY_DIR}/components/esp_rmaker_neo/test_rmaker_neo."
    )
    set(RMNG_TEST_POST_BUILD_MESSAGE_LINE5 "----------------------------------------------")
    add_custom_command(
        TARGET ${RMNG_TEST_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${RMNG_TEST_POST_BUILD_MESSAGE_LINE1}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${RMNG_TEST_POST_BUILD_MESSAGE_LINE2}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${RMNG_TEST_POST_BUILD_MESSAGE_LINE3}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${RMNG_TEST_POST_BUILD_MESSAGE_LINE4}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${RMNG_TEST_POST_BUILD_MESSAGE_LINE5}"
    )
endif ()
