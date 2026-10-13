# Included from POSIX consumers (esp_rmaker_neo, protocomm-posix). Defines protobuf_c_make_available().

include("${CMAKE_CURRENT_LIST_DIR}/../third_party_versions.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/fetch_helpers.cmake")
include(FetchContent)

# Archive timestamps are set to extraction time, so a re-fetch triggers a rebuild.
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()

# Capture at file parse time; CMAKE_CURRENT_LIST_DIR inside a function body resolves to the caller's listfile.
set(_RMNG_PROTOBUF_C_SCRIPT_DIR "${CMAKE_CURRENT_LIST_DIR}")

function (protobuf_c_make_available)
    if (TARGET protobuf-c)
        return()
    endif ()

    # Function-local; add_subdirectory below inherits these and they are restored on return.
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW) # allow option override
    set(ENABLE_TESTING OFF)

    set(_pb_wrapper_dir "${_RMNG_PROTOBUF_C_SCRIPT_DIR}/../protobuf-c")
    set(_pb_src "${_pb_wrapper_dir}/protobuf-c")
    rmng_fetchcontent_acquire_lock("${_pb_src}" _pb_lock)
    rmng_fetchcontent_source_ready("${_pb_src}" "${RMNG_THIRD_PARTY_PROTOBUF_C_SHA256}" _pb_src_ready)
    if (NOT _pb_src_ready)
        rmng_fetchcontent_guard_checkout("${_pb_src}" "${RMNG_THIRD_PARTY_PROTOBUF_C_COMMIT}")
        message(STATUS "rmng-sdk: Fetching protobuf-c")
        rmng_fetchcontent_reset(rmng_protobuf_c_fc)
        FetchContent_Declare(
            rmng_protobuf_c_fc
            URL ${RMNG_THIRD_PARTY_PROTOBUF_C_URL}
            URL_HASH SHA256=${RMNG_THIRD_PARTY_PROTOBUF_C_SHA256}
            SOURCE_DIR "${_pb_src}" BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/rmng_protobuf_c-build"
        )
        FetchContent_MakeAvailable(rmng_protobuf_c_fc)
        rmng_fetchcontent_stamp_write("${_pb_src}" "${RMNG_THIRD_PARTY_PROTOBUF_C_SHA256}")
    endif ()

    # Wrapper CMakeLists builds the static lib from vendored sources
    add_subdirectory("${_pb_wrapper_dir}" "${CMAKE_BINARY_DIR}/posix/lib/protobuf-c")
    rmng_fetchcontent_release_lock("${_pb_lock}")
endfunction ()
