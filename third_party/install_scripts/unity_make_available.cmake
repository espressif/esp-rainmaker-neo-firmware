# Included from POSIX test consumers (esp_rmaker_neo, osal, test_rmaker_neo_ota). Defines unity_make_available().

include("${CMAKE_CURRENT_LIST_DIR}/../third_party_versions.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/fetch_helpers.cmake")
include(FetchContent)

# Archive timestamps are set to extraction time, so a re-fetch triggers a rebuild.
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()

# Capture at file parse time; CMAKE_CURRENT_LIST_DIR inside function body resolves to caller's listfile.
set(_RMNG_UNITY_SCRIPT_DIR "${CMAKE_CURRENT_LIST_DIR}")

function (unity_make_available)
    if (TARGET unity::framework)
        return()
    endif ()
    set(_unity_src "${_RMNG_UNITY_SCRIPT_DIR}/../Unity")
    set(_unity_bin "${CMAKE_BINARY_DIR}/posix/lib/Unity")
    rmng_fetchcontent_acquire_lock("${_unity_src}" _unity_lock)
    rmng_fetchcontent_source_ready("${_unity_src}" "${RMNG_THIRD_PARTY_UNITY_SHA256}" _unity_src_ready)
    if (_unity_src_ready)
        add_subdirectory("${_unity_src}" "${_unity_bin}")
    else ()
        rmng_fetchcontent_guard_checkout("${_unity_src}" "${RMNG_THIRD_PARTY_UNITY_COMMIT}")
        message(STATUS "rmng-sdk: Fetching Unity test framework")
        rmng_fetchcontent_reset(rmng_unity_fc)
        FetchContent_Declare(
            rmng_unity_fc
            URL ${RMNG_THIRD_PARTY_UNITY_URL}
            URL_HASH SHA256=${RMNG_THIRD_PARTY_UNITY_SHA256}
            SOURCE_DIR "${_unity_src}" BINARY_DIR "${_unity_bin}"
        )
        FetchContent_MakeAvailable(rmng_unity_fc)
        rmng_fetchcontent_stamp_write("${_unity_src}" "${RMNG_THIRD_PARTY_UNITY_SHA256}")
    endif ()
    rmng_fetchcontent_release_lock("${_unity_lock}")
endfunction ()
