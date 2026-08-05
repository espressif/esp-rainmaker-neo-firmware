set(TEST_MQTT_COMMON_SOURCES
    tests_src/test_mqtt_basic.c tests_src/test_mqtt_common_util.c tests_src/test_mqtt_common_events.c
    tests_src/test_mqtt_common_subscription_manager.c test_all.c
)
set(TEST_MQTT_COMMON_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})

set(TEST_MQTT_COMMON_PRIV_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/config)

# ESP-IDF platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS ${TEST_MQTT_COMMON_SOURCES}
        INCLUDE_DIRS ${TEST_MQTT_COMMON_INCLUDE_DIRS}
        PRIV_INCLUDE_DIRS ${TEST_MQTT_COMMON_PRIV_INCLUDE_DIRS}
        PRIV_REQUIRES unity osal
    )
    set(TEST_MQTT_COMMON_BINARY_TARGET ${COMPONENT_TARGET})

    # POSIX platform ###
else ()
    set(TEST_MQTT_COMMON_TARGET_NAME osal_mqtt_tests)
    add_executable(${TEST_MQTT_COMMON_TARGET_NAME} ${TEST_MQTT_COMMON_SOURCES} test_runner_posix.c)
    target_include_directories(
        ${TEST_MQTT_COMMON_TARGET_NAME} PRIVATE ${TEST_MQTT_COMMON_INCLUDE_DIRS} ${TEST_MQTT_COMMON_PRIV_INCLUDE_DIRS}
    )
    target_link_libraries(${TEST_MQTT_COMMON_TARGET_NAME} PRIVATE osal_mqtt unity::framework)

    add_test(NAME ${TEST_MQTT_COMMON_TARGET_NAME} COMMAND ${TEST_MQTT_COMMON_TARGET_NAME})
    set(TEST_MQTT_COMMON_BINARY_TARGET ${TEST_MQTT_COMMON_TARGET_NAME})
endif ()

# Add binary data for the local broker
set(TEST_MQTT_COMMON_CERTS_DIR ${CMAKE_CURRENT_LIST_DIR}/certs)
set(TEST_MQTT_COMMON_CERTS_HOST_CRT ${TEST_MQTT_COMMON_CERTS_DIR}/test_mqtt_common_host_crt.pem)
set(TEST_MQTT_COMMON_CERTS_CLIENT_CRT ${TEST_MQTT_COMMON_CERTS_DIR}/test_mqtt_common_client.crt)
set(TEST_MQTT_COMMON_CERTS_CLIENT_KEY ${TEST_MQTT_COMMON_CERTS_DIR}/test_mqtt_common_client.key)

if (NOT EXISTS ${TEST_MQTT_COMMON_CERTS_HOST_CRT})
    message(
        FATAL_ERROR
            "[test-mqtt-common] Server certificate 'test_mqtt_common_host_crt.pem' not found in ${TEST_MQTT_COMMON_CERTS_DIR}"
    )
endif ()
if (NOT EXISTS ${TEST_MQTT_COMMON_CERTS_CLIENT_CRT})
    message(
        FATAL_ERROR
            "[test-mqtt-common] Client certificate 'test_mqtt_common_client.crt' not found in ${TEST_MQTT_COMMON_CERTS_DIR}"
    )
endif ()
if (NOT EXISTS ${TEST_MQTT_COMMON_CERTS_CLIENT_KEY})
    message(
        FATAL_ERROR
            "[test-mqtt-common] Client key 'test_mqtt_common_client.key' not found in ${TEST_MQTT_COMMON_CERTS_DIR}"
    )
endif ()

include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/target_add_binary_data.cmake)
target_add_binary_data(${TEST_MQTT_COMMON_BINARY_TARGET} "${TEST_MQTT_COMMON_CERTS_CLIENT_CRT}" TEXT)
target_add_binary_data(${TEST_MQTT_COMMON_BINARY_TARGET} "${TEST_MQTT_COMMON_CERTS_CLIENT_KEY}" TEXT)
