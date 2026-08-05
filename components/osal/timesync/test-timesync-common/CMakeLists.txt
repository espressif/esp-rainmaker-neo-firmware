list(APPEND OSAL_TIMESYNC_PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../priv_include")

set(TEST_OSAL_TIMESYNC_SOURCES tests_src/test_timesync_basic.c tests_src/test_timesync_timezone.c
                               tests_src/test_timesync_storage.c test_all.c
)
set(TEST_OSAL_TIMESYNC_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR} ${OSAL_TIMESYNC_PRIV_INCLUDE_DIRS})

# ESP-IDF platform ###
if (ESP_PLATFORM)
    # Only this component's own dir is declared at register time. osal's priv_include is attached afterwards so IDF's
    # component validation (which inspects register-time properties) does not warn about borrowing it.
    idf_component_register(
        SRCS ${TEST_OSAL_TIMESYNC_SOURCES}
        INCLUDE_DIRS "."
        PRIV_REQUIRES unity osal
    )
    target_include_directories(${COMPONENT_LIB} PRIVATE ${OSAL_TIMESYNC_PRIV_INCLUDE_DIRS})

    # POSIX platform ###
else ()
    set(TEST_OSAL_TIMESYNC_TARGET_NAME timesync_common_tests)
    add_executable(${TEST_OSAL_TIMESYNC_TARGET_NAME} ${TEST_OSAL_TIMESYNC_SOURCES} test_runner_posix.c)
    target_include_directories(${TEST_OSAL_TIMESYNC_TARGET_NAME} PRIVATE ${TEST_OSAL_TIMESYNC_INCLUDE_DIRS})
    target_link_libraries(${TEST_OSAL_TIMESYNC_TARGET_NAME} PRIVATE osal unity::framework)

    add_test(NAME ${TEST_OSAL_TIMESYNC_TARGET_NAME} COMMAND ${TEST_OSAL_TIMESYNC_TARGET_NAME})
endif ()
