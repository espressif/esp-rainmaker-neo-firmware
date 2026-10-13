# Used only from http-common/CMakeLists.txt (POSIX). Fetches curl then FetchContent_MakeAvailable adds it.

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/fetch_helpers.cmake")
include(FetchContent)

# Archive timestamps are set to extraction time, so a re-fetch triggers a rebuild.
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()

# curl: v8.18.0
set(_RMNG_HTTP_CURL_URL "https://github.com/curl/curl/releases/download/curl-8_18_0/curl-8.18.0.tar.xz")
set(_RMNG_HTTP_CURL_SHA256 "40df79166e74aa20149365e11ee4c798a46ad57c34e4f68fd13100e2c9a91946")
set(_RMNG_HTTP_CURL_COMMIT "2eebc58c4b8d68c98c8344381a9f6df4cca838fd")

# Capture at file parse time; CMAKE_CURRENT_LIST_DIR inside function body resolves to caller's listfile.
set(_RMNG_HTTP_CURL_SCRIPT_DIR "${CMAKE_CURRENT_LIST_DIR}")

function (http_common_curl_make_available)
    if (TARGET CURL::libcurl OR TARGET libcurl)
        return()
    endif ()
    set(_curl_src "${_RMNG_HTTP_CURL_SCRIPT_DIR}/libraries/curl")
    set(_curl_bin "${CMAKE_BINARY_DIR}/posix/lib/curl")
    rmng_fetchcontent_acquire_lock("${_curl_src}" _curl_lock)
    rmng_fetchcontent_source_ready("${_curl_src}" "${_RMNG_HTTP_CURL_SHA256}" _curl_src_ready)
    if (_curl_src_ready)
        add_subdirectory("${_curl_src}" "${_curl_bin}")
        rmng_fetchcontent_release_lock("${_curl_lock}")
        return()
    endif ()
    rmng_fetchcontent_guard_checkout("${_curl_src}" "${_RMNG_HTTP_CURL_COMMIT}")
    message(STATUS "rmng-sdk: Fetching curl")
    rmng_fetchcontent_reset(rmng_curl_fc)
    FetchContent_Declare(
        rmng_curl_fc
        URL ${_RMNG_HTTP_CURL_URL}
        URL_HASH SHA256=${_RMNG_HTTP_CURL_SHA256}
        SOURCE_DIR "${_curl_src}" BINARY_DIR "${_curl_bin}"
    )
    FetchContent_MakeAvailable(rmng_curl_fc)
    rmng_fetchcontent_stamp_write("${_curl_src}" "${_RMNG_HTTP_CURL_SHA256}")
    rmng_fetchcontent_release_lock("${_curl_lock}")
endfunction ()
