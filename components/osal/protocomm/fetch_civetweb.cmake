# Used only from protocomm-posix/CMakeLists.txt. Populates civetweb sources; patched root CMakeLists builds the static
# lib.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/fetch_helpers.cmake")
include(FetchContent)

set(_RMNG_PROTOCOMM_CIVETWEB_TAG "d7ba35bbb649209c66e582d5a0244ba988a15159")
# Capture at file parse time; CMAKE_CURRENT_LIST_DIR inside function body resolves to caller's listfile.
set(_RMNG_PROTOCOMM_CIVETWEB_SCRIPT_DIR "${CMAKE_CURRENT_LIST_DIR}")

function (protocomm_posix_civetweb_make_available)
    set(_cv_src "${_RMNG_PROTOCOMM_CIVETWEB_SCRIPT_DIR}/libraries/civetweb/civetweb")
    set(_cv_bin "${CMAKE_BINARY_DIR}/_deps/rmng_civetweb-build")
    rmng_fetchcontent_acquire_lock("${_cv_src}" _cv_lock)
    rmng_git_working_tree_current_commit("${_cv_src}" _cv_src_ready)
    if (_cv_src_ready OR EXISTS "${_cv_src}/src/civetweb.c")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E copy "${_RMNG_PROTOCOMM_CIVETWEB_SCRIPT_DIR}/libraries/civetweb/CMakeLists.txt"
                    "${_cv_src}/CMakeLists.txt"
        )
        add_subdirectory("${_cv_src}" "${_cv_bin}")
        rmng_fetchcontent_release_lock("${_cv_lock}")
        return()
    endif ()
    find_package(Git REQUIRED)
    message(STATUS "rmng-sdk: Fetching civetweb")
    FetchContent_Declare(
        rmng_civetweb_fc
        GIT_REPOSITORY https://github.com/civetweb/civetweb.git
        GIT_TAG ${_RMNG_PROTOCOMM_CIVETWEB_TAG}
        SOURCE_DIR "${_cv_src}" BINARY_DIR "${_cv_bin}"
        PATCH_COMMAND
            ${CMAKE_COMMAND} -E copy "${_RMNG_PROTOCOMM_CIVETWEB_SCRIPT_DIR}/libraries/civetweb/CMakeLists.txt"
            <SOURCE_DIR>/CMakeLists.txt
    )
    FetchContent_MakeAvailable(rmng_civetweb_fc)
    rmng_fetchcontent_release_lock("${_cv_lock}")
endfunction ()
