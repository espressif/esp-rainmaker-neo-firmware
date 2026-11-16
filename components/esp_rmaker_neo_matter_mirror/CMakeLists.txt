# RainMaker Neo Matter Mirror
#
# Derives a Matter data model from the RainMaker Neo data model at init and keeps the two in sync at runtime. The
# platform-neutral engine (src/engine, plain C) is separated from the esp-matter port (src/port, C++); the engine plus a
# fake port is unit-tested on POSIX. ESP-IDF-only as a whole (needs esp-matter via the project's EXTRA_COMPONENT_DIRS).

include("${CMAKE_CURRENT_LIST_DIR}/requires.cmake")

# ESP-IDF early expansion pass: register requirements only (add_custom_command is not scriptable there).
if (ESP_PLATFORM AND CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(REQUIRES ${RMNG_MATTER_MIRROR_REQUIRES} PRIV_REQUIRES esp_matter nvs_flash mbedtls)
    return()
endif ()

include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

# The generated-headers directory must exist at configure time (it is listed as an include dir).
file(MAKE_DIRECTORY "${RM_MIRROR_GEN_DIR}")

# ESP-IDF Platform ###
if (ESP_PLATFORM)
    idf_component_register(
        SRCS ${RMNG_MATTER_MIRROR_ENGINE_SRCS} ${RMNG_MATTER_MIRROR_PORT_SRCS}
        INCLUDE_DIRS ${RMNG_MATTER_MIRROR_INCLUDE_DIRS}
        PRIV_INCLUDE_DIRS ${RMNG_MATTER_MIRROR_PRIV_INCLUDE_DIRS}
        REQUIRES ${RMNG_MATTER_MIRROR_REQUIRES}
        PRIV_REQUIRES esp_matter nvs_flash mbedtls
    )

    if (CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_CHIPOBLE_UNUSED)
        message(WARNING "On-network onboarding never commissions over CHIPoBLE. Turn off "
                        "CONFIG_ENABLE_CHIPOBLE to reclaim its flash."
        )
    endif ()

    # The mirror serves the endpoint correlation entry by supplying the device info provider itself. Both options are
    # needed and both sit in esp-matter choices, which Kconfig will not let another component select.
    if (NOT CONFIG_CUSTOM_DEVICE_INFO_PROVIDER)
        message(
            WARNING "The rmng.device FixedLabel entry correlating a device with its endpoint is served only when the "
                    "mirror provides the device info provider. Set CONFIG_CUSTOM_DEVICE_INFO_PROVIDER=y and "
                    "CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER=y; without it clients fall back to the endpoint "
                    "ordering contract."
        )
    endif ()

    # Advisory: Matter clusters compiled into esp-matter that the mirror never instantiates. esp-matter defaults new
    # ones to y, so a product's exclusion list goes stale as it grows. Tidiness rather than flash - an unreferenced
    # cluster is already discarded by --gc-sections. Not a warning: an application may drive a cluster itself, and the
    # commissioning/diagnostics infrastructure is required whatever the mapping says.
    include("${CMAKE_CURRENT_LIST_DIR}/src/port/clusters/cluster_kconfig.cmake")
    rm_mirror_report_unused_clusters()

    idf_build_get_property(python PYTHON)

    add_custom_command(
        OUTPUT ${RM_MIRROR_GEN_SRCS} ${RM_MIRROR_GEN_HDR} ${RM_MIRROR_GEN_CHECK_HDR}
        COMMAND
            ${python} "${RM_MIRROR_GEN_SCRIPT}" ${RM_MIRROR_MAPPING_ARGS} --output-c "${RM_MIRROR_GEN_SRCS}" --output-h
            "${RM_MIRROR_GEN_HDR}" --output-check "${RM_MIRROR_GEN_CHECK_HDR}" ${RM_MIRROR_CLUSTER_CHECK_ARGS}
        DEPENDS ${RM_MIRROR_MAPPING_LIBS} "${RM_MIRROR_PROFILE_JSON}" ${RM_MIRROR_GEN_DEPS}
        COMMENT "Generating Matter mirror mapping tables"
    )

    # Normal CMake Platform ###
else ()
    # POSIX: engine-only static library, for unit tests (no esp-matter on POSIX).
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(
        OUTPUT ${RM_MIRROR_GEN_SRCS} ${RM_MIRROR_GEN_HDR} ${RM_MIRROR_GEN_CHECK_HDR}
        COMMAND
            ${Python3_EXECUTABLE} "${RM_MIRROR_GEN_SCRIPT}" ${RM_MIRROR_MAPPING_ARGS} --output-c "${RM_MIRROR_GEN_SRCS}"
            --output-h "${RM_MIRROR_GEN_HDR}" --output-check "${RM_MIRROR_GEN_CHECK_HDR}"
            ${RM_MIRROR_CLUSTER_CHECK_ARGS}
        DEPENDS ${RM_MIRROR_MAPPING_LIBS} "${RM_MIRROR_PROFILE_JSON}" ${RM_MIRROR_GEN_DEPS}
        COMMENT "Generating Matter mirror mapping tables"
    )

    add_library(${RMNG_MATTER_MIRROR_NAME} STATIC ${RMNG_MATTER_MIRROR_ENGINE_SRCS})
    target_include_directories(
        ${RMNG_MATTER_MIRROR_NAME} PUBLIC ${RMNG_MATTER_MIRROR_INCLUDE_DIRS} ${RMNG_MATTER_MIRROR_PRIV_INCLUDE_DIRS}
    )
    target_link_libraries(${RMNG_MATTER_MIRROR_NAME} PUBLIC esp_rmaker_neo)

    option(TEST_ESP_RMAKER_NEO_MATTER_MIRROR "Enable testing for esp_rmaker_neo_matter_mirror" OFF)
    if (TEST_ESP_RMAKER_NEO_MATTER_MIRROR)
        add_subdirectory(test_rmaker_neo_matter_mirror)
    endif ()
endif ()
