# Internal osal sub-build: the POSIX OTA partition-emulation implementation and mock bootloader. Added via
# add_subdirectory() from the osal CMakeLists on POSIX only; on ESP-IDF the impl source is compiled directly into the
# osal component (see components/osal/CMakeLists.txt).
if (ESP_PLATFORM)
    message(FATAL_ERROR "osal/ota is added as a subdirectory on POSIX only")
endif ()

set(OSAL_OTA_NAME osal_ota)

add_library(
    ${OSAL_OTA_NAME} STATIC "${CMAKE_CURRENT_LIST_DIR}/src/osal_ota_posix.c"
                            "${CMAKE_CURRENT_LIST_DIR}/src/osal_ota_posix_config.c"
)
target_include_directories(${OSAL_OTA_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_include_directories(${OSAL_OTA_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/priv_include)
target_link_libraries(${OSAL_OTA_NAME} PUBLIC osal)

# Build the mock bootloader executable
set(OSAL_OTA_POSIX_BOOTLOADER_NAME "mock_bootloader_posix")
add_executable(${OSAL_OTA_POSIX_BOOTLOADER_NAME} src/mock_bootloader_posix.c)
set_target_properties(${OSAL_OTA_POSIX_BOOTLOADER_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(${OSAL_OTA_POSIX_BOOTLOADER_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/priv_include)
target_link_libraries(${OSAL_OTA_POSIX_BOOTLOADER_NAME} PRIVATE ${OSAL_OTA_NAME} osal)

# Read the default partition prefix and derive the default first partition from the header
file(READ ${CMAKE_CURRENT_LIST_DIR}/priv_include/osal_ota_posix_shared.h SHARED_HEADER_CONTENT)
string(REGEX MATCH "#define OSAL_OTA_POSIX_PART_PREFIX \"([^\"]*)\"" _ ${SHARED_HEADER_CONTENT})
set(OSAL_OTA_POSIX_PART_PREFIX "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define OSAL_OTA_POSIX_BASE_DIR \"([^\"]*)\"" _ ${SHARED_HEADER_CONTENT})
set(OSAL_OTA_POSIX_BASE_DIR "${CMAKE_MATCH_1}")

# Expose the mock-bootloader facts to any consumer scope (esp_rmaker_neo_ota re-exports them as RMNG_OTA_POSIX_*); plain
# PARENT_SCOPE would only reach osal now that this is an osal-internal sub-build.
set_property(GLOBAL PROPERTY OSAL_OTA_POSIX_BOOTLOADER_NAME "${OSAL_OTA_POSIX_BOOTLOADER_NAME}")
set_property(GLOBAL PROPERTY OSAL_OTA_POSIX_PARTITION_FOLDER "${CMAKE_BINARY_DIR}/${OSAL_OTA_POSIX_BASE_DIR}")
set_property(GLOBAL PROPERTY OSAL_OTA_POSIX_DEFAULT_FIRST_PARTITION "${OSAL_OTA_POSIX_PART_PREFIX}0")

# Add mbedtls library (without unit tests)
include(${CMAKE_CURRENT_LIST_DIR}/../../../third_party/install_scripts/mbedtls.cmake)
mbedtls_make_available()
target_link_libraries(${OSAL_OTA_NAME} PRIVATE mbedtls)
