# RMNG common CMake helpers for Python virtual environments
#
# ~~~
# Usage -- the include path is relative to the *including* file:
#
#   include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/python_venv.cmake)
#
#   rmng_python_venv_prepare(VENV DEPS "idf-component-manager;requests")
#   rmng_python_venv_run(${VENV} MODULE mypkg.tools FUNCTION generate
#                        ARGS "--out;${CMAKE_BINARY_DIR}/gen")
#
# Functions:
#
#   rmng_python_venv_prepare(OUT_VENV [DEPS <pkg1;pkg2;...>] [PYTHON_EXECUTABLE <path>])
#     - Creates or reuses a venv at ${CMAKE_BINARY_DIR}/.venv
#     - Upgrades pip/setuptools/wheel
#     - Ensures DEPS are installed (installs only if missing)
#     - Sets OUT_VENV to the venv path in the caller's scope
#
#   rmng_python_get_executable(OUT_PYTHON_EXECUTABLE VENV)
#     - Sets OUT_PYTHON_EXECUTABLE to the interpreter inside VENV
#
#   rmng_python_venv_run(VENV MODULE <module> FUNCTION <function>
#                        [ARGS <arg1;arg2;...>] [WORKING_DIRECTORY <dir>])
#     - Executes a Python function inside the given venv
# ~~~

function (rmng_python_venv_prepare OUT_VENV)
    set(_options)
    set(_oneValueArgs PYTHON_EXECUTABLE)
    set(_multiValueArgs DEPS)
    cmake_parse_arguments(RMNG "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

    # Find a Python interpreter if not provided
    if (RMNG_PYTHON_EXECUTABLE)
        set(_python_exec "${RMNG_PYTHON_EXECUTABLE}")
    elseif (DEFINED Python3_EXECUTABLE)
        set(_python_exec "${Python3_EXECUTABLE}")
    else ()
        find_package(
            Python3
            COMPONENTS Interpreter
            REQUIRED
        )
        set(_python_exec "${Python3_EXECUTABLE}")
    endif ()

    set(_venv_dir "${CMAKE_BINARY_DIR}/.venv")

    # Create venv if missing
    set(_first_time FALSE)
    if (NOT EXISTS "${_venv_dir}")
        set(_first_time TRUE)
        execute_process(COMMAND "${_python_exec}" -m venv "${_venv_dir}" RESULT_VARIABLE _venv_res)
        if (NOT _venv_res EQUAL 0)
            message(FATAL_ERROR "Failed to create Python venv at ${_venv_dir}")
        endif ()
    endif ()

    # Resolve venv python path (use python -m pip instead of pip executable)
    if (WIN32)
        set(_venv_python "${_venv_dir}/Scripts/python.exe")
    else ()
        set(_venv_python "${_venv_dir}/bin/python")
    endif ()

    # Upgrade packaging tooling for first time only
    if (_first_time)
        execute_process(
            COMMAND "${_venv_python}" -m pip install --upgrade pip setuptools wheel RESULT_VARIABLE _pip_upd_res
        )
        if (NOT _pip_upd_res EQUAL 0)
            message(FATAL_ERROR "Failed to upgrade pip/setuptools/wheel in ${_venv_dir}")
        endif ()
    endif ()

    # Ensure requested deps are present
    foreach (_pkg IN LISTS RMNG_DEPS)
        if (NOT _pkg STREQUAL "")
            execute_process(
                COMMAND "${_venv_python}" -m pip show "${_pkg}"
                RESULT_VARIABLE _show_res
                OUTPUT_QUIET ERROR_QUIET
            )
            if (NOT _show_res EQUAL 0)
                execute_process(COMMAND "${_venv_python}" -m pip install "${_pkg}" RESULT_VARIABLE _install_res)
                if (NOT _install_res EQUAL 0)
                    message(FATAL_ERROR "Failed to install Python package '${_pkg}' in venv ${_venv_dir}")
                endif ()
            endif ()
        endif ()
    endforeach ()

    # Return venv path
    set(${OUT_VENV}
        "${_venv_dir}"
        PARENT_SCOPE
    )
endfunction ()

function (rmng_python_get_executable OUT_PYTHON_EXECUTABLE VENV)
    if (WIN32)
        set(${OUT_PYTHON_EXECUTABLE}
            "${VENV}/Scripts/python.exe"
            PARENT_SCOPE
        )
    else ()
        set(${OUT_PYTHON_EXECUTABLE}
            "${VENV}/bin/python"
            PARENT_SCOPE
        )
    endif ()
endfunction ()

function (rmng_python_venv_run VENV)
    set(_options)
    set(_oneValueArgs MODULE FUNCTION WORKING_DIRECTORY)
    set(_multiValueArgs ARGS)
    cmake_parse_arguments(RUN "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

    if (NOT VENV)
        message(FATAL_ERROR "rmng_python_venv_run: VENV path is required as the first argument")
    endif ()
    if (NOT RUN_MODULE)
        message(FATAL_ERROR "rmng_python_venv_run: MODULE is required")
    endif ()
    if (NOT RUN_FUNCTION)
        message(FATAL_ERROR "rmng_python_venv_run: FUNCTION is required")
    endif ()

    if (WIN32)
        set(_venv_python "${VENV}/Scripts/python.exe")
    else ()
        set(_venv_python "${VENV}/bin/python")
    endif ()

    # Build inline Python snippet
    set(_pycode
        "import importlib,sys; m=importlib.import_module('${RUN_MODULE}'); f=getattr(m,'${RUN_FUNCTION}'); rc=f(*sys.argv[1:]); sys.exit(0 if (rc is None or rc==0) else int(rc))"
    )

    set(_cmd "${_venv_python}" -c "${_pycode}")
    if (RUN_ARGS)
        list(APPEND _cmd ${RUN_ARGS})
    endif ()

    if (RUN_WORKING_DIRECTORY)
        execute_process(
            COMMAND ${_cmd}
            WORKING_DIRECTORY "${RUN_WORKING_DIRECTORY}"
            RESULT_VARIABLE _run_res
        )
    else ()
        execute_process(COMMAND ${_cmd} RESULT_VARIABLE _run_res)
    endif ()

    if (NOT _run_res EQUAL 0)
        message(FATAL_ERROR "rmng_python_venv_run: Python call failed with exit code ${_run_res}")
    endif ()
endfunction ()
