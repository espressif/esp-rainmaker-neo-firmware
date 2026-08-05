include("${CMAKE_CURRENT_LIST_DIR}/../third_party_versions.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/fetch_helpers.cmake")
include(FetchContent)

function (mbedtls_install_requirements MBEDTLS_SRC)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    # PEP 405: in a venv, sys.prefix differs from sys.base_prefix
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c
                "import sys; sys.exit(0 if getattr(sys, 'base_prefix', sys.prefix) != sys.prefix else 1)"
        RESULT_VARIABLE _rmng_mbedtls_py_venv_ok
        OUTPUT_QUIET ERROR_QUIET
    )
    if (NOT _rmng_mbedtls_py_venv_ok EQUAL 0)
        message(
            FATAL_ERROR
                "rmng-sdk: Python used for Mbed TLS (${Python3_EXECUTABLE}) must be a virtualenv interpreter. "
                "Create/activate a venv, ensure `cmake` sees that Python (e.g. PATH or -DPython3_EXECUTABLE=...), then reconfigure."
        )
    endif ()
    if (EXISTS "${MBEDTLS_SRC}/scripts/basic.requirements.txt")
        execute_process(
            COMMAND ${Python3_EXECUTABLE} "${_RMNG_MBEDTLS_SCRIPT_DIR}/../../cmake/scripts/check_requirements.py"
                    "${MBEDTLS_SRC}/scripts/basic.requirements.txt"
            RESULT_VARIABLE _pip_check_result
            OUTPUT_QUIET ERROR_QUIET
        )
        if (NOT _pip_check_result EQUAL 0)
            message(STATUS "rmng-sdk: Installing Python requirements for mbedtls")
            execute_process(
                COMMAND ${Python3_EXECUTABLE} -m pip install -r "${MBEDTLS_SRC}/scripts/basic.requirements.txt"
                RESULT_VARIABLE _pip_install_result
            )
            if (NOT _pip_install_result EQUAL 0)
                message(FATAL_ERROR "Failed to install Python requirements for mbedtls")
            endif ()
        endif ()
    endif ()
endfunction ()

# Capture at file parse time; CMAKE_CURRENT_LIST_DIR inside a function body resolves to the caller's listfile.
set(_RMNG_MBEDTLS_SCRIPT_DIR "${CMAKE_CURRENT_LIST_DIR}")

function (mbedtls_make_available)
    set(_mbedtls_src "${_RMNG_MBEDTLS_SCRIPT_DIR}/../mbedtls")
    # Exported even when the target already exists, so consumers (e.g. osal/http pointing curl at mbedtls headers) never
    # hardcode the fetched-source path.
    set(RMNG_MBEDTLS_INCLUDE_DIR
        "${_mbedtls_src}/include"
        PARENT_SCOPE
    )
    if (TARGET mbedtls)
        return()
    endif ()

    # Function-local; add_subdirectory below inherits these and they are restored on return.
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW) # allow option override
    set(ENABLE_TESTING OFF)
    # Install mbedtls library as position independent code
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    set(_mbedtls_bin "${CMAKE_BINARY_DIR}/posix/lib/mbedtls")
    rmng_fetchcontent_acquire_lock("${_mbedtls_src}" _mbedtls_lock)
    rmng_git_working_tree_current_commit("${_mbedtls_src}" _mbedtls_src_ready)
    if (_mbedtls_src_ready)
        mbedtls_install_requirements("${_mbedtls_src}")
        add_subdirectory("${_mbedtls_src}" "${_mbedtls_bin}")
    else ()
        find_package(Git REQUIRED)
        message(STATUS "rmng-sdk: Fetching Mbed TLS")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        FetchContent_Declare(
            rmng_mbedtls_fc
            GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
            GIT_TAG ${RMNG_THIRD_PARTY_MBEDTLS_TAG}
            GIT_SUBMODULES_RECURSE ON SOURCE_DIR "${_mbedtls_src}" BINARY_DIR "${_mbedtls_bin}"
            PATCH_COMMAND ${Python3_EXECUTABLE} -m pip install -r "scripts/basic.requirements.txt"
        )
        mbedtls_install_requirements("${_mbedtls_src}")
        FetchContent_MakeAvailable(rmng_mbedtls_fc)
    endif ()
    rmng_fetchcontent_release_lock("${_mbedtls_lock}")
endfunction ()
