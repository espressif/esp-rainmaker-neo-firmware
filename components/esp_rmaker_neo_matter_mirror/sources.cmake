# Generated mapping tables (see mapping/ + scripts/gen_mapping_table.py)
set(RM_MIRROR_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(RM_MIRROR_GEN_SRCS "${RM_MIRROR_GEN_DIR}/rm_mirror_mapping_gen.c")
set(RM_MIRROR_GEN_HDR "${RM_MIRROR_GEN_DIR}/rm_mirror_mapping_gen.h")

# Cross-check included by the port after its per-cluster factories: a mapping needing a cluster the port cannot build
# fails the build here rather than at init, unless the application registers factories at runtime.
set(RM_MIRROR_GEN_CHECK_HDR "${RM_MIRROR_GEN_DIR}/rm_mirror_cluster_check_gen.h")
if (CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTERNAL_CLUSTERS)
    set(RM_MIRROR_CLUSTER_CHECK_ARGS --allow-external-clusters)
else ()
    set(RM_MIRROR_CLUSTER_CHECK_ARGS "")
endif ()
set(RM_MIRROR_GEN_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/scripts/gen_mapping_table.py")

# The generator plus the shared validator it imports (scripts/mapping/ is a copy of the canonical mapping repo; see
# mapping/SOURCE). Any of them changing re-runs the generation.
set(RM_MIRROR_GEN_DEPS
    "${RM_MIRROR_GEN_SCRIPT}" "${CMAKE_CURRENT_LIST_DIR}/scripts/mapping/__init__.py"
    "${CMAKE_CURRENT_LIST_DIR}/scripts/mapping/vocabulary.py" "${CMAKE_CURRENT_LIST_DIR}/scripts/mapping/validate.py"
)

# Golden vectors: a copied input, not a build product. The C form the POSIX tests read is lowered from this file, so the
# committed data stays the arbiter for every consumer.
set(RM_MIRROR_VECTORS_JSON "${CMAKE_CURRENT_LIST_DIR}/mapping/vectors/rmng_matter_mapping.vectors.json")
set(RM_MIRROR_GEN_VECTORS_HDR "${RM_MIRROR_GEN_DIR}/rm_mirror_vectors_gen.h")

# The mapping library: every capability, composite and device-type rule the mirror knows. A product extends the
# vocabulary with its own library files through CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTRA_LIBS, merged after this one so
# a clash names the shipped file as the original.
set(RM_MIRROR_MAPPING_LIBS "${CMAKE_CURRENT_LIST_DIR}/mapping/lib/rmng_matter_mapping.json")

foreach (rm_mirror_extra_lib IN LISTS CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTRA_LIBS)
    get_filename_component(rm_mirror_extra_lib "${rm_mirror_extra_lib}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if (NOT EXISTS "${rm_mirror_extra_lib}")
        message(FATAL_ERROR "Matter mirror library file ${rm_mirror_extra_lib} does not exist. Set it via "
                            "CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_EXTRA_LIBS."
        )
    endif ()
    list(APPEND RM_MIRROR_MAPPING_LIBS "${rm_mirror_extra_lib}")
endforeach ()

# Synthetic entries for vocabulary no shipped device type uses yet, so the unit tests can exercise it.
if (TEST_ESP_RMAKER_NEO_MATTER_MIRROR)
    list(APPEND RM_MIRROR_MAPPING_LIBS
         "${CMAKE_CURRENT_LIST_DIR}/test_rmaker_neo_matter_mirror/mapping/test_vocabulary.json"
    )
endif ()

# Build-time subsetting: the profile names the device types to compile in, and library entries nothing kept references
# are dropped from the generated tables. Selected by the Kconfig choice; a shipped profile resolves to
# mapping/profiles/<name>.json, a custom path is taken relative to the project directory.
if (CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_PROFILE_LIGHT)
    set(rm_mirror_profile "light")
elseif (CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_PROFILE_CUSTOM)
    set(rm_mirror_profile "${CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_PROFILE_PATH}")
elseif (TEST_ESP_RMAKER_NEO_MATTER_MIRROR)
    set(rm_mirror_profile "${CMAKE_CURRENT_LIST_DIR}/test_rmaker_neo_matter_mirror/mapping/test_profile.json")
else ()
    set(rm_mirror_profile "all")
endif ()

if (rm_mirror_profile MATCHES "[/\\]" OR rm_mirror_profile MATCHES "\\.json$")
    get_filename_component(RM_MIRROR_PROFILE_JSON "${rm_mirror_profile}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
else ()
    set(RM_MIRROR_PROFILE_JSON "${CMAKE_CURRENT_LIST_DIR}/mapping/profiles/${rm_mirror_profile}.json")
endif ()

if (NOT EXISTS "${RM_MIRROR_PROFILE_JSON}")
    message(FATAL_ERROR "Matter mirror profile '${rm_mirror_profile}' resolves to ${RM_MIRROR_PROFILE_JSON}, "
                        "which does not exist. Set it via CONFIG_ESP_RMAKER_NEO_MATTER_MIRROR_PROFILE."
    )
endif ()

set(RM_MIRROR_LIB_ARGS "")
foreach (rm_mirror_lib IN LISTS RM_MIRROR_MAPPING_LIBS)
    list(APPEND RM_MIRROR_LIB_ARGS --lib "${rm_mirror_lib}")
endforeach ()
set(RM_MIRROR_MAPPING_ARGS ${RM_MIRROR_LIB_ARGS} --profile "${RM_MIRROR_PROFILE_JSON}")

# Platform-neutral engine (plain C; POSIX-unit-testable against a fake port), with the public helpers that need no port
set(RMNG_MATTER_MIRROR_ENGINE_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/src/rm_mirror_std_params.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/rm_mirror_devices.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/engine/rm_mirror_transform.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/engine/rm_mirror_color.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/engine/rm_mirror_table.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/engine/rm_mirror_lowering.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/engine/rm_mirror_endpoint_map.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/engine/rm_mirror_bridge.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/engine/rm_mirror_sync.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/engine/rm_mirror_onboarding.c"
    ${RM_MIRROR_GEN_SRCS}
)

# esp-matter port (C++ mirror port plus the DAC-backed credential providers)
set(RMNG_MATTER_MIRROR_PORT_SRCS "${CMAKE_CURRENT_LIST_DIR}/src/port/rm_mirror_port_esp_matter.cpp"
                                 "${CMAKE_CURRENT_LIST_DIR}/src/port/rm_mirror_credentials_esp_matter.c"
)

set(RMNG_MATTER_MIRROR_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")
set(RMNG_MATTER_MIRROR_PRIV_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/priv_include" "${RM_MIRROR_GEN_DIR}")
