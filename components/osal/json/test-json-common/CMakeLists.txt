set(TEST_JSON_COMMON_SOURCES tests_src/test_json_basic.c test_all.c)
set(TEST_JSON_COMMON_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})

# ESP-IDF platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS ${TEST_JSON_COMMON_SOURCES}
        INCLUDE_DIRS ${TEST_JSON_COMMON_INCLUDE_DIRS}
        PRIV_REQUIRES unity osal
    )

    # POSIX platform ###
else ()
    set(TEST_JSON_COMMON_TARGET_NAME json_common_tests)
    add_executable(${TEST_JSON_COMMON_TARGET_NAME} ${TEST_JSON_COMMON_SOURCES} test_runner_posix.c)
    target_include_directories(${TEST_JSON_COMMON_TARGET_NAME} PRIVATE ${TEST_JSON_COMMON_INCLUDE_DIRS})
    target_link_libraries(${TEST_JSON_COMMON_TARGET_NAME} PRIVATE osal unity::framework)

    add_test(NAME ${TEST_JSON_COMMON_TARGET_NAME} COMMAND ${TEST_JSON_COMMON_TARGET_NAME})
endif ()
