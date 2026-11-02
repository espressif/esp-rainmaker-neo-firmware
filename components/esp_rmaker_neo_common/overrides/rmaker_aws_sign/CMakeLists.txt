# POSIX only. On ESP-IDF the managed component registers itself and the port is selected at runtime, so there is nothing
# for this file to do. See README.md for what the port covers and why.
include(${CMAKE_CURRENT_LIST_DIR}/component_dir_setup.cmake)

# Platform-independent half. The component's own CMakeLists.txt is ESP-IDF-only, so the source list comes from the
# variables file, which excludes src/esp_rmaker_aws_sign_default.c - the only referrer of the built-in ESP-IDF port.
include(${RMAKER_AWS_SIGN_COMPONENT_DIR}/rmaker_aws_sign_variables.cmake)

# The component's crypto port is reused verbatim: it names only PSA and the ops struct, and PSA is what mbedtls provides
# off-target too. The other groups, JSON included, are osal-backed and live in esp_rmaker_neo.
list(APPEND RMAKER_AWS_SIGN_SRCS "${RMAKER_AWS_SIGN_COMPONENT_DIR}/port/esp/crypto.c")

# esp_err.h comes from osal's shim off-target. PUBLIC so users of the public header get it too.
list(APPEND RMAKER_AWS_SIGN_REQUIRES osal)

# mbedtls supplies psa/crypto.h for the reused crypto.c. The install script fetches the sources when needed, so there is
# no ordering dependency on another consumer having fetched them first.
include(${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/install_scripts/mbedtls.cmake)
mbedtls_make_available()

set(RMAKER_AWS_SIGN_NAME rmaker_aws_sign)
add_library(${RMAKER_AWS_SIGN_NAME} STATIC ${RMAKER_AWS_SIGN_SRCS})
target_include_directories(${RMAKER_AWS_SIGN_NAME} PUBLIC ${RMAKER_AWS_SIGN_INCLUDE_DIRS})
target_include_directories(${RMAKER_AWS_SIGN_NAME} PRIVATE ${RMAKER_AWS_SIGN_PRIV_INCLUDE_DIRS})
target_link_libraries(${RMAKER_AWS_SIGN_NAME} PUBLIC ${RMAKER_AWS_SIGN_REQUIRES})
target_link_libraries(${RMAKER_AWS_SIGN_NAME} PRIVATE ${RMAKER_AWS_SIGN_PRIV_REQUIRES} mbedtls)
