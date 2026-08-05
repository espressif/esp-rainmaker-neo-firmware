set(TEST_OSAL_SOURCES
    tests_src/test_ticks.c
    tests_src/test_tick_math.c
    tests_src/test_timer_chain.c
    tests_src/test_semaphore.c
    tests_src/test_queue.c
    tests_src/test_event_group.c
    tests_src/test_event_loop.c
    tests_src/test_scheduler.c
    tests_src/test_task.c
    tests_src/test_random.c
    tests_src/test_sysinfo.c
    test_all.c
)
set(TEST_OSAL_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})

# ESP-IDF platform ###
if (ESP_PLATFORM)
    set(TARGET ${COMPONENT_LIB} PROPERTY COMPONENT_NAME "test-platform-common")
    idf_component_register(
        SRCS ${TEST_OSAL_SOURCES}
        INCLUDE_DIRS ${TEST_OSAL_INCLUDE_DIRS}
        PRIV_REQUIRES unity osal
    )

    # POSIX platform ###
else ()
    set(TEST_OSAL_NAME platform_common_tests)
    add_executable(${TEST_OSAL_NAME} ${TEST_OSAL_SOURCES} test_runner_posix.c)
    target_include_directories(platform_common_tests PRIVATE ${TEST_OSAL_INCLUDE_DIRS})
    target_link_libraries(${TEST_OSAL_NAME} PRIVATE osal unity::framework)

    add_test(NAME ${TEST_OSAL_NAME} COMMAND ${TEST_OSAL_NAME})
endif ()
