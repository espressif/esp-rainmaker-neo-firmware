function (testing_enable_coverage TARGET_UNDER_TEST)
    # Add GCC-style coverage flags (supported by Clang) to both the library and the test executable
    if (TARGET ${TARGET_UNDER_TEST})
        target_compile_options(${TARGET_UNDER_TEST} PRIVATE --coverage)
        target_link_options(${TARGET_UNDER_TEST} PRIVATE --coverage)
    else ()
        message(FATAL_ERROR "[testing_enable_coverage] Target under test '${TARGET_UNDER_TEST}' does not exist")
    endif ()

    # Handle different compilers for coverage runtime linking
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # For GCC, explicitly link the gcov library
        target_link_libraries(${TARGET_UNDER_TEST} PRIVATE -lgcov)
    elseif (CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
        # On Apple/Clang, explicitly link the profile runtime to resolve llvm_gcda_* symbols
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
            OUTPUT_VARIABLE CLANG_RESOURCE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        set(PROFILE_RT_LIB "${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.profile_osx.a")
        if (EXISTS "${PROFILE_RT_LIB}")
            target_link_libraries(${TARGET_UNDER_TEST} PRIVATE "${PROFILE_RT_LIB}")
        else ()
            message(
                WARNING
                    "[testing_enable_coverage] libclang_rt.profile_osx.a not found at ${PROFILE_RT_LIB}. Coverage link may fail."
            )
        endif ()
    elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang")
        # For non-Apple Clang, find and link the appropriate profile runtime library
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
            OUTPUT_VARIABLE CLANG_RESOURCE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        # Try different possible profile runtime library names/locations
        set(PROFILE_RT_LIB_CANDIDATES
            "${CLANG_RESOURCE_DIR}/lib/linux/libclang_rt.profile-x86_64.a"
            "${CLANG_RESOURCE_DIR}/lib/libclang_rt.profile-x86_64.a" "${CLANG_RESOURCE_DIR}/lib/libclang_rt.profile.a"
        )

        set(PROFILE_RT_LIB_FOUND FALSE)
        foreach (CANDIDATE ${PROFILE_RT_LIB_CANDIDATES})
            if (EXISTS "${CANDIDATE}")
                target_link_libraries(${TARGET_UNDER_TEST} PRIVATE "${CANDIDATE}")
                set(PROFILE_RT_LIB_FOUND TRUE)
                break()
            endif ()
        endforeach ()

        if (NOT PROFILE_RT_LIB_FOUND)
            message(
                WARNING
                    "[testing_enable_coverage] Could not find Clang profile runtime library in ${CLANG_RESOURCE_DIR}. Coverage link may fail."
            )
        endif ()
    else ()
        message(
            WARNING
                "[testing_enable_coverage] Unknown compiler ${CMAKE_C_COMPILER_ID}. Coverage runtime linking may not work properly."
        )
    endif ()
endfunction ()

function (testing_add_gcovr_coverage_target)
    set(oneValueArgs NAME ROOT_DIR TEST_DIR OUTPUT_DIR)
    set(multiValueArgs EXCLUDES FILTERS)
    cmake_parse_arguments(GCT "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT GCT_ROOT_DIR)
        message(FATAL_ERROR "[testing_add_gcovr_coverage_target] ROOT_DIR is required")
    endif ()
    if (NOT GCT_TEST_DIR)
        message(FATAL_ERROR "[testing_add_gcovr_coverage_target] TEST_DIR is required")
    endif ()
    if (NOT GCT_NAME)
        set(GCT_NAME coverage)
    endif ()
    if (NOT GCT_OUTPUT_DIR)
        set(GCT_OUTPUT_DIR "${CMAKE_BINARY_DIR}/coverage")
    endif ()

    get_filename_component(TESTING_ROOT "${GCT_ROOT_DIR}" ABSOLUTE)
    get_filename_component(TESTING_TEST_DIR "${GCT_TEST_DIR}" ABSOLUTE)
    find_program(GCOVR_EXECUTABLE NAMES gcovr)
    if (NOT GCOVR_EXECUTABLE)
        message(
            WARNING
                "[testing_add_gcovr_coverage_target] gcovr not found in PATH. 'make ${GCT_NAME}' will fail unless gcovr is installed."
        )
        set(GCOVR_EXECUTABLE gcovr)
    endif ()

    # Match gcovr's --gcov-executable to the C compiler that produced .gcno/.gcda
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # Prefer the version-suffixed gcov: distros where the default gcc is newer than the default gcov (e.g. Ubuntu
        # 24.04 ships gcc 14 with gcov 13) otherwise fail every .gcno with a "version 'B42*', prefer 'B33*'" mismatch.
        string(REGEX MATCH "^[0-9]+" GCOV_MAJOR "${CMAKE_C_COMPILER_VERSION}")
        find_program(GCOV_TOOL NAMES "gcov-${GCOV_MAJOR}" gcov)
        if (NOT GCOV_TOOL)
            set(GCOV_TOOL gcov)
        endif ()
        set(GCOVR_GCOV_EXECUTABLE "${GCOV_TOOL}")
    else ()
        find_program(LLVM_COV_EXECUTABLE NAMES llvm-cov)
        if (NOT LLVM_COV_EXECUTABLE AND APPLE)
            execute_process(
                COMMAND xcrun -f llvm-cov
                OUTPUT_VARIABLE LLVM_COV_EXECUTABLE
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        endif ()
        if (NOT LLVM_COV_EXECUTABLE)
            set(LLVM_COV_EXECUTABLE llvm-cov)
        endif ()
        set(GCOVR_GCOV_EXECUTABLE "${LLVM_COV_EXECUTABLE} gcov")
    endif ()

    # Default excludes: third-party libs, tests, remote. Uses anchored directory excludes that match paths relative to
    # -r ${TESTING_ROOT}.
    set(DEFAULT_EXCLUDES "^.*/test[-_].*(/|$)" "^.*/tests_src/.*" "^.*/test_.*\\.c$" "^.*/remote/.*")
    if (GCT_EXCLUDES)
        list(APPEND DEFAULT_EXCLUDES ${GCT_EXCLUDES})
    endif ()
    message(STATUS "DEFAULT_EXCLUDES: ${DEFAULT_EXCLUDES}")
    # Exclude nested .../build dirs under components/ & examples/ only; ^.*/build matches test/build/... and would skip
    # the whole POSIX CMake output tree.
    set(DEFAULT_EXCLUDE_DIRS "^(components|examples)/.+/build($|/)" "^posix/lib($|/)" "^.*/deps$")

    # Build proper argument lists for gcovr
    set(GCOVR_EXCLUDE_ARGS)
    foreach (exclude_pattern IN LISTS DEFAULT_EXCLUDES)
        list(APPEND GCOVR_EXCLUDE_ARGS --exclude ${exclude_pattern})
    endforeach ()
    set(GCOVR_EXCLUDE_DIR_ARGS)
    foreach (exclude_dir_pattern IN LISTS DEFAULT_EXCLUDE_DIRS)
        list(APPEND GCOVR_EXCLUDE_DIR_ARGS --exclude-directories ${exclude_dir_pattern})
    endforeach ()

    # Optional --filter patterns (paths relative to -r ${TESTING_ROOT}); multiple = OR
    set(GCOVR_FILTER_ARGS)
    foreach (filter_pattern IN LISTS GCT_FILTERS)
        list(APPEND GCOVR_FILTER_ARGS --filter ${filter_pattern})
    endforeach ()

    # Clean coverage before test target
    set(GCT_TEST_TARGET ${GCT_NAME}_test)
    add_custom_target(
        ${GCT_TEST_TARGET}
        COMMAND ${CMAKE_COMMAND} -E env bash -lc "find ${CMAKE_BINARY_DIR} -name '*.gcov' -o -name '*.gcda' -delete"
        COMMAND ${CMAKE_COMMAND} -E env bash -lc "ctest --test-dir ${TESTING_TEST_DIR} -E system --output-on-failure"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Cleaning coverage files and running unit tests"
        VERBATIM
    )

    # generate coverage
    set(GCT_GEN_TARGET ${GCT_NAME}_gen)
    add_custom_target(
        ${GCT_GEN_TARGET}
        # Generate coverage
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${GCT_OUTPUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GCT_OUTPUT_DIR}
        # Positional search path = object dir only: default would scan -r +
        # object-dir and
        # pick up every .gcda under the repo (stale other-preset builds → gcov
        # version skew).
        COMMAND
            ${GCOVR_EXECUTABLE} -r ${TESTING_ROOT} --merge-mode-functions=merge-use-line-min ${GCOVR_FILTER_ARGS}
            --object-directory ${CMAKE_BINARY_DIR} --gcov-executable "${GCOVR_GCOV_EXECUTABLE}"
            ${GCOVR_EXCLUDE_DIR_ARGS} ${GCOVR_EXCLUDE_ARGS} --html-details ${GCT_OUTPUT_DIR}/index.html --xml
            ${GCT_OUTPUT_DIR}/coverage.xml --print-summary ${CMAKE_BINARY_DIR}
        # Relative --filter is resolved vs cwd at gcovr startup; keep cwd = -r so
        # patterns like ^components/esp_rmaker_neo(/|$) match source paths under the repo
        # root.
        WORKING_DIRECTORY ${TESTING_ROOT}
        COMMENT "Generating coverage report @ ${GCT_OUTPUT_DIR}/index.html"
        VERBATIM
    )
endfunction ()
