set(TEST_HTTP_COMMON_SOURCES tests_src/test_http_basic.c test_all.c)
set(TEST_HTTP_COMMON_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})

set(TEST_HTTP_COMMON_PRIV_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/config)

# ESP-IDF platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS ${TEST_HTTP_COMMON_SOURCES}
        INCLUDE_DIRS ${TEST_HTTP_COMMON_INCLUDE_DIRS}
        PRIV_INCLUDE_DIRS ${TEST_HTTP_COMMON_PRIV_INCLUDE_DIRS}
        PRIV_REQUIRES unity osal
    )
    set(TEST_HTTP_COMMON_BINARY_TARGET ${COMPONENT_TARGET})

    # POSIX platform ###
else ()
    set(TEST_HTTP_COMMON_TARGET_NAME http_common_tests)
    add_executable(${TEST_HTTP_COMMON_TARGET_NAME} ${TEST_HTTP_COMMON_SOURCES} test_runner_posix.c)
    target_include_directories(
        ${TEST_HTTP_COMMON_TARGET_NAME} PRIVATE ${TEST_HTTP_COMMON_INCLUDE_DIRS} ${TEST_HTTP_COMMON_PRIV_INCLUDE_DIRS}
    )
    target_link_libraries(${TEST_HTTP_COMMON_TARGET_NAME} PRIVATE osal_http unity::framework)

    add_test(NAME ${TEST_HTTP_COMMON_TARGET_NAME} COMMAND ${TEST_HTTP_COMMON_TARGET_NAME})
    set(TEST_HTTP_COMMON_BINARY_TARGET ${TEST_HTTP_COMMON_TARGET_NAME})
endif ()

# TLS tests use platform CA bundle (ca-bundle-posix on POSIX, ESP cert bundle on ESP); no embedded cert needed.
