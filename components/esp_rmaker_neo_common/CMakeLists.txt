include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

# ESP-IDF Platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS ${RMNG_COMMON_SRCS}
        INCLUDE_DIRS ${RMNG_COMMON_INCLUDE_DIRS}
        PRIV_INCLUDE_DIRS ${RMNG_COMMON_PRIV_INCLUDE_DIRS}
        REQUIRES ${RMNG_COMMON_REQUIRES}
        PRIV_REQUIRES ${RMNG_COMMON_PRIV_REQUIRES}
    )

    # Normal CMake Platform ###
else ()
    add_library(${RMNG_COMMON_NAME} STATIC ${RMNG_COMMON_SRCS})
    target_include_directories(${RMNG_COMMON_NAME} PUBLIC ${RMNG_COMMON_INCLUDE_DIRS})
    target_include_directories(${RMNG_COMMON_NAME} PRIVATE ${RMNG_COMMON_PRIV_INCLUDE_DIRS})

    # Testing setup
    if (CMAKE_TESTING_ENABLED)
        include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/testing.cmake)
        add_subdirectory(test_rmaker_neo_common)
        testing_enable_coverage(esp_rmaker_neo_common)
        get_filename_component(RMNG_SDK_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
        testing_add_gcovr_coverage_target(
            NAME rmng_common_coverage
            ROOT_DIR "${RMNG_SDK_REPO_ROOT}"
            TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}"
            OUTPUT_DIR "${CMAKE_BINARY_DIR}/rmng-common-coverage"
        )
    endif ()

    # Add third-party libraries (without unit tests)
    include(${CMAKE_CURRENT_LIST_DIR}/../../third_party/install_scripts/mbedtls.cmake)
    mbedtls_make_available()

    # Add dependencies.
    if (NOT TARGET osal)
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../osal ${CMAKE_BINARY_DIR}/lib/osal)
    endif ()

    target_link_libraries(${RMNG_COMMON_NAME} PUBLIC ${RMNG_COMMON_REQUIRES})
    target_link_libraries(${RMNG_COMMON_NAME} PRIVATE ${RMNG_COMMON_PRIV_REQUIRES})
endif ()
