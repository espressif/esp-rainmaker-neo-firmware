# Cross-process serialization for FetchContent populate. Parallel CI/pytest jobs share SOURCE_DIRs in the repo tree
# (e.g. components/esp_rmaker_neo_ota/deps/*, components/.../libraries/*); acquire lock before the git-commit check,
# release after populate so a second process can't pass the check while another is mid-clone.

function (rmng_fetchcontent_acquire_lock source_dir out_lock_var)
    get_filename_component(_rmng_fc_parent "${source_dir}" DIRECTORY)
    get_filename_component(_rmng_fc_name "${source_dir}" NAME)
    set(_rmng_fc_lock "${_rmng_fc_parent}/.rmng-fc-${_rmng_fc_name}.lock")
    file(MAKE_DIRECTORY "${_rmng_fc_parent}")
    file(
        LOCK "${_rmng_fc_lock}"
        GUARD PROCESS
        TIMEOUT 600
        RESULT_VARIABLE _rmng_fc_err
    )
    if (_rmng_fc_err)
        message(FATAL_ERROR "rmng-sdk: FetchContent lock on ${_rmng_fc_lock} failed: ${_rmng_fc_err}")
    endif ()
    set(${out_lock_var}
        "${_rmng_fc_lock}"
        PARENT_SCOPE
    )
endfunction ()

function (rmng_fetchcontent_release_lock lock_path)
    file(LOCK "${lock_path}" RELEASE)
endfunction ()

# True if source_dir is a git working tree with a resolvable HEAD commit (rev-parse succeeds).

function (rmng_git_working_tree_current_commit source_dir result_var)
    find_package(Git QUIET)
    if (NOT GIT_EXECUTABLE)
        set(${result_var}
            ""
            PARENT_SCOPE
        )
        return()
    endif ()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --verify HEAD
        RESULT_VARIABLE _rmng_git_rev_result
        OUTPUT_VARIABLE _rmng_git_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    )
    if (_rmng_git_rev_result EQUAL 0)
        set(${result_var}
            "${_rmng_git_sha}"
            PARENT_SCOPE
        )
    else ()
        set(${result_var}
            ""
            PARENT_SCOPE
        )
    endif ()
endfunction ()
