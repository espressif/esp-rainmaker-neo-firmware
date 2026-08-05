# RMNG Menuconfig CMake Integration
#
# ~~~
# Usage -- the include path is relative to the *including* file:
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/menuconfig.cmake)
#
#   # Register components from their own CMakeLists.txt
#   kconfig_register(NAME <component>
#                    KCONFIGS <path1> [<path2> ...]
#                    [CHILDREN <child1> [<child2> ...]])
#
#   # Generate the combined Kconfig and the menuconfig targets, and make global
#   # sdkconfig.h / sdkconfig.cmake a dependency of every kconfig_register()ed component
#   rmng_setup_menuconfig(
#       COMPONENT <component_name>
#       [CONFIG_FILE <path>]        # Default: ${CMAKE_BINARY_DIR}/.config.<component>
#   )
#
#   rmng_menuconfig_configure_cmake_variables(
#       COMPONENT <component_name>
#       [CONFIG_FILE <path>]                    # Default: ${CMAKE_BINARY_DIR}/.config.<component>
#       [DEFAULTS_FILE <path1> [<path2> ...]]   # Optional, for single-pass configuration
#       OUTPUT_VARIABLE <variable_name>         # Receives the generated sdkconfig.cmake path
#   )
#
# rmng_setup_menuconfig creates:
#   - menuconfig       : interactive configuration
#   - global_sdkconfig : generates the global sdkconfig.h and sdkconfig.cmake
# ~~~
#
# rmng_menuconfig_configure_cmake_variables generates CONFIG_* CMake variables from .config files and returns the path
# to the generated sdkconfig.cmake file for manual inclusion.
#
# If .config file doesn't exist during build, it generates defaults automatically.

# Helper function to get default paths for menuconfig functions
function (_rmng_menuconfig_get_paths COMPONENT RESULT_CONFIG_FILE RESULT_ROOT_KCONFIG)
    set(${RESULT_CONFIG_FILE}
        "${CMAKE_BINARY_DIR}/.config.${COMPONENT}"
        PARENT_SCOPE
    )
    set(${RESULT_ROOT_KCONFIG}
        "${CMAKE_BINARY_DIR}/kconfig/${COMPONENT}/Kconfig"
        PARENT_SCOPE
    )
endfunction ()

function (kconfig_register)
    cmake_parse_arguments(KC "" "NAME" "KCONFIGS;CHILDREN" ${ARGN})

    if (NOT KC_NAME OR NOT KC_KCONFIGS)
        message(FATAL_ERROR "kconfig_register(NAME <name> KCONFIGS <path1> [path2 ...] [CHILDREN ...])")
    endif ()

    # Normalize all Kconfig paths to absolute paths and forward slashes
    set(_normalized_paths)
    foreach (_kconfig IN LISTS KC_KCONFIGS)
        if (NOT IS_ABSOLUTE "${_kconfig}")
            set(_abs "${CMAKE_CURRENT_SOURCE_DIR}/${_kconfig}")
            get_filename_component(_abs "${_abs}" ABSOLUTE)
        else ()
            set(_abs "${_kconfig}")
        endif ()
        file(TO_CMAKE_PATH "${_abs}" _abs)
        list(APPEND _normalized_paths "${_abs}")
    endforeach ()

    # Register component metadata
    get_property(_all GLOBAL PROPERTY KCONFIG_COMPONENTS)
    list(FIND _all "${KC_NAME}" _idx)
    if (_idx EQUAL -1)
        set_property(GLOBAL APPEND PROPERTY KCONFIG_COMPONENTS "${KC_NAME}")
    endif ()
    set_property(GLOBAL PROPERTY "KCONFIG_PATHS_${KC_NAME}" "${_normalized_paths}")
    set_property(GLOBAL PROPERTY "KCONFIG_CHILDREN_${KC_NAME}" "${KC_CHILDREN}")
endfunction ()

# Internal DFS emitter
function (_kconfig_emit NODE OUT INDENT VISITED_VAR)
    # dup/loop guard (soft): skip if already emitted
    set(_visited "${${VISITED_VAR}}")
    list(FIND _visited "${NODE}" _seen)
    if (NOT _seen EQUAL -1)
        return()
    endif ()

    get_property(_paths GLOBAL PROPERTY "KCONFIG_PATHS_${NODE}")
    if (NOT _paths)
        message(FATAL_ERROR "kconfig: component '${NODE}' not registered")
    endif ()

    get_property(_kids GLOBAL PROPERTY "KCONFIG_CHILDREN_${NODE}")

    # mark visited
    if (_visited)
        set(${VISITED_VAR}
            "${_visited};${NODE}"
            PARENT_SCOPE
        )
    else ()
        set(${VISITED_VAR}
            "${NODE}"
            PARENT_SCOPE
        )
    endif ()

    # emit this node then its children
    file(APPEND "${OUT}" "# ${INDENT}${NODE}\n")
    foreach (_path IN LISTS _paths)
        file(APPEND "${OUT}" "source \"${_path}\"\n")
    endforeach ()

    foreach (_c IN LISTS _kids)
        get_property(_child_paths GLOBAL PROPERTY "KCONFIG_PATHS_${_c}")
        if (_child_paths)
            _kconfig_emit("${_c}" "${OUT}" "  ${INDENT}" "${VISITED_VAR}")
        else ()
            message(STATUS "kconfig: skipping unregistered child component '${_c}'")
        endif ()
    endforeach ()
