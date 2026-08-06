set(TEST_OSAL_JSON_SOURCES tests_src/test_json_basic.c test_all.c)
set(TEST_OSAL_JSON_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})

# ESP-IDF platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS ${TEST_OSAL_JSON_SOURCES}
        INCLUDE_DIRS ${TEST_OSAL_JSON_INCLUDE_DIRS}
        PRIV_REQUIRES unity osal
    )

    # POSIX platform ###
else ()
    set(TEST_OSAL_JSON_TARGET osal_json_tests)
    add_executable(${TEST_OSAL_JSON_TARGET} ${TEST_OSAL_JSON_SOURCES} test_runner_posix.c)
    target_include_directories(${TEST_OSAL_JSON_TARGET} PRIVATE ${TEST_OSAL_JSON_INCLUDE_DIRS})
    target_link_libraries(${TEST_OSAL_JSON_TARGET} PRIVATE osal unity::framework)

    add_test(NAME ${TEST_OSAL_JSON_TARGET} COMMAND ${TEST_OSAL_JSON_TARGET})
endif ()
