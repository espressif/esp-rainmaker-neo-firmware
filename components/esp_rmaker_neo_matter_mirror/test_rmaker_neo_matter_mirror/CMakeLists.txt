# POSIX unit tests for the Matter mirror engine (fake port, no esp-matter).

set(TEST_ESP_RMAKER_NEO_MATTER_MIRROR_TARGET esp_rmaker_neo_matter_mirror_tests)

# The committed golden vectors, lowered to C tables for test_mirror_vectors.c. The JSON is a copied input
# (mapping/SOURCE); only its C form is generated, and only for this test.
add_custom_command(
    OUTPUT ${RM_MIRROR_GEN_VECTORS_HDR}
    COMMAND ${Python3_EXECUTABLE} "${RM_MIRROR_GEN_SCRIPT}" ${RM_MIRROR_LIB_ARGS} --vectors-in
            "${RM_MIRROR_VECTORS_JSON}" --output-vectors-c "${RM_MIRROR_GEN_VECTORS_HDR}"
    DEPENDS "${RM_MIRROR_VECTORS_JSON}" ${RM_MIRROR_GEN_DEPS}
    COMMENT "Lowering the committed golden vectors to C"
)

add_executable(
    ${TEST_ESP_RMAKER_NEO_MATTER_MIRROR_TARGET} test_mirror.c test_mirror_vectors.c fake_port.c test_runner_posix.c
                                                ${RM_MIRROR_GEN_VECTORS_HDR}
)
# The core's private headers give the tests a real self node and bridged child node
target_include_directories(
    ${TEST_ESP_RMAKER_NEO_MATTER_MIRROR_TARGET} PRIVATE ${CMAKE_CURRENT_LIST_DIR}
                                                        ${CMAKE_CURRENT_LIST_DIR}/../../esp_rmaker_neo/priv_include
)
target_link_libraries(${TEST_ESP_RMAKER_NEO_MATTER_MIRROR_TARGET} PRIVATE esp_rmaker_neo_matter_mirror unity::framework)

add_test(NAME ${TEST_ESP_RMAKER_NEO_MATTER_MIRROR_TARGET} COMMAND ${TEST_ESP_RMAKER_NEO_MATTER_MIRROR_TARGET})
