# file_sync.cmake -- functions to download file contents from GitHub.

# Resolves REF (branch, tag, or full 40-char SHA) to a commit SHA. Branch/tag tips need a remote lookup so cache keys
# track the actual tree, not just the ref name. Uses `git ls-remote` (no rate limit); falls back to the GitHub API if
# git is unavailable. Set GITHUB_TOKEN in the environment for higher API limits on that fallback path.
function (_github_resolve_ref_to_sha OWNER REPO REF RESOLVED_SHA_OUT)
    string(LENGTH "${REF}" _REF_LEN)
    string(REGEX MATCH "^[0-9a-fA-F][0-9a-fA-F]*$" _REF_ALL_HEX "${REF}")
    if (_REF_LEN EQUAL 40 AND NOT _REF_ALL_HEX STREQUAL "")
        string(TOLOWER "${REF}" RESOLVED_LOWER)
        set(${RESOLVED_SHA_OUT}
            "${RESOLVED_LOWER}"
            PARENT_SCOPE
        )
        return()
    endif ()

    find_program(GIT_EXECUTABLE NAMES git)
    if (GIT_EXECUTABLE)
        execute_process(
            # Trailing "*" on the tag pattern so git also emits the peeled "refs/tags/<ref>^{}" line for annotated tags;
            # an exact pattern suppresses it. Extra glob hits are ignored when matching below.
            COMMAND "${GIT_EXECUTABLE}" ls-remote "https://github.com/${OWNER}/${REPO}.git" "refs/heads/${REF}"
                    "refs/tags/${REF}*"
            OUTPUT_VARIABLE LS_REMOTE_OUT
            ERROR_VARIABLE LS_REMOTE_ERR
            RESULT_VARIABLE LS_REMOTE_RESULT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if (LS_REMOTE_RESULT EQUAL 0)
            # Lines are "<sha>\t<refname>". Prefer a branch tip, then an annotated tag's peeled ("^{}") commit, then a
            # lightweight tag.
            string(REGEX REPLACE "([.+*?^$()\\[\\]|\\\\])" "\\\\\\1" _REF_RE "${REF}")
            set(_LS_SHA "")
            foreach (_CAND "refs/heads/${_REF_RE}" "refs/tags/${_REF_RE}\\^{}" "refs/tags/${_REF_RE}")
                if (_LS_SHA STREQUAL "")
                    string(REGEX MATCH "([0-9a-fA-F]+)[ \t]+${_CAND}\n" _LS_MATCH "${LS_REMOTE_OUT}\n")
                    if (NOT _LS_MATCH STREQUAL "")
                        string(TOLOWER "${CMAKE_MATCH_1}" _CAND_SHA)
                        string(LENGTH "${_CAND_SHA}" _CAND_LEN)
                        if (_CAND_LEN EQUAL 40)
                            set(_LS_SHA "${_CAND_SHA}")
                        endif ()
                    endif ()
                endif ()
            endforeach ()
            if (NOT _LS_SHA STREQUAL "")
                set(${RESOLVED_SHA_OUT}
                    "${_LS_SHA}"
                    PARENT_SCOPE
                )
                return()
            endif ()
            # git worked but the ref does not exist; the API would not help.
            message(FATAL_ERROR "git ls-remote found no refs/heads/${REF} or refs/tags/${REF} in ${OWNER}/${REPO}")
        endif ()
        message(
            STATUS
                "git ls-remote failed for ${OWNER}/${REPO}@${REF} (exit ${LS_REMOTE_RESULT}: ${LS_REMOTE_ERR}); falling back to GitHub API"
        )
    endif ()

    string(REPLACE "/" "%2F" API_REF "${REF}")
    set(API_URL "https://api.github.com/repos/${OWNER}/${REPO}/commits/${API_REF}")
    string(MD5 API_TMP_HASH "${OWNER}/${REPO}/${REF}")
    set(API_TMP "${CMAKE_BINARY_DIR}/.github_commits_${API_TMP_HASH}.json")

    if (DEFINED ENV{GITHUB_TOKEN} AND NOT "$ENV{GITHUB_TOKEN}" STREQUAL "")
        file(
            DOWNLOAD "${API_URL}" "${API_TMP}"
            HTTPHEADER "Accept: application/vnd.github+json"
            HTTPHEADER "Authorization: Bearer $ENV{GITHUB_TOKEN}"
            STATUS API_STATUS
        )
    else ()
        file(
            DOWNLOAD "${API_URL}" "${API_TMP}"
            HTTPHEADER "Accept: application/vnd.github+json"
            STATUS API_STATUS
        )
    endif ()

    list(GET API_STATUS 0 API_CODE)
    list(GET API_STATUS 1 API_MESSAGE)
    if (NOT API_CODE EQUAL 0)
        message(FATAL_ERROR "GitHub API failed for ${OWNER}/${REPO}@${REF} (${API_URL}): ${API_MESSAGE}")
    endif ()

    file(READ "${API_TMP}" API_BODY)
    # CMake regex does not support {40}; use one-or-more hex then enforce length.
    string(REGEX MATCH "\"sha\"[ \t]*:[ \t]*\"([0-9a-fA-F][0-9a-fA-F]*)\"" _SHA_MATCH "${API_BODY}")
    if (_SHA_MATCH STREQUAL "")
        message(FATAL_ERROR "Could not parse commit sha from GitHub API response for ${OWNER}/${REPO}@${REF}")
    endif ()
    set(_PARSED_SHA "${CMAKE_MATCH_1}")
    string(LENGTH "${_PARSED_SHA}" _PARSED_LEN)
    if (NOT _PARSED_LEN EQUAL 40)
        message(
            FATAL_ERROR
                "Could not parse commit sha from GitHub API response for ${OWNER}/${REPO}@${REF} (expected 40 hex chars, got ${_PARSED_LEN})"
        )
    endif ()
    set(${RESOLVED_SHA_OUT}
        "${_PARSED_SHA}"
        PARENT_SCOPE
    )
endfunction ()

# Downloads a subdirectory of a repository at COMMIT into TARGET_DIR (contents of the folder, not the folder name
# itself). SOURCE_FOLDER_PATH is the path inside the repo (e.g. components/foo), no leading slash. Uses the repo
# zipball; requires CMake's built-in tar (cmake -E tar), available with CMake 3.16+.
function (download_github_folder OWNER REPO COMMIT SOURCE_FOLDER_PATH TARGET_DIR)
    string(REGEX REPLACE "^/+|/+$" "" SOURCE_FOLDER_PATH "${SOURCE_FOLDER_PATH}")
    if (SOURCE_FOLDER_PATH STREQUAL "")
        message(FATAL_ERROR "SOURCE_FOLDER_PATH must not be empty")
    endif ()

    _github_resolve_ref_to_sha("${OWNER}" "${REPO}" "${COMMIT}" RESOLVED_SHA)
    string(MD5 ARCHIVE_HASH "${OWNER}/${REPO}/${RESOLVED_SHA}")
    set(ARCHIVE_FILE "${CMAKE_BINARY_DIR}/.github_folder_${ARCHIVE_HASH}.zip")
    set(EXTRACT_DIR "${CMAKE_BINARY_DIR}/.github_folder_extract_${ARCHIVE_HASH}")
    set(ARCHIVE_URL "https://github.com/${OWNER}/${REPO}/archive/${COMMIT}.zip")

    # Cache key uses resolved SHA so moving branch tips get a new zip path; full SHAs skip the API (see
    # _github_resolve_ref_to_sha).
    string(TOLOWER "${COMMIT}" COMMIT_LOWER)
    if (NOT ("${COMMIT_LOWER}" STREQUAL "${RESOLVED_SHA}"))
        message(STATUS "${OWNER}/${REPO}@${COMMIT} -> ${RESOLVED_SHA} (cache key)")
    endif ()
    if (EXISTS "${ARCHIVE_FILE}")
        message(
            STATUS "Reusing cached archive ${ARCHIVE_FILE} for ${OWNER}/${REPO}@${RESOLVED_SHA} (${SOURCE_FOLDER_PATH})"
        )
    else ()
        message(STATUS "Downloading ${OWNER}/${REPO}@${COMMIT} archive (GitHub folder: ${SOURCE_FOLDER_PATH})...")
        file(
            DOWNLOAD "${ARCHIVE_URL}" "${ARCHIVE_FILE}"
            STATUS DOWNLOAD_STATUS
            SHOW_PROGRESS
        )
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        list(GET DOWNLOAD_STATUS 1 STATUS_MESSAGE)
        if (NOT STATUS_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download archive from ${ARCHIVE_URL}: ${STATUS_MESSAGE}")
        endif ()
    endif ()

    set(SKIP_EXTRACT FALSE)
    if (EXISTS "${EXTRACT_DIR}")
        file(GLOB ROOT_CANDIDATES "${EXTRACT_DIR}/*")
        list(LENGTH ROOT_CANDIDATES ROOT_COUNT)
        if (ROOT_COUNT EQUAL 1)
            list(GET ROOT_CANDIDATES 0 INNER_ROOT)
            set(_CACHED_SOURCE "${INNER_ROOT}/${SOURCE_FOLDER_PATH}")
            if (IS_DIRECTORY "${_CACHED_SOURCE}")
                set(SKIP_EXTRACT TRUE)
            endif ()
        endif ()
    endif ()

    if (NOT SKIP_EXTRACT)
        if (EXISTS "${EXTRACT_DIR}")
            file(REMOVE_RECURSE "${EXTRACT_DIR}")
        endif ()
        file(MAKE_DIRECTORY "${EXTRACT_DIR}")

        message(STATUS "Extracting archive to ${EXTRACT_DIR}...")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E tar xf "${ARCHIVE_FILE}"
            WORKING_DIRECTORY "${EXTRACT_DIR}"
            RESULT_VARIABLE TAR_RESULT
        )
        if (NOT TAR_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract ${ARCHIVE_FILE} (cmake -E tar exit ${TAR_RESULT})")
        endif ()

        file(GLOB ROOT_CANDIDATES "${EXTRACT_DIR}/*")
        list(LENGTH ROOT_CANDIDATES ROOT_COUNT)
        if (NOT ROOT_COUNT EQUAL 1)
            message(FATAL_ERROR "Expected a single top-level directory in GitHub archive, found ${ROOT_COUNT}")
        endif ()
        list(GET ROOT_CANDIDATES 0 INNER_ROOT)
    endif ()

    set(SOURCE_FOLDER "${INNER_ROOT}/${SOURCE_FOLDER_PATH}")
    if (NOT IS_DIRECTORY "${SOURCE_FOLDER}")
        message(FATAL_ERROR "Folder not in archive: ${SOURCE_FOLDER_PATH} (resolved ${SOURCE_FOLDER})")
    endif ()

    file(MAKE_DIRECTORY "${TARGET_DIR}")
    message(STATUS "Copying ${SOURCE_FOLDER_PATH} into ${TARGET_DIR}...")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${SOURCE_FOLDER}" "${TARGET_DIR}" RESULT_VARIABLE COPY_RESULT
    )
    if (NOT COPY_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to copy ${SOURCE_FOLDER} to ${TARGET_DIR}")
    endif ()

    message(STATUS "Synced ${OWNER}/${REPO}@${COMMIT}:${SOURCE_FOLDER_PATH} -> ${TARGET_DIR}")
endfunction ()

function (download_github_file OWNER REPO COMMIT PATHS OUTPUT_FILES)
    # Ensure PATHS and OUTPUT_FILES have the same length
    list(LENGTH PATHS PATHS_COUNT)
    list(LENGTH OUTPUT_FILES OUTPUT_FILES_COUNT)

    if (NOT PATHS_COUNT EQUAL OUTPUT_FILES_COUNT)
        message(FATAL_ERROR "PATHS and OUTPUT_FILES must have the same number of elements")
    endif ()

    # Download each file
    math(EXPR LAST_INDEX "${PATHS_COUNT} - 1")
    foreach (INDEX RANGE 0 ${LAST_INDEX})
        list(GET PATHS ${INDEX} CURRENT_PATH)
        list(GET OUTPUT_FILES ${INDEX} CURRENT_OUTPUT)

        # Construct the raw GitHub URL (using jsdelivr CDN to avoid rate limiting)
        set(GITHUB_URL "https://cdn.jsdelivr.net/gh/${OWNER}/${REPO}@${COMMIT}/${CURRENT_PATH}")

        # Download the file content
        file(
            DOWNLOAD "${GITHUB_URL}" "${CURRENT_OUTPUT}"
            STATUS DOWNLOAD_STATUS
            SHOW_PROGRESS
        )

        # Check if download was successful
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        list(GET DOWNLOAD_STATUS 1 STATUS_MESSAGE)

        if (NOT STATUS_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to download file from ${GITHUB_URL}: ${STATUS_MESSAGE}")
        endif ()

        message(STATUS "Successfully downloaded ${CURRENT_PATH} from ${OWNER}/${REPO}@${COMMIT} to ${CURRENT_OUTPUT}")
    endforeach ()
endfunction ()
