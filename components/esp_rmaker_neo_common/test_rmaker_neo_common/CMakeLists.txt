# include internal RMNG common headers for testing of internal components
include(${CMAKE_CURRENT_LIST_DIR}/../sources.cmake)

set(TEST_ESP_RMAKER_NEO_COMMON_SRCS
    # All test sources
    "tests_src/test_credentials.c"
    "tests_src/test_work_queue.c"
    "tests_src/util/test_convert.c"
    "tests_src/util/test_nvs.c"
    "tests_src/util/test_crypto.c"
    "tests_src/retry/test_backoff.c"
    "tests_src/retry/test_backoff_race.c"
    # Test suite
    "test_all.c"
)
set(TEST_ESP_RMAKER_NEO_COMMON_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}")
set(TEST_ESP_RMAKER_NEO_COMMON_PRIV_INCLUDE_DIRS ${RMNG_COMMON_INCLUDE_DIRS})
set(TEST_ESP_RMAKER_NEO_COMMON_PRIV_REQUIRES ${RMNG_COMMON_NAME} ${RMNG_COMMON_PRIV_REQUIRES})

# ESP-IDF platform ###
if (ESP_PLATFORM)
    # esp_rmaker_neo_common's own dirs are attached after register: IDF's component validation inspects register-time
    # properties only, and would otherwise warn about this component borrowing them.
    idf_component_register(
        SRCS ${TEST_ESP_RMAKER_NEO_COMMON_SRCS}
        INCLUDE_DIRS ${TEST_ESP_RMAKER_NEO_COMMON_INCLUDE_DIRS}
        PRIV_REQUIRES ${TEST_ESP_RMAKER_NEO_COMMON_PRIV_REQUIRES} unity
    )
    target_include_directories(${COMPONENT_LIB} PRIVATE ${TEST_ESP_RMAKER_NEO_COMMON_PRIV_INCLUDE_DIRS})

    # Normal CMake platform ###
else ()
    set(TEST_ESP_RMAKER_NEO_COMMON_TARGET esp_rmaker_neo_common_tests)
    add_executable(${TEST_ESP_RMAKER_NEO_COMMON_TARGET} ${TEST_ESP_RMAKER_NEO_COMMON_SRCS} test_runner_posix.c)
    target_include_directories(${TEST_ESP_RMAKER_NEO_COMMON_TARGET} PUBLIC ${TEST_ESP_RMAKER_NEO_COMMON_INCLUDE_DIRS})
    target_include_directories(
        ${TEST_ESP_RMAKER_NEO_COMMON_TARGET} PRIVATE ${TEST_ESP_RMAKER_NEO_COMMON_PRIV_INCLUDE_DIRS}
    )

    # Link dependencies
    target_link_libraries(
        ${TEST_ESP_RMAKER_NEO_COMMON_TARGET} PRIVATE ${TEST_ESP_RMAKER_NEO_COMMON_PRIV_REQUIRES} unity::framework
    )

    add_test(NAME ${TEST_ESP_RMAKER_NEO_COMMON_TARGET} COMMAND ${TEST_ESP_RMAKER_NEO_COMMON_TARGET})

    add_custom_command(
        TARGET ${TEST_ESP_RMAKER_NEO_COMMON_TARGET}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${TEST_ESP_RMAKER_NEO_COMMON_POST_BUILD_MESSAGE_LINE1}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${TEST_ESP_RMAKER_NEO_COMMON_POST_BUILD_MESSAGE_LINE2}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${TEST_ESP_RMAKER_NEO_COMMON_POST_BUILD_MESSAGE_LINE3}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${TEST_ESP_RMAKER_NEO_COMMON_POST_BUILD_MESSAGE_LINE4}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${TEST_ESP_RMAKER_NEO_COMMON_POST_BUILD_MESSAGE_LINE5}"
    )
endif ()
