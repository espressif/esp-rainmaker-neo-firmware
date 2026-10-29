set(TEST_OSAL_HTTP_SOURCES tests_src/test_http_basic.c tests_src/test_http_contracts.c test_all.c)
set(TEST_OSAL_HTTP_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})

set(TEST_OSAL_HTTP_PRIV_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/config)

# ESP-IDF platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS ${TEST_OSAL_HTTP_SOURCES}
        INCLUDE_DIRS ${TEST_OSAL_HTTP_INCLUDE_DIRS}
        PRIV_INCLUDE_DIRS ${TEST_OSAL_HTTP_PRIV_INCLUDE_DIRS}
        PRIV_REQUIRES unity osal
    )
    set(TEST_OSAL_HTTP_BINARY_TARGET ${COMPONENT_TARGET})

    # POSIX platform ###
else ()
    set(TEST_OSAL_HTTP_TARGET osal_http_tests)
    add_executable(${TEST_OSAL_HTTP_TARGET} ${TEST_OSAL_HTTP_SOURCES} test_runner_posix.c)
    target_include_directories(
        ${TEST_OSAL_HTTP_TARGET} PRIVATE ${TEST_OSAL_HTTP_INCLUDE_DIRS} ${TEST_OSAL_HTTP_PRIV_INCLUDE_DIRS}
    )
    target_link_libraries(${TEST_OSAL_HTTP_TARGET} PRIVATE osal_http unity::framework)

    add_test(NAME ${TEST_OSAL_HTTP_TARGET} COMMAND ${TEST_OSAL_HTTP_TARGET})
    set(TEST_OSAL_HTTP_BINARY_TARGET ${TEST_OSAL_HTTP_TARGET})
endif ()

# TLS tests use platform CA bundle (ca-bundle-posix on POSIX, ESP cert bundle on ESP); no embedded cert needed.
