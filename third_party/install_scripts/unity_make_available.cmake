# Included from POSIX test consumers (esp_rmaker_neo, osal, test_rmaker_neo_ota). Defines unity_make_available().

include("${CMAKE_CURRENT_LIST_DIR}/../third_party_versions.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/fetch_helpers.cmake")
include(FetchContent)

# Capture at file parse time; CMAKE_CURRENT_LIST_DIR inside function body resolves to caller's listfile.
set(_RMNG_UNITY_SCRIPT_DIR "${CMAKE_CURRENT_LIST_DIR}")

function (unity_make_available)
    if (TARGET unity::framework)
        return()
    endif ()
    set(_unity_src "${_RMNG_UNITY_SCRIPT_DIR}/../Unity")
    set(_unity_bin "${CMAKE_BINARY_DIR}/posix/lib/Unity")
    rmng_fetchcontent_acquire_lock("${_unity_src}" _unity_lock)
    rmng_git_working_tree_current_commit("${_unity_src}" _unity_src_ready)
    if (_unity_src_ready)
        add_subdirectory("${_unity_src}" "${_unity_bin}")
    else ()
        if (NOT EXISTS "${_unity_src}/src/unity.c")
            find_package(Git REQUIRED)
            message(STATUS "rmng-sdk: Fetching Unity test framework")
        endif ()
        FetchContent_Declare(
            rmng_unity_fc
            GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
            GIT_TAG ${RMNG_THIRD_PARTY_UNITY_TAG}
            SOURCE_DIR "${_unity_src}" BINARY_DIR "${_unity_bin}"
        )
        FetchContent_MakeAvailable(rmng_unity_fc)
    endif ()
    rmng_fetchcontent_release_lock("${_unity_lock}")
endfunction ()
