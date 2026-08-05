# include internal RMNG common headers for testing of internal components
include(${CMAKE_CURRENT_LIST_DIR}/../sources.cmake)

set(RMNG_COMMON_TEST_SRCS
    # All test sources
    "tests_src/test_credentials.c"
    "tests_src/test_work_queue.c"
    "tests_src/util/test_convert.c"
    "tests_src/util/test_nvs.c"
    "tests_src/util/test_crypto.c"
    "tests_src/retry/test_backoff.c"
    # Test suite
    "test_all.c"
)
set(RMNG_COMMON_TEST_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}")
set(RMNG_COMMON_TEST_PRIV_INCLUDE_DIRS ${RMNG_COMMON_INCLUDE_DIRS})
set(RMNG_COMMON_TEST_PRIV_REQUIRES ${RMNG_COMMON_NAME} ${RMNG_COMMON_PRIV_REQUIRES})

# ESP-IDF platform ###
if (ESP_PLATFORM)
    # esp_rmaker_neo_common's own dirs are attached after register: IDF's component validation inspects register-time
    # properties only, and would otherwise warn about this component borrowing them.
    idf_component_register(
        SRCS ${RMNG_COMMON_TEST_SRCS}
        INCLUDE_DIRS ${RMNG_COMMON_TEST_INCLUDE_DIRS}
        PRIV_REQUIRES ${RMNG_COMMON_TEST_PRIV_REQUIRES} unity
    )
    target_include_directories(${COMPONENT_LIB} PRIVATE ${RMNG_COMMON_TEST_PRIV_INCLUDE_DIRS})

    # Normal CMake platform ###
else ()
    set(RMNG_COMMON_TEST_NAME rmng-common-tests)
    add_executable(${RMNG_COMMON_TEST_NAME} ${RMNG_COMMON_TEST_SRCS} test_runner_posix.c)
    target_include_directories(${RMNG_COMMON_TEST_NAME} PUBLIC ${RMNG_COMMON_TEST_INCLUDE_DIRS})
    target_include_directories(${RMNG_COMMON_TEST_NAME} PRIVATE ${RMNG_COMMON_TEST_PRIV_INCLUDE_DIRS})

    # Link dependencies
    target_link_libraries(${RMNG_COMMON_TEST_NAME} PRIVATE ${RMNG_COMMON_TEST_PRIV_REQUIRES} unity::framework)

    add_test(NAME ${RMNG_COMMON_TEST_NAME} COMMAND ${RMNG_COMMON_TEST_NAME})

    add_custom_command(
        TARGET ${RMNG_COMMON_TEST_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${RMNG_COMMON_TEST_POST_BUILD_MESSAGE_LINE1}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${RMNG_COMMON_TEST_POST_BUILD_MESSAGE_LINE2}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${RMNG_COMMON_TEST_POST_BUILD_MESSAGE_LINE3}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${RMNG_COMMON_TEST_POST_BUILD_MESSAGE_LINE4}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --cyan "${RMNG_COMMON_TEST_POST_BUILD_MESSAGE_LINE5}"
    )
endif ()
