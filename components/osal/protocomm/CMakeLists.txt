# Normal CMake Platform ###

# Remove warnings for vendored sources
set(ESP_PROTOCOMM_VENDOR_SRCS
    ${ESP_PROTOCOMM_VENDOR_SRC_DIR}/common/protocomm.c ${ESP_PROTOCOMM_VENDOR_PROTOC_DIR}/constants.pb-c.c
    ${ESP_PROTOCOMM_VENDOR_PROTOC_DIR}/session.pb-c.c ${ESP_PROTOCOMM_VENDOR_PROTOC_DIR}/sec0.pb-c.c
    ${ESP_PROTOCOMM_VENDOR_PROTOC_DIR}/sec1.pb-c.c ${ESP_PROTOCOMM_VENDOR_PROTOC_DIR}/sec2.pb-c.c
)
set(ESP_PROTOCOMM_VENDOR_SRCS_SEC0 ${ESP_PROTOCOMM_VENDOR_SRC_DIR}/security/security0.c)
set(ESP_PROTOCOMM_VENDOR_SRCS_SEC1 ${ESP_PROTOCOMM_VENDOR_SRC_DIR}/security/security1.c)
set(ESP_PROTOCOMM_VENDOR_SRCS_SEC2
    ${ESP_PROTOCOMM_VENDOR_SRC_DIR}/security/security2.c ${ESP_PROTOCOMM_VENDOR_SRC_DIR}/crypto/srp6a/esp_srp.c
    ${ESP_PROTOCOMM_VENDOR_SRC_DIR}/crypto/srp6a/esp_srp_mpi.c
)
set_source_files_properties(
    ${ESP_PROTOCOMM_VENDOR_SRCS} ${ESP_PROTOCOMM_VENDOR_SRCS_SEC0} ${ESP_PROTOCOMM_VENDOR_SRCS_SEC1}
    ${ESP_PROTOCOMM_VENDOR_SRCS_SEC2} PROPERTIES COMPILE_OPTIONS "-Wno-format"
)

# Construct self directories
set(PC_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/src")

# Construct target dependencies
set(PC_INCLUDE_DIRS
    ${ESP_PROTOCOMM_VENDOR_INCLUDE_DIR}/common ${ESP_PROTOCOMM_VENDOR_INCLUDE_DIR}/security
    ${ESP_PROTOCOMM_VENDOR_INCLUDE_DIR}/transports ${ESP_PROTOCOMM_VENDOR_INCLUDE_DIR}/crypto/srp6a
    ${ESP_PROTOCOMM_VENDOR_PROTOC_DIR}
)
set(PC_PRIV_INCLUDE_DIRS ${ESP_PROTOCOMM_VENDOR_SRC_DIR}/common)
set(PC_SRCS
    ${ESP_PROTOCOMM_VENDOR_SRCS}
    # POSIX implementations
    ${PC_SRC_DIR}/transports/protocomm_httpd.c
)

# Add config-specific sources
if (CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_0)
    list(APPEND PC_SRCS ${ESP_PROTOCOMM_VENDOR_SRCS_SEC0})
endif ()
if (CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1)
    list(APPEND PC_SRCS ${ESP_PROTOCOMM_VENDOR_SRCS_SEC1})
endif ()
if (CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2)
    list(APPEND PC_SRCS ${ESP_PROTOCOMM_VENDOR_SRCS_SEC2})
endif ()

# Add dependencies
include(${CMAKE_CURRENT_LIST_DIR}/../../../third_party/install_scripts/protobuf-c.cmake)
protobuf_c_make_available()
include(${CMAKE_CURRENT_LIST_DIR}/fetch_civetweb.cmake)
protocomm_posix_civetweb_make_available()
include(${CMAKE_CURRENT_LIST_DIR}/../../../third_party/install_scripts/mbedtls.cmake)
mbedtls_make_available()

# Construct target
set(PC_NAME protocomm-posix)
add_library(${PC_NAME} STATIC ${PC_SRCS})
target_include_directories(${PC_NAME} PUBLIC ${PC_INCLUDE_DIRS})
target_include_directories(${PC_NAME} PRIVATE ${PC_PRIV_INCLUDE_DIRS})
target_link_libraries(${PC_NAME} PRIVATE civetweb mbedtls protobuf-c osal)
target_link_libraries(${PC_NAME} PUBLIC osal)
