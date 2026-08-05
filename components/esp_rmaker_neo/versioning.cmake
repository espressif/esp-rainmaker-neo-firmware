# This is where the versioning is handled for the RMNG component.

# Current: v0.8.0
set(RMNG_VERSION_MAJOR 0)
set(RMNG_VERSION_MINOR 8)
set(RMNG_VERSION_PATCH 0)
# empty string for release, "-<type>" for pre-release
set(RMNG_VERSION_TYPE "")

set(_rmng_full_version "${RMNG_VERSION_MAJOR}.${RMNG_VERSION_MINOR}.${RMNG_VERSION_PATCH}${RMNG_VERSION_TYPE}")

# If sitting on a git tag matching the full version, leave TYPE as-is. Else, append "-<short-sha>".
find_package(Git QUIET)
if (Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_CURRENT_LIST_DIR} tag --points-at HEAD
        OUTPUT_VARIABLE _rmng_head_tags
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        RESULT_VARIABLE _rmng_git_rc
    )

    set(_rmng_tag_match FALSE)
    if (_rmng_git_rc EQUAL 0 AND _rmng_head_tags)
        string(REPLACE "\n" ";" _rmng_head_tag_list "${_rmng_head_tags}")
        foreach (_tag IN LISTS _rmng_head_tag_list)
            if (_tag STREQUAL "${_rmng_full_version}" OR _tag STREQUAL "v${_rmng_full_version}")
                set(_rmng_tag_match TRUE)
                break()
            endif ()
        endforeach ()
    endif ()

    if (NOT _rmng_tag_match)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_CURRENT_LIST_DIR} rev-parse --short HEAD
            OUTPUT_VARIABLE _rmng_sha
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        )
        if (_rmng_sha)
            set(RMNG_VERSION_TYPE "-${_rmng_sha}")
        endif ()
    endif ()

    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_CURRENT_LIST_DIR} status --porcelain
        OUTPUT_VARIABLE _rmng_dirty
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )
    if (_rmng_dirty)
        set(RMNG_VERSION_TYPE "${RMNG_VERSION_TYPE}*")
    endif ()
endif ()

# Generate the version header into the build tree, not the source tree: writing it next to the template made every
# configure dirty the worktree (the version embeds the short SHA and a "*" when dirty, so the file changes as you work)
# and made a read-only checkout unbuildable. RMNG_VERSION_INCLUDE_DIR is what sources.cmake puts on the include path.
set(RMNG_VERSION_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/versioning")
configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/include/versioning/esp_rmaker_version.h.in
    ${RMNG_VERSION_INCLUDE_DIR}/esp_rmaker_version.h
)
