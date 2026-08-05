# Grab tinyCBOR from component registry
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/esp_component_manager.cmake)
rmng_idf_components_import(RMNG_OTA_COMMON_COMPONENTS_DIR DEPENDENCIES "espressif/cbor:0.6.1~4")
set(TINYCBOR_COMPONENT_DIR ${RMNG_OTA_COMMON_COMPONENTS_DIR}/espressif__cbor)
set(TINYCBOR_SOURCES
    "${TINYCBOR_COMPONENT_DIR}/tinycbor/src/cborencoder_close_container_checked.c"
    "${TINYCBOR_COMPONENT_DIR}/tinycbor/src/cborencoder.c"
    "${TINYCBOR_COMPONENT_DIR}/tinycbor/src/cborencoder_float.c"
    "${TINYCBOR_COMPONENT_DIR}/tinycbor/src/cborerrorstrings.c"
    "${TINYCBOR_COMPONENT_DIR}/tinycbor/src/cborparser_dup_string.c"
    "${TINYCBOR_COMPONENT_DIR}/tinycbor/src/cborparser.c"
    "${TINYCBOR_COMPONENT_DIR}/tinycbor/src/cborparser_float.c"
    "${TINYCBOR_COMPONENT_DIR}/tinycbor/src/cborpretty_stdio.c"
    "${TINYCBOR_COMPONENT_DIR}/tinycbor/src/cborpretty.c"
    "${TINYCBOR_COMPONENT_DIR}/tinycbor/src/cbortojson.c"
    "${TINYCBOR_COMPONENT_DIR}/tinycbor/src/cborvalidation.c"
)
set(TINYCBOR_INCLUDE_DIRS "${TINYCBOR_COMPONENT_DIR}/tinycbor/src")
