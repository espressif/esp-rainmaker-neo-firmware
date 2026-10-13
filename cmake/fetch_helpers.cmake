# Serializes FetchContent populate across processes that share a SOURCE_DIR (e.g. third_party/mbedtls,
# components/osal/*/libraries/*); hold the lock from the ready check until populate ends.

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

# True if source_dir holds a finished archive fetch for sha256.
function (rmng_fetchcontent_source_ready source_dir sha256 result_var)
    set(_rmng_fc_ready FALSE)
    if (EXISTS "${source_dir}/.rmng-fc-stamp")
        file(READ "${source_dir}/.rmng-fc-stamp" _rmng_fc_stamp)
        if (_rmng_fc_stamp STREQUAL sha256)
            set(_rmng_fc_ready TRUE)
        endif ()
    endif ()
    set(${result_var}
        ${_rmng_fc_ready}
        PARENT_SCOPE
    )
endfunction ()

# Stops the configure if source_dir is a git checkout but not a clean clone of commit; ignores changes to ARGN paths.
function (rmng_fetchcontent_guard_checkout source_dir commit)
    if (NOT EXISTS "${source_dir}/.git")
        return()
    endif ()
    set(_rmng_fc_head "")
    set(_rmng_fc_changes "unknown")
    find_package(Git QUIET)
    if (GIT_EXECUTABLE)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --verify HEAD
            OUTPUT_VARIABLE _rmng_fc_head
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        )
        set(_rmng_fc_excludes "")
        foreach (_rmng_fc_path IN LISTS ARGN)
            list(APPEND _rmng_fc_excludes ":(exclude)${_rmng_fc_path}")
        endforeach ()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" status --porcelain -- . ${_rmng_fc_excludes}
            RESULT_VARIABLE _rmng_fc_status_result
            OUTPUT_VARIABLE _rmng_fc_changes
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        )
        if (NOT _rmng_fc_status_result EQUAL 0)
            set(_rmng_fc_changes "unknown")
        endif ()
    endif ()
    if (NOT _rmng_fc_head STREQUAL commit OR NOT _rmng_fc_changes STREQUAL "")
        message(FATAL_ERROR "rmng-sdk: '${source_dir}' is a git checkout that is not a clean clone of ${commit}. "
                            "The fetch replaces this directory, so move or delete it, then configure again."
        )
    endif ()
endfunction ()

# Clears the FetchContent populate state of name, so the next populate downloads and extracts again.
function (rmng_fetchcontent_reset name)
    string(TOLOWER "${name}" _rmng_fc_name)
    file(REMOVE_RECURSE "${FETCHCONTENT_BASE_DIR}/${_rmng_fc_name}-subbuild")
endfunction ()

# Marks source_dir as a finished fetch of the archive with sha256.
function (rmng_fetchcontent_stamp_write source_dir sha256)
    file(WRITE "${source_dir}/.rmng-fc-stamp" "${sha256}")
endfunction ()
