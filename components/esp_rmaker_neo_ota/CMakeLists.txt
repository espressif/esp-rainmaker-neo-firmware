include("${CMAKE_CURRENT_LIST_DIR}/requires.cmake")

# ESP-IDF Platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS "${CMAKE_CURRENT_LIST_DIR}/src/placeholder.c"
        REQUIRES ${RMNG_OTA_REQUIRES}
        PRIV_REQUIRES ${RMNG_OTA_PRIV_REQUIRES}
    )

    # Add optional dependencies
    list(LENGTH RMNG_OTA_OPT_PRIV_REQUIRES RMNG_OTA_OPT_PRIV_REQUIRES_LENGTH)
    if (RMNG_OTA_OPT_PRIV_REQUIRES_LENGTH GREATER 0)
        idf_component_optional_requires(PRIVATE ${RMNG_OTA_OPT_PRIV_REQUIRES})
    endif ()

    # Add sources and include directories
    include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

    target_sources(${COMPONENT_LIB} PRIVATE ${RMNG_OTA_SRCS})
    target_include_directories(${COMPONENT_LIB} PUBLIC ${RMNG_OTA_INCLUDE_DIRS})
    target_include_directories(${COMPONENT_LIB} PRIVATE ${RMNG_OTA_PRIV_INCLUDE_DIRS})

    # Normal CMake Platform ###
else ()
    include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

    # Add library
    add_library(${RMNG_OTA_NAME} STATIC ${RMNG_OTA_SRCS})
    target_include_directories(${RMNG_OTA_NAME} PUBLIC ${RMNG_OTA_INCLUDE_DIRS})
    target_include_directories(${RMNG_OTA_NAME} PRIVATE ${RMNG_OTA_PRIV_INCLUDE_DIRS})

    # Add coverage target and test component
    option(TEST_RMNG_OTA "Enable testing for RMNG OTA" OFF)
    if (TEST_RMNG_OTA)
        include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/testing.cmake)
        testing_enable_coverage(${RMNG_OTA_NAME})
        get_filename_component(RMNG_SDK_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
        testing_add_gcovr_coverage_target(
            NAME rmng_ota_coverage
            ROOT_DIR "${RMNG_SDK_REPO_ROOT}"
            TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}"
            OUTPUT_DIR "${CMAKE_BINARY_DIR}/rmng-ota-coverage"
            FILTERS "^components/esp_rmaker_neo_ota(/|$)"
        )
        add_subdirectory(test_rmaker_neo_ota)
    endif ()

    # Add dependencies (so standalone posix build e.g. test_apps/posix has required targets). esp_rmaker_neo_common adds
    # osal (which builds osal_ota, osal_mqtt, etc.).
    if (NOT TARGET esp_rmaker_neo_common)
        add_subdirectory(
            ${CMAKE_CURRENT_LIST_DIR}/../esp_rmaker_neo_common ${CMAKE_BINARY_DIR}/lib/esp_rmaker_neo_common
        )
    endif ()
    include(${CMAKE_CURRENT_LIST_DIR}/../../third_party/install_scripts/mbedtls.cmake)
    mbedtls_make_available()

    target_link_libraries(${RMNG_OTA_NAME} PUBLIC ${RMNG_OTA_REQUIRES})
    target_link_libraries(${RMNG_OTA_NAME} PRIVATE ${RMNG_OTA_PRIV_REQUIRES} ${RMNG_OTA_OPT_PRIV_REQUIRES})

    # Link mock bootloader variables to parent (osal/ota publishes them as GLOBAL properties)
    get_property(OSAL_OTA_POSIX_BOOTLOADER_NAME GLOBAL PROPERTY OSAL_OTA_POSIX_BOOTLOADER_NAME)
    get_property(OSAL_OTA_POSIX_PARTITION_FOLDER GLOBAL PROPERTY OSAL_OTA_POSIX_PARTITION_FOLDER)
    get_property(OSAL_OTA_POSIX_DEFAULT_FIRST_PARTITION GLOBAL PROPERTY OSAL_OTA_POSIX_DEFAULT_FIRST_PARTITION)
    set(RMNG_OTA_POSIX_BOOTLOADER_NAME
        ${OSAL_OTA_POSIX_BOOTLOADER_NAME}
        PARENT_SCOPE
    )
    set(RMNG_OTA_POSIX_PARTITION_FOLDER
        ${OSAL_OTA_POSIX_PARTITION_FOLDER}
        PARENT_SCOPE
    )
    set(RMNG_OTA_POSIX_DEFAULT_FIRST_PARTITION
        ${OSAL_OTA_POSIX_DEFAULT_FIRST_PARTITION}
        PARENT_SCOPE
    )
endif ()
