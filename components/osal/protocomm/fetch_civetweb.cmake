# Used only from protocomm-posix/CMakeLists.txt. Populates civetweb sources; patched root CMakeLists builds the static
# lib.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/fetch_helpers.cmake")
include(FetchContent)

# Archive timestamps are set to extraction time, so a re-fetch triggers a rebuild.
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()

# civetweb: v1.16
set(_RMNG_PROTOCOMM_CIVETWEB_URL "https://github.com/civetweb/civetweb/archive/refs/tags/v1.16.tar.gz")
set(_RMNG_PROTOCOMM_CIVETWEB_SHA256 "f0e471c1bf4e7804a6cfb41ea9d13e7d623b2bcc7bc1e2a4dd54951a24d60285")
set(_RMNG_PROTOCOMM_CIVETWEB_COMMIT "d7ba35bbb649209c66e582d5a0244ba988a15159")

# Capture at file parse time; CMAKE_CURRENT_LIST_DIR inside function body resolves to caller's listfile.
set(_RMNG_PROTOCOMM_CIVETWEB_SCRIPT_DIR "${CMAKE_CURRENT_LIST_DIR}")

function (protocomm_posix_civetweb_make_available)
    set(_cv_src "${_RMNG_PROTOCOMM_CIVETWEB_SCRIPT_DIR}/libraries/civetweb/civetweb")
    set(_cv_bin "${CMAKE_BINARY_DIR}/_deps/rmng_civetweb-build")
    rmng_fetchcontent_acquire_lock("${_cv_src}" _cv_lock)
    rmng_fetchcontent_source_ready("${_cv_src}" "${_RMNG_PROTOCOMM_CIVETWEB_SHA256}" _cv_src_ready)
    if (_cv_src_ready)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E copy "${_RMNG_PROTOCOMM_CIVETWEB_SCRIPT_DIR}/libraries/civetweb/CMakeLists.txt"
                    "${_cv_src}/CMakeLists.txt"
        )
        add_subdirectory("${_cv_src}" "${_cv_bin}")
        rmng_fetchcontent_release_lock("${_cv_lock}")
        return()
    endif ()
    rmng_fetchcontent_guard_checkout("${_cv_src}" "${_RMNG_PROTOCOMM_CIVETWEB_COMMIT}" CMakeLists.txt)
    message(STATUS "rmng-sdk: Fetching civetweb")
    rmng_fetchcontent_reset(rmng_civetweb_fc)
    FetchContent_Declare(
        rmng_civetweb_fc
        URL ${_RMNG_PROTOCOMM_CIVETWEB_URL}
        URL_HASH SHA256=${_RMNG_PROTOCOMM_CIVETWEB_SHA256}
        SOURCE_DIR "${_cv_src}" BINARY_DIR "${_cv_bin}"
        PATCH_COMMAND
            ${CMAKE_COMMAND} -E copy "${_RMNG_PROTOCOMM_CIVETWEB_SCRIPT_DIR}/libraries/civetweb/CMakeLists.txt"
            <SOURCE_DIR>/CMakeLists.txt
    )
    FetchContent_MakeAvailable(rmng_civetweb_fc)
    rmng_fetchcontent_stamp_write("${_cv_src}" "${_RMNG_PROTOCOMM_CIVETWEB_SHA256}")
    rmng_fetchcontent_release_lock("${_cv_lock}")
endfunction ()
