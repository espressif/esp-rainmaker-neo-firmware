# target_add_binary_data adds binary data into the built target, by converting it to a generated source file which is
# then compiled to a binary object as part of the build

# Capture this script's directory for reliable use inside the function
function (target_add_binary_data target embed_file embed_type)
    set(DATA_FILE_EMBED_ASM_SCRIPT_PATH "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/data_file_embed_asm.cmake")
    cmake_parse_arguments(_ "" "RENAME_TO" "DEPENDS" ${ARGN})

    get_filename_component(embed_file "${embed_file}" ABSOLUTE)

    get_filename_component(name "${embed_file}" NAME)
    set(embed_srcfile "${CMAKE_CURRENT_BINARY_DIR}/${name}.S")

    set(rename_to_arg)
    if (__RENAME_TO) # use a predefined variable name
        set(rename_to_arg -D "VARIABLE_BASENAME=${__RENAME_TO}")
    endif ()

    add_custom_command(
        OUTPUT "${embed_srcfile}"
        COMMAND "${CMAKE_COMMAND}" -D "DATA_FILE=${embed_file}" -D "SOURCE_FILE=${embed_srcfile}" ${rename_to_arg} -D
                "FILE_TYPE=${embed_type}" -P "${DATA_FILE_EMBED_ASM_SCRIPT_PATH}"
        MAIN_DEPENDENCY "${embed_file}"
        DEPENDS "${DATA_FILE_EMBED_ASM_SCRIPT_PATH}" ${__DEPENDS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        VERBATIM
    )

    set_property(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        APPEND
        PROPERTY ADDITIONAL_CLEAN_FILES "${embed_srcfile}"
    )

    target_sources("${target}" PRIVATE "${embed_srcfile}")
    # Ensure CMake treats the generated file as Assembly
    set_source_files_properties("${embed_srcfile}" PROPERTIES GENERATED TRUE LANGUAGE ASM)
endfunction ()
