# Provides the esp-aws-iot source tree that rmng-sdk sources its AWS IoT libraries from.
#
# rmng-sdk no longer vendors the AWS libraries individually; it consumes them from a single esp-aws-iot checkout,
# fetched with FetchContent into third_party/esp-aws-iot (submodules included) like the other third-party sources.

include_guard(DIRECTORY)

include("${CMAKE_CURRENT_LIST_DIR}/fetch_helpers.cmake")
include(FetchContent)

set(RMNG_ESP_AWS_IOT_REPOSITORY
    "https://github.com/espressif/esp-aws-iot"
    CACHE STRING "esp-aws-iot Git repository rmng-sdk sources its AWS IoT libraries from"
)
# No upstream release contains this commit: it is an untagged esp-aws-iot master commit, picked because it is the first
# to carry the coreMQTT/Jobs versions rmng-sdk builds against. When re-pinning, move to a tagged release if one has
# appeared since, and re-check the Jobs API version (esp_rmaker_neo_ota targets Jobs 1.5.1) before bumping.
set(RMNG_ESP_AWS_IOT_GIT_REF
    "9c879feedb699611e7c220b93aed84c84322ff84"
    CACHE STRING "esp-aws-iot Git ref to check out: branch, tag, or full commit SHA"
)

# esp-aws-iot carries 11 library submodules; rmng-sdk builds these six. Cloning the rest (corePKCS11, coreHTTP,
# Device-Defender, Device-Shadow, Fleet-Provisioning) is pure fetch cost, so they are filtered out below. None of the
# six has submodules of its own today, but GIT_SUBMODULES_RECURSE stays ON so that nesting added upstream is picked up.
set(RMNG_ESP_AWS_IOT_SUBMODULES
    "libraries/backoffAlgorithm/backoffAlgorithm"
    "libraries/coreMQTT/coreMQTT"
    "libraries/coreMQTT-Agent/coreMQTT-Agent"
    "libraries/coreJSON/coreJSON"
    "libraries/Jobs-for-AWS-IoT-embedded-sdk/Jobs-for-AWS-IoT-embedded-sdk"
    "libraries/aws-iot-core-mqtt-file-streams-embedded-c/aws-iot-core-mqtt-file-streams-embedded-c"
)

# ${CMAKE_CURRENT_LIST_DIR} == <repo root>/cmake
get_filename_component(RMNG_ESP_AWS_IOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/esp-aws-iot" ABSOLUTE)

# ESP-IDF's requirement-expansion pass runs component listfiles in script mode, where FetchContent_Declare's
# define_property call is illegal. Export the paths and let the real configure pass do the fetching.
if (CMAKE_BUILD_EARLY_EXPANSION OR CMAKE_SCRIPT_MODE_FILE)
    set(RMNG_ESP_AWS_IOT_LIBS_DIR "${RMNG_ESP_AWS_IOT_DIR}/libraries")
    return()
endif ()

# Fetch once per configure run (this file is included from several component scopes). A full commit SHA is enforced on
# every configure: a checkout sitting on a different commit gets re-fetched. A branch/tag is a moving ref, so an
# existing working tree is left alone (pull it manually to advance it).
get_property(_rmng_eai_done GLOBAL PROPERTY RMNG_ESP_AWS_IOT_FETCHED)
if (NOT _rmng_eai_done)
    rmng_fetchcontent_acquire_lock("${RMNG_ESP_AWS_IOT_DIR}" _rmng_eai_lock)
    rmng_git_working_tree_current_commit("${RMNG_ESP_AWS_IOT_DIR}" _rmng_eai_git)
    # CMake regex has no {n} repetition, so match the charset and check the length separately.
    set(_rmng_eai_ref_is_sha FALSE)
    string(LENGTH "${RMNG_ESP_AWS_IOT_GIT_REF}" _rmng_eai_ref_len)
    if (_rmng_eai_ref_len EQUAL 40 AND RMNG_ESP_AWS_IOT_GIT_REF MATCHES "^[0-9a-fA-F]+$")
        set(_rmng_eai_ref_is_sha TRUE)
    endif ()
    # An interrupted clone leaves the superproject at the right commit with libraries/ present but the submodule dirs
    # empty, which neither check above notices: the configure then dies much later on a missing
    # backoffAlgorithmFilePaths.cmake. Treat any empty consumed submodule as an incomplete checkout and re-fetch.
    set(_rmng_eai_submodules_ok TRUE)
    foreach (_rmng_eai_sm IN LISTS RMNG_ESP_AWS_IOT_SUBMODULES)
        file(GLOB _rmng_eai_sm_content "${RMNG_ESP_AWS_IOT_DIR}/${_rmng_eai_sm}/*")
        if (NOT _rmng_eai_sm_content)
            set(_rmng_eai_submodules_ok FALSE)
            break()
        endif ()
    endforeach ()
    if (NOT _rmng_eai_git
        OR NOT EXISTS "${RMNG_ESP_AWS_IOT_DIR}/libraries"
        OR NOT _rmng_eai_submodules_ok
        OR (_rmng_eai_ref_is_sha AND NOT _rmng_eai_git STREQUAL RMNG_ESP_AWS_IOT_GIT_REF)
    )
        find_package(Git REQUIRED)
        message(STATUS "rmng-sdk: Fetching esp-aws-iot ${RMNG_ESP_AWS_IOT_GIT_REF} from ${RMNG_ESP_AWS_IOT_REPOSITORY}")
        FetchContent_Declare(
            rmng_esp_aws_iot_fc
            GIT_REPOSITORY "${RMNG_ESP_AWS_IOT_REPOSITORY}"
            GIT_TAG "${RMNG_ESP_AWS_IOT_GIT_REF}"
            GIT_SUBMODULES
                ${RMNG_ESP_AWS_IOT_SUBMODULES}
                GIT_SUBMODULES_RECURSE
                ON
                SOURCE_DIR
                "${RMNG_ESP_AWS_IOT_DIR}"
                BINARY_DIR
                "${CMAKE_BINARY_DIR}/_deps/rmng_esp_aws_iot-build"
        )
        FetchContent_MakeAvailable(rmng_esp_aws_iot_fc)
    endif ()
    rmng_fetchcontent_release_lock("${_rmng_eai_lock}")
    set_property(GLOBAL PROPERTY RMNG_ESP_AWS_IOT_FETCHED TRUE)
endif ()

if (NOT EXISTS "${RMNG_ESP_AWS_IOT_DIR}/libraries")
    message(FATAL_ERROR "rmng-sdk: esp-aws-iot fetch into '${RMNG_ESP_AWS_IOT_DIR}' produced no libraries/ dir "
                        "(repository '${RMNG_ESP_AWS_IOT_REPOSITORY}', ref '${RMNG_ESP_AWS_IOT_GIT_REF}')."
    )
endif ()

# Fail here, naming the submodule, rather than later on a missing *FilePaths.cmake from a library listfile.
foreach (_rmng_eai_sm IN LISTS RMNG_ESP_AWS_IOT_SUBMODULES)
    file(GLOB _rmng_eai_sm_content "${RMNG_ESP_AWS_IOT_DIR}/${_rmng_eai_sm}/*")
    if (NOT _rmng_eai_sm_content)
        message(FATAL_ERROR "rmng-sdk: esp-aws-iot submodule '${_rmng_eai_sm}' is empty under "
                            "'${RMNG_ESP_AWS_IOT_DIR}'. Delete that directory tree and re-configure to re-fetch."
        )
    endif ()
endforeach ()

set(RMNG_ESP_AWS_IOT_LIBS_DIR "${RMNG_ESP_AWS_IOT_DIR}/libraries")
