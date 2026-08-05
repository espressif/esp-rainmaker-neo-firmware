set(TEST_ESP_CONSOLE_POSIX_SOURCES tests_src/test_console_shim.c test_all.c)
set(TEST_ESP_CONSOLE_POSIX_INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR})

# POSIX-only component, so only the POSIX test path is provided.
set(TEST_ESP_CONSOLE_POSIX_NAME esp_console_posix_tests)
add_executable(${TEST_ESP_CONSOLE_POSIX_NAME} ${TEST_ESP_CONSOLE_POSIX_SOURCES} test_runner_posix.c)
target_include_directories(${TEST_ESP_CONSOLE_POSIX_NAME} PRIVATE ${TEST_ESP_CONSOLE_POSIX_INCLUDE_DIRS})
target_link_libraries(${TEST_ESP_CONSOLE_POSIX_NAME} PRIVATE osal unity::framework)

add_test(NAME ${TEST_ESP_CONSOLE_POSIX_NAME} COMMAND ${TEST_ESP_CONSOLE_POSIX_NAME})
