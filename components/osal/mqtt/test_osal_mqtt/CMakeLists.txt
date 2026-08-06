set(TEST_OSAL_MQTT_SOURCES
    tests_src/test_mqtt_basic.c tests_src/test_mqtt_common_util.c tests_src/test_mqtt_common_events.c
    tests_src/test_mqtt_common_subscription_manager.c test_all.c
)
set(TEST_OSAL_MQTT_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})

set(TEST_OSAL_MQTT_PRIV_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/config)

# ESP-IDF platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS ${TEST_OSAL_MQTT_SOURCES}
        INCLUDE_DIRS ${TEST_OSAL_MQTT_INCLUDE_DIRS}
        PRIV_INCLUDE_DIRS ${TEST_OSAL_MQTT_PRIV_INCLUDE_DIRS}
        PRIV_REQUIRES unity osal
    )
    set(TEST_OSAL_MQTT_BINARY_TARGET ${COMPONENT_TARGET})

    # POSIX platform ###
else ()
    set(TEST_OSAL_MQTT_TARGET osal_mqtt_tests)
    add_executable(${TEST_OSAL_MQTT_TARGET} ${TEST_OSAL_MQTT_SOURCES} test_runner_posix.c)
    target_include_directories(
        ${TEST_OSAL_MQTT_TARGET} PRIVATE ${TEST_OSAL_MQTT_INCLUDE_DIRS} ${TEST_OSAL_MQTT_PRIV_INCLUDE_DIRS}
    )
    target_link_libraries(${TEST_OSAL_MQTT_TARGET} PRIVATE osal_mqtt unity::framework)

    add_test(NAME ${TEST_OSAL_MQTT_TARGET} COMMAND ${TEST_OSAL_MQTT_TARGET})
    set(TEST_OSAL_MQTT_BINARY_TARGET ${TEST_OSAL_MQTT_TARGET})
endif ()

# Add binary data for the local broker
set(TEST_OSAL_MQTT_CERTS_DIR ${CMAKE_CURRENT_LIST_DIR}/certs)
set(TEST_OSAL_MQTT_CERTS_HOST_CRT ${TEST_OSAL_MQTT_CERTS_DIR}/test_osal_mqtt_ca.pem)
set(TEST_OSAL_MQTT_CERTS_CLIENT_CRT ${TEST_OSAL_MQTT_CERTS_DIR}/test_osal_mqtt_client.crt)
set(TEST_OSAL_MQTT_CERTS_CLIENT_KEY ${TEST_OSAL_MQTT_CERTS_DIR}/test_osal_mqtt_client.key)

if (NOT EXISTS ${TEST_OSAL_MQTT_CERTS_HOST_CRT})
    message(
        FATAL_ERROR
            "[test_osal_mqtt] Server certificate 'test_osal_mqtt_ca.pem' not found in ${TEST_OSAL_MQTT_CERTS_DIR}"
    )
endif ()
if (NOT EXISTS ${TEST_OSAL_MQTT_CERTS_CLIENT_CRT})
    message(
        FATAL_ERROR
            "[test_osal_mqtt] Client certificate 'test_osal_mqtt_client.crt' not found in ${TEST_OSAL_MQTT_CERTS_DIR}"
    )
endif ()
if (NOT EXISTS ${TEST_OSAL_MQTT_CERTS_CLIENT_KEY})
    message(
        FATAL_ERROR "[test_osal_mqtt] Client key 'test_osal_mqtt_client.key' not found in ${TEST_OSAL_MQTT_CERTS_DIR}"
    )
endif ()

include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/target_add_binary_data.cmake)
target_add_binary_data(${TEST_OSAL_MQTT_BINARY_TARGET} "${TEST_OSAL_MQTT_CERTS_CLIENT_CRT}" TEXT)
target_add_binary_data(${TEST_OSAL_MQTT_BINARY_TARGET} "${TEST_OSAL_MQTT_CERTS_CLIENT_KEY}" TEXT)
