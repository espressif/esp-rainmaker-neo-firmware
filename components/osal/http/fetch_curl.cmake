# Used only from http-common/CMakeLists.txt (POSIX). Fetches curl then FetchContent_MakeAvailable adds it.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/fetch_helpers.cmake")
include(FetchContent)

set(_RMNG_HTTP_CURL_TAG "2eebc58c4b8d68c98c8344381a9f6df4cca838fd")
# Capture at file parse time; CMAKE_CURRENT_LIST_DIR inside function body resolves to caller's listfile.
set(_RMNG_HTTP_CURL_SCRIPT_DIR "${CMAKE_CURRENT_LIST_DIR}")

function (http_common_curl_make_available)
    if (TARGET CURL::libcurl OR TARGET libcurl)
        return()
    endif ()
    set(_curl_src "${_RMNG_HTTP_CURL_SCRIPT_DIR}/libraries/curl")
    set(_curl_bin "${CMAKE_BINARY_DIR}/posix/lib/curl")
    rmng_fetchcontent_acquire_lock("${_curl_src}" _curl_lock)
    rmng_git_working_tree_current_commit("${_curl_src}" _curl_src_ready)
    if (_curl_src_ready)
        add_subdirectory("${_curl_src}" "${_curl_bin}")
        rmng_fetchcontent_release_lock("${_curl_lock}")
        return()
    endif ()
    if (NOT EXISTS "${_curl_src}/CMakeLists.txt")
        find_package(Git REQUIRED)
        message(STATUS "rmng-sdk: Fetching curl")
    endif ()
    FetchContent_Declare(
        rmng_curl_fc
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG ${_RMNG_HTTP_CURL_TAG}
        SOURCE_DIR "${_curl_src}" BINARY_DIR "${_curl_bin}"
    )
    FetchContent_MakeAvailable(rmng_curl_fc)
    rmng_fetchcontent_release_lock("${_curl_lock}")
endfunction ()
