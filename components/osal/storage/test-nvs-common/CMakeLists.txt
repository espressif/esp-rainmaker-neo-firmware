set(TEST_NVS_COMMON_SOURCES tests_src/test_nvs_basic.c tests_src/test_nvs_partition_isolation.c test_all.c)
set(TEST_NVS_COMMON_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})

# ESP-IDF platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS ${TEST_NVS_COMMON_SOURCES}
        INCLUDE_DIRS ${TEST_NVS_COMMON_INCLUDE_DIRS}
        PRIV_REQUIRES unity osal
    )

    # POSIX platform ###
else ()
    set(TEST_NVS_COMMON_NAME nvs_common_tests)
    add_executable(${TEST_NVS_COMMON_NAME} ${TEST_NVS_COMMON_SOURCES} test_runner_posix.c)
    target_include_directories(${TEST_NVS_COMMON_NAME} PRIVATE ${TEST_NVS_COMMON_INCLUDE_DIRS})
    target_link_libraries(${TEST_NVS_COMMON_NAME} PRIVATE osal unity::framework)

    add_test(NAME ${TEST_NVS_COMMON_NAME} COMMAND ${TEST_NVS_COMMON_NAME})
endif ()
