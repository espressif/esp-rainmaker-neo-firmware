set(TEST_OSAL_CONSOLE_SOURCES tests_src/test_console_shim.c test_all.c)
set(TEST_OSAL_CONSOLE_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})

# POSIX-only component, so only the POSIX test path is provided.
set(TEST_OSAL_CONSOLE_TARGET osal_console_tests)
add_executable(${TEST_OSAL_CONSOLE_TARGET} ${TEST_OSAL_CONSOLE_SOURCES} test_runner_posix.c)
target_include_directories(${TEST_OSAL_CONSOLE_TARGET} PRIVATE ${TEST_OSAL_CONSOLE_INCLUDE_DIRS})
target_link_libraries(${TEST_OSAL_CONSOLE_TARGET} PRIVATE osal unity::framework)

add_test(NAME ${TEST_OSAL_CONSOLE_TARGET} COMMAND ${TEST_OSAL_CONSOLE_TARGET})
