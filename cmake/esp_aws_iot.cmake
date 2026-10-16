# Fetches the esp-aws-iot archive and its library archives into third_party/esp-aws-iot; pins are in
# third_party/third_party_versions.cmake.

include_guard(DIRECTORY)

include("${CMAKE_CURRENT_LIST_DIR}/../third_party/third_party_versions.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/fetch_helpers.cmake")
include(FetchContent)

# Archive timestamps are set to extraction time, so a re-fetch triggers a rebuild.
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()

# ${CMAKE_CURRENT_LIST_DIR} == <repo root>/cmake
get_filename_component(RMNG_ESP_AWS_IOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/esp-aws-iot" ABSOLUTE)
set(RMNG_ESP_AWS_IOT_LIBS_DIR "${RMNG_ESP_AWS_IOT_DIR}/libraries")

# ESP-IDF's requirement-expansion pass runs component listfiles in script mode, where FetchContent_Declare's
# define_property call is illegal. Export the paths and let the real configure pass do the fetching.
if (CMAKE_BUILD_EARLY_EXPANSION OR CMAKE_SCRIPT_MODE_FILE)
    return()
endif ()

# Fetch once per configure run (this file is included from several component scopes).
get_property(_rmng_eai_done GLOBAL PROPERTY RMNG_ESP_AWS_IOT_FETCHED)
if (NOT _rmng_eai_done)
    rmng_fetchcontent_acquire_lock("${RMNG_ESP_AWS_IOT_DIR}" _rmng_eai_lock)
    rmng_fetchcontent_source_ready("${RMNG_ESP_AWS_IOT_DIR}" "${RMNG_THIRD_PARTY_ESP_AWS_IOT_SHA256}" _rmng_eai_ready)
    if (NOT _rmng_eai_ready)
        rmng_fetchcontent_guard_checkout("${RMNG_ESP_AWS_IOT_DIR}" "${RMNG_THIRD_PARTY_ESP_AWS_IOT_COMMIT}")
        message(STATUS "rmng-sdk: Fetching esp-aws-iot")
        rmng_fetchcontent_reset(rmng_esp_aws_iot_fc)
        FetchContent_Declare(
            rmng_esp_aws_iot_fc
            URL ${RMNG_THIRD_PARTY_ESP_AWS_IOT_URL}
            URL_HASH SHA256=${RMNG_THIRD_PARTY_ESP_AWS_IOT_SHA256}
            SOURCE_DIR "${RMNG_ESP_AWS_IOT_DIR}" BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/rmng_esp_aws_iot-build"
        )
        FetchContent_MakeAvailable(rmng_esp_aws_iot_fc)
        rmng_fetchcontent_stamp_write("${RMNG_ESP_AWS_IOT_DIR}" "${RMNG_THIRD_PARTY_ESP_AWS_IOT_SHA256}")
    endif ()

    # GitHub archives leave submodule directories empty, so each library is a separate archive.
    foreach (_rmng_eai_lib IN LISTS RMNG_THIRD_PARTY_ESP_AWS_IOT_LIBRARIES)
        set(_rmng_eai_lib_dir "${RMNG_ESP_AWS_IOT_LIBS_DIR}/${_rmng_eai_lib}/${_rmng_eai_lib}")
        set(_rmng_eai_lib_sha256 "${RMNG_THIRD_PARTY_ESP_AWS_IOT_${_rmng_eai_lib}_SHA256}")
        rmng_fetchcontent_source_ready("${_rmng_eai_lib_dir}" "${_rmng_eai_lib_sha256}" _rmng_eai_ready)
        if (NOT _rmng_eai_ready)
            message(STATUS "rmng-sdk: Fetching esp-aws-iot library ${_rmng_eai_lib}")
            rmng_fetchcontent_reset(rmng_eai_${_rmng_eai_lib}_fc)
            FetchContent_Declare(
                rmng_eai_${_rmng_eai_lib}_fc
                URL ${RMNG_THIRD_PARTY_ESP_AWS_IOT_${_rmng_eai_lib}_URL}
                URL_HASH SHA256=${_rmng_eai_lib_sha256}
                SOURCE_DIR "${_rmng_eai_lib_dir}" BINARY_DIR
                "${CMAKE_BINARY_DIR}/_deps/rmng_eai_${_rmng_eai_lib}-build"
            )
            FetchContent_MakeAvailable(rmng_eai_${_rmng_eai_lib}_fc)
            rmng_fetchcontent_stamp_write("${_rmng_eai_lib_dir}" "${_rmng_eai_lib_sha256}")
        endif ()
    endforeach ()

    rmng_fetchcontent_release_lock("${_rmng_eai_lock}")
    set_property(GLOBAL PROPERTY RMNG_ESP_AWS_IOT_FETCHED TRUE)
endif ()
