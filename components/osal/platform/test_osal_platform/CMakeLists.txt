set(TEST_OSAL_PLATFORM_SOURCES
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
set(TEST_OSAL_PLATFORM_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})

# ESP-IDF platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS ${TEST_OSAL_PLATFORM_SOURCES}
        INCLUDE_DIRS ${TEST_OSAL_PLATFORM_INCLUDE_DIRS}
        PRIV_REQUIRES unity osal
    )

    # POSIX platform ###
else ()
    set(TEST_OSAL_PLATFORM_TARGET osal_platform_tests)
    add_executable(${TEST_OSAL_PLATFORM_TARGET} ${TEST_OSAL_PLATFORM_SOURCES} test_runner_posix.c)
    target_include_directories(${TEST_OSAL_PLATFORM_TARGET} PRIVATE ${TEST_OSAL_PLATFORM_INCLUDE_DIRS})
    target_link_libraries(${TEST_OSAL_PLATFORM_TARGET} PRIVATE osal unity::framework)

    add_test(NAME ${TEST_OSAL_PLATFORM_TARGET} COMMAND ${TEST_OSAL_PLATFORM_TARGET})
endif ()