endfunction ()

function (kconfig_generate)
    cmake_parse_arguments(KCG "" "ROOT;OUTPUT" "" ${ARGN})
    if (NOT KCG_ROOT OR NOT KCG_OUTPUT)
        message(FATAL_ERROR "kconfig_generate(ROOT <name> OUTPUT <file>)")
    endif ()

    get_filename_component(_out "${KCG_OUTPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    file(WRITE "${_out}" "# Auto-generated Kconfig. Do not edit.\n")

    set(_VISITED "")
    _kconfig_emit("${KCG_ROOT}" "${_out}" "" "_VISITED")

    message(STATUS "Generated Kconfig: ${_out}")
    set(KCONFIG_COMBINED
        "${_out}"
        PARENT_SCOPE
    ) # optional handle for callers
endfunction ()

function (rmng_setup_menuconfig)
    set(_options)
    set(_oneValueArgs COMPONENT CONFIG_FILE)
    cmake_parse_arguments(MENUCONFIG "${_options}" "${_oneValueArgs}" "" ${ARGN})

    if (NOT MENUCONFIG_COMPONENT)
        message(FATAL_ERROR "rmng_setup_menuconfig: COMPONENT is required")
    endif ()

    # Set defaults
    if (NOT MENUCONFIG_CONFIG_FILE)
        _rmng_menuconfig_get_paths(${MENUCONFIG_COMPONENT} MENUCONFIG_CONFIG_FILE _root_kconfig)
    endif ()

    # Prepare Python venv with required packages
    include(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/python_venv.cmake)
    rmng_python_venv_prepare(_menuconfig_venv DEPS "kconfiglib" "esp-idf-kconfig")
    rmng_python_get_executable(_menuconfig_python "${_menuconfig_venv}")

    # Generate combined Kconfig
    if (NOT _root_kconfig)
        _rmng_menuconfig_get_paths(${MENUCONFIG_COMPONENT} _dummy _root_kconfig)
    endif ()
    kconfig_generate(ROOT ${MENUCONFIG_COMPONENT} OUTPUT ${_root_kconfig})

    # Create menuconfig target
    set(_menuconfig_target menuconfig)
    add_custom_target(
        ${_menuconfig_target}
        COMMAND ${CMAKE_COMMAND} -E env KCONFIG_CONFIG=${MENUCONFIG_CONFIG_FILE} MENUCONFIG_COLOR=mono
                ${_menuconfig_python} -m menuconfig ${_root_kconfig}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Configure ${MENUCONFIG_COMPONENT} with menuconfig"
        EXCLUDE_FROM_ALL
        USES_TERMINAL
    )

    # Ensure config file exists during build
    add_custom_command(
        OUTPUT ${MENUCONFIG_CONFIG_FILE}
        COMMAND
            ${CMAKE_COMMAND} -E env ${_menuconfig_python}
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/gen_default_config.py ${_root_kconfig} ${MENUCONFIG_CONFIG_FILE}
        COMMENT "Generate default config for ${MENUCONFIG_COMPONENT}"
    )

    # Create global sdkconfig.h
    set(_global_config_folder "${CMAKE_BINARY_DIR}/config")
    set(_global_sdkconfig_h "${_global_config_folder}/sdkconfig.h")

    add_custom_command(
        OUTPUT ${_global_sdkconfig_h} ${_global_sdkconfig_cmake}
        COMMAND
            ${CMAKE_COMMAND} -E env ${_menuconfig_python} ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/gen_sdkconfig_h.py
            ${MENUCONFIG_CONFIG_FILE} ${_global_sdkconfig_h}
        DEPENDS ${MENUCONFIG_CONFIG_FILE}
        COMMENT "Generate global sdkconfig.h"
    )

    add_custom_target(
        global_sdkconfig_h
        DEPENDS ${_global_sdkconfig_h}
        COMMENT "Generate global sdkconfig.h"
    )

    # Helper function to recursively add global_sdkconfig dependency
    function (_add_global_sdkconfig_h_dependency COMPONENT_NAME)
        if (TARGET ${COMPONENT_NAME})
            add_dependencies(${COMPONENT_NAME} global_sdkconfig_h)

            # Add global config folder to the component if it is not an interface library
            get_target_property(_type ${COMPONENT_NAME} TYPE)
            if (NOT _type STREQUAL "INTERFACE_LIBRARY")
                target_include_directories(${COMPONENT_NAME} PUBLIC ${_global_config_folder})
            endif ()
        endif ()

        get_property(_children GLOBAL PROPERTY "KCONFIG_CHILDREN_${COMPONENT_NAME}")
        foreach (_child IN LISTS _children)
            _add_global_sdkconfig_h_dependency(${_child})
        endforeach ()
    endfunction ()

    # Add global sdkconfig_h dependency to the component and all its descendants
    _add_global_sdkconfig_h_dependency(${MENUCONFIG_COMPONENT})

endfunction ()

function (rmng_menuconfig_configure_cmake_variables)
    set(_options)
    set(_oneValueArgs COMPONENT CONFIG_FILE OUTPUT_VARIABLE)
    set(_multiValueArgs DEFAULTS_FILE)
    cmake_parse_arguments(MENUCONFIG "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

    if (NOT MENUCONFIG_COMPONENT)
        message(FATAL_ERROR "rmng_menuconfig_configure_cmake_variables: COMPONENT is required")
    endif ()

    if (NOT MENUCONFIG_CONFIG_FILE)
        _rmng_menuconfig_get_paths(${MENUCONFIG_COMPONENT} MENUCONFIG_CONFIG_FILE _root_kconfig)
    endif ()

    # Prepare Python venv with required packages
    include(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/python_venv.cmake)
    rmng_python_venv_prepare(_menuconfig_venv DEPS "kconfiglib")
    rmng_python_get_executable(_menuconfig_python "${_menuconfig_venv}")

    # Generate combined Kconfig (assumes components are already registered via kconfig_reg.cmake)
    if (NOT _root_kconfig)
        _rmng_menuconfig_get_paths(${MENUCONFIG_COMPONENT} _dummy _root_kconfig)
    endif ()
    kconfig_generate(ROOT ${MENUCONFIG_COMPONENT} OUTPUT ${_root_kconfig})

    if (NOT EXISTS ${MENUCONFIG_CONFIG_FILE})
        # generate default config
        if (MENUCONFIG_DEFAULTS_FILE)
            # Check if all defaults files exist
            set(_all_exist TRUE)
            foreach (_defaults_file IN LISTS MENUCONFIG_DEFAULTS_FILE)
                if (NOT EXISTS ${_defaults_file})
                    set(_all_exist FALSE)
                    break()
                endif ()
            endforeach ()

            if (_all_exist)
                message(STATUS "Generating default config for ${MENUCONFIG_COMPONENT} from ${MENUCONFIG_DEFAULTS_FILE}")
                execute_process(
                    COMMAND
                        ${CMAKE_COMMAND} -E env ${_menuconfig_python}
                        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/gen_default_config.py ${_root_kconfig}
                        ${MENUCONFIG_CONFIG_FILE} ${MENUCONFIG_DEFAULTS_FILE}
                )
            else ()
                message(
                    STATUS
                        "Some defaults files missing, generating default config for ${MENUCONFIG_COMPONENT} from ${_root_kconfig}"
                )
                execute_process(
                    COMMAND
                        ${CMAKE_COMMAND} -E env ${_menuconfig_python}
                        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/gen_default_config.py ${_root_kconfig}
                        ${MENUCONFIG_CONFIG_FILE}
                )
            endif ()
        else ()
            message(STATUS "Generating default config for ${MENUCONFIG_COMPONENT} from ${_root_kconfig}")
            execute_process(
                COMMAND
                    ${CMAKE_COMMAND} -E env ${_menuconfig_python}
                    ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/gen_default_config.py ${_root_kconfig}
                    ${MENUCONFIG_CONFIG_FILE}
            )
        endif ()
    endif ()

    # Configure CMake variables from the config file
    set(_cmake_config_file "${CMAKE_BINARY_DIR}/config/sdkconfig.cmake")
    execute_process(
        COMMAND
            ${CMAKE_COMMAND} -E env ${_menuconfig_python}
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/gen_sdkconfig_cmake.py ${MENUCONFIG_CONFIG_FILE}
            ${_cmake_config_file}
    )
    if (NOT EXISTS ${_cmake_config_file})
        message(FATAL_ERROR "Failed to generate sdkconfig.cmake")
    endif ()

    # Create a configure-time dependency on the config file. This ensures CMake re-runs if the config changes during
    # builds.
    configure_file(${MENUCONFIG_CONFIG_FILE} ${MENUCONFIG_CONFIG_FILE}.stamp COPYONLY)

    # Set the output variable with the cmake config file path
    if (MENUCONFIG_OUTPUT_VARIABLE)
        set(${MENUCONFIG_OUTPUT_VARIABLE}
            ${_cmake_config_file}
            PARENT_SCOPE
        )
    endif ()
endfunction ()
