# RMNG helpers to import ESP components via idf-component-manager without IDF
#
# ~~~
# Usage -- the include path is relative to the *including* file:
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/esp_component_manager.cmake)
#
#   rmng_idf_components_import(CMP_OUT DEPENDENCIES "espressif/cbor:~1.0" "espressif/esp-tls:~1.1")
#   message(STATUS "Managed components at: ${CMP_OUT}")
#
# Functions:
#
#   rmng_idf_components_import(OUT_DIR
#                              [DIR <path>]
#                              [RESOLUTION <latest|all>]
#                              [DEPENDENCIES <comp:ver> [<comp:ver> ...]])
#     - Prepares its own venv and installs idf-component-manager + PyYAML into it,
#       so the caller passes no venv (see python_venv.cmake for the venv helpers).
#     - Runs: compote registry sync <managed_components_dir>
#               --resolution <latest|all> --component <name><version_spec>
#     - Managed components land in DIR/.idf_managed_components when DIR is given,
#       otherwise in ${CMAKE_BINARY_DIR}/.idf_managed_components.
#     - Sets OUT_DIR (in the caller's scope) to that directory.
# ~~~

function (rmng_idf_components_import OUT_DIR)
    set(_options)
    set(_oneValueArgs DIR RESOLUTION)
    set(_multiValueArgs DEPENDENCIES)
    cmake_parse_arguments(IMP "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

    # Resolve venv python executable
    include(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/python_venv.cmake)
    rmng_python_venv_prepare(VENV DEPS "idf-component-manager")
    rmng_python_get_executable(_venv_python "${VENV}")

    # Ensure idf-component-manager present
    execute_process(
        COMMAND "${_venv_python}" -m pip show idf-component-manager
        RESULT_VARIABLE _show_res
        OUTPUT_QUIET ERROR_QUIET
    )
    if (NOT _show_res EQUAL 0)
        execute_process(COMMAND "${_venv_python}" -m pip install idf-component-manager RESULT_VARIABLE _install_res)
        if (NOT _install_res EQUAL 0)
            message(FATAL_ERROR "Failed to install idf-component-manager in venv: ${VENV}")
        endif ()
    endif ()

    # Ensure PyYAML present (compote imports yaml)
    execute_process(
        COMMAND "${_venv_python}" -m pip show PyYAML
        RESULT_VARIABLE _yaml_show
        OUTPUT_QUIET ERROR_QUIET
    )
    if (NOT _yaml_show EQUAL 0)
        execute_process(COMMAND "${_venv_python}" -m pip install PyYAML RESULT_VARIABLE _yaml_install)
        if (NOT _yaml_install EQUAL 0)
            message(FATAL_ERROR "Failed to install PyYAML in venv: ${VENV}")
        endif ()
    endif ()

    # Determine base dir
    if (IMP_DIR)
        set(_base_dir "${IMP_DIR}")
    else ()
        set(_base_dir "${CMAKE_BINARY_DIR}")
    endif ()
    file(MAKE_DIRECTORY "${_base_dir}")

    foreach (_dep IN LISTS IMP_DEPENDENCIES)
        if (NOT _dep STREQUAL "")
            string(REPLACE ":" ";" _parts "${_dep}")
            list(LENGTH _parts _len)
            if (_len LESS 2)
                message(FATAL_ERROR "Dependency '${_dep}' must be in 'name:version' format")
            endif ()
            list(GET _parts 0 _name)
            list(GET _parts 1 _ver)
        endif ()
    endforeach ()

    # Build compote executable path
    if (WIN32)
        set(_compote_exec "${VENV}/Scripts/compote.exe")
    else ()
        set(_compote_exec "${VENV}/bin/compote")
    endif ()

    # Resolution strategy: default to latest
    if (IMP_RESOLUTION)
        set(_resolution "${IMP_RESOLUTION}")
    else ()
        set(_resolution "latest")
    endif ()
    set(_sync_opts --resolution "${_resolution}")

    # Add recursive option
    list(APPEND _sync_opts --recursive)

    # Build --component args from DEPENDENCIES list
    set(_comp_args)
    set(_requested_ids)
    set(_comp_args)
    foreach (_dep IN LISTS IMP_DEPENDENCIES)
        if (NOT _dep STREQUAL "")
            string(REPLACE ":" ";" _parts "${_dep}")
            list(LENGTH _parts _len)
            if (_len LESS 2)
                message(FATAL_ERROR "Dependency '${_dep}' must be in 'name:version' or 'name:<version_spec>' format")
            endif ()
            list(GET _parts 0 _name)
            list(GET _parts 1 _ver)
            string(REPLACE "/" "__" _id "${_name}")
            list(APPEND _requested_ids "${_id}")
            # If version starts with a digit, treat as exact and use '=='
            if (_ver MATCHES "^[0-9]")
                set(_spec "${_name}==${_ver}")
            else ()
                # Assume _ver already includes an operator like ^, ~, <=, >=, ==, etc.
                set(_spec "${_name}${_ver}")
            endif ()
            list(APPEND _comp_args "${_spec}")
        endif ()
    endforeach ()

    # Ensure managed components dir exists
    set(_managed_dir "${_base_dir}/.idf_managed_components")
    file(MAKE_DIRECTORY "${_managed_dir}")

    # Filter out already extracted components with matching versions
    set(_components_to_sync)
    foreach (_arg IN LISTS _comp_args)
        # Extract component name from spec (e.g., "espressif/cbor==0.6.1~4" -> "espressif__cbor"). Handles the
        # constraint formats ==, ^, ~, >=, <=, etc.
        if (_arg MATCHES "^([^=^~<>]+)([=^~<>].*)$")
            set(_component_name "${CMAKE_MATCH_1}")
        else ()
            set(_component_name "${_arg}")
        endif ()
        string(STRIP "${_component_name}" _component_name)
        string(REPLACE "/" "__" _component_id "${_component_name}")
        set(_component_dir "${_managed_dir}/${_component_id}")
        set(_version_file "${_component_dir}/.rmng_version_spec")

        # Check if component is already extracted and verify version compatibility
        if (EXISTS "${_component_dir}")
            # Check if component has idf_component.yml with version info
            set(_idf_component_yml "${_component_dir}/idf_component.yml")
            if (EXISTS "${_idf_component_yml}")
                # Parse version from idf_component.yml
                file(READ "${_idf_component_yml}" _yml_content)
                # Look for top-level version line (simple approach)
                string(FIND "${_yml_content}" "\nversion:" _version_pos)
                if (NOT _version_pos EQUAL -1)
                    # Extract everything after "version: " until newline
                    string(SUBSTRING "${_yml_content}" ${_version_pos} -1 _version_line)
                    string(REGEX REPLACE "^\nversion:[ ]*([^\n\r]+).*" "\\1" _installed_version "${_version_line}")
                    string(STRIP "${_installed_version}" _installed_version)

                    # Extract requested version constraint from spec (e.g., "espressif/cbor==0.6.1~4" -> "==0.6.1~4")
                    if (_arg MATCHES "^[^=^~<>]+([=^~<>].*)$")
                        set(_requested_constraint "${CMAKE_MATCH_1}")

                        # Parse constraint type and version
                        if (_requested_constraint MATCHES "^==(.+)$")
                            # Exact version match required
                            set(_constraint_type "exact")
                            set(_constraint_version "${CMAKE_MATCH_1}")
                        elseif (_requested_constraint MATCHES "^\\^(.*)$")
                            # Caret constraint - allow compatible minor/patch versions
                            set(_constraint_type "caret")
                            set(_constraint_version "${CMAKE_MATCH_1}")
                        elseif (_requested_constraint MATCHES "^~(.*)$")
                            # Tilde constraint - allow patch-level changes
                            set(_constraint_type "tilde")
                            set(_constraint_version "${CMAKE_MATCH_1}")
                        else ()
                            # Other constraints - be conservative and re-sync
                            set(_constraint_type "other")
                            set(_constraint_version "${_requested_constraint}")
                        endif ()

                        # Check compatibility based on constraint type
                        set(_is_compatible FALSE)

                        if (_constraint_type STREQUAL "exact")
                            # Exact match required
                            if ("${_installed_version}" STREQUAL "${_constraint_version}")
                                set(_is_compatible TRUE)
                            endif ()
                        elseif (_constraint_type STREQUAL "caret")
                            # Caret constraint: ^1.2.3 allows 1.x.x but not 2.x.x. Simple check: same major version.
                            string(REGEX MATCH "^([0-9]+)\\." _installed_major "${_installed_version}")
                            string(REGEX MATCH "^([0-9]+)\\." _constraint_major "${_constraint_version}")
                            if ("${_installed_major}" STREQUAL "${_constraint_major}")
                                set(_is_compatible TRUE)
                            endif ()
                        elseif (_constraint_type STREQUAL "tilde")
                            # Tilde constraint: ~1.2.3 allows 1.2.x but not 1.3.x. Simple check: same major.minor.
                            string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\." _installed_major_minor "${_installed_version}")
                            string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\." _constraint_major_minor
                                         "${_constraint_version}"
                            )
                            if ("${_installed_major_minor}" STREQUAL "${_constraint_major_minor}")
                                set(_is_compatible TRUE)
                            endif ()
                        else ()
                            # For unknown constraints, be conservative and re-sync
                            set(_is_compatible FALSE)
                        endif ()

                        if (_is_compatible)
                            message(
                                STATUS
                                    "Component '${_component_name}' already extracted with compatible version '${_installed_version}' for constraint '${_requested_constraint}', skipping sync"
                            )
                        else ()
                            message(
                                WARNING
                                    "Component '${_component_name}' exists with version '${_installed_version}' but requested constraint '${_requested_constraint}' may not be satisfied. Re-syncing."
                            )
                            list(APPEND _components_to_sync "${_arg}")
                        endif ()
                    else ()
                        message(
                            STATUS
                                "Component '${_component_name}' exists with version '${_installed_version}' (unable to parse constraint from '${_arg}'), assuming compatible"
                        )
                    endif ()
                else ()
                    message(
                        STATUS
                            "Component '${_component_name}' exists but no version found in idf_component.yml, re-syncing for safety"
                    )
                    list(APPEND _components_to_sync "${_arg}")
                endif ()
            else ()
                message(
                    STATUS
                        "Component '${_component_name}' exists but no idf_component.yml found (legacy install), re-syncing for safety"
                )
                list(APPEND _components_to_sync "${_arg}")
            endif ()
        else ()
            # Component not extracted yet
            list(APPEND _components_to_sync "${_arg}")
        endif ()
    endforeach ()

    # Sync and extract components
    foreach (_arg IN LISTS _components_to_sync)
        message(
            STATUS
                "Syncing components via compote: ${_compote_exec} registry sync [${_sync_opts}] --component ${_arg} ${_managed_dir}"
        )
        execute_process(
            COMMAND "${_compote_exec}" registry sync ${_sync_opts} --component ${_arg} "${_managed_dir}"
            RESULT_VARIABLE _sync_res
            OUTPUT_VARIABLE _sync_out
            ERROR_VARIABLE _sync_err
        )
        if (NOT _sync_res EQUAL 0)
            message(STATUS "compote registry sync output:\n${_sync_out}")
            message(STATUS "compote registry sync error:\n${_sync_err}")
            message(FATAL_ERROR "Failed to sync components via compote")
        endif ()

        # Extract component archives immediately after sync
        file(GLOB_RECURSE _component_zips "${_managed_dir}/components/*/*/*/*.zip")
        foreach (_zip_file IN LISTS _component_zips)
            get_filename_component(_zip_base "${_zip_file}" NAME_WE)
            string(REGEX REPLACE "-v.*$" "" _id "${_zip_base}")
            set(_dest_dir "${_managed_dir}/${_id}")

            # Make fresh destination directory
            file(REMOVE_RECURSE "${_dest_dir}")
            file(MAKE_DIRECTORY "${_dest_dir}")

            set(_pycode
                "import zipfile,sys,os; z=sys.argv[1]; d=sys.argv[2]; os.makedirs(d,exist_ok=True); zipfile.ZipFile(z).extractall(d)"
            )
            execute_process(
                COMMAND "${_venv_python}" -c "${_pycode}" "${_zip_file}" "${_dest_dir}"
                RESULT_VARIABLE _unzip_res
                OUTPUT_VARIABLE _unzip_out
                ERROR_VARIABLE _unzip_err
            )
            if (NOT _unzip_res EQUAL 0)
                message(STATUS "zip extract output:\n${_unzip_out}")
                message(STATUS "zip extract error:\n${_unzip_err}")
                message(FATAL_ERROR "Failed to extract ${_zip_file} to ${_dest_dir}")
            endif ()

        endforeach ()

        # Clean up archive folder after each component
        file(REMOVE_RECURSE "${_managed_dir}/components")
    endforeach ()
    set(${OUT_DIR}
        "${_managed_dir}"
        PARENT_SCOPE
    )
endfunction ()
