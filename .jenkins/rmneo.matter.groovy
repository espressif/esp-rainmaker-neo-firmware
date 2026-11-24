// Matter-only steps for Jenkinsfile.rmneo.matter_firmware: setup, discovery and the esp-matter
// build. Directory setup, packaging and artifacts are shared with the normal firmware job and
// loaded from rmng.groovy, so the two jobs cannot drift.

def setup_environment() {
    sh '''

    # Print ESP-IDF version details for debug
    echo "========================================="
    echo "ESP-Matter Docker Image Info:"
    echo "========================================="
    cd ${IDF_PATH}
    IDF_BRANCH=`git rev-parse --abbrev-ref HEAD`
    IDF_COMMIT_ID=`git rev-parse --verify HEAD`
    IDF_TAG=`git describe --tags --always`
    IDF_COMMIT_DATE=`git log -1 --format=%ci`
    echo "ESP-IDF Branch      : ${IDF_BRANCH}"
    echo "ESP-IDF Tag         : ${IDF_TAG}"
    echo "ESP-IDF Commit      : ${IDF_COMMIT_ID}"
    echo "ESP-IDF Commit Date : ${IDF_COMMIT_DATE}"

    # Print esp-matter version details for debug
    cd ${ESP_MATTER_PATH}
    MATTER_BRANCH=`git rev-parse --abbrev-ref HEAD`
    MATTER_COMMIT_ID=`git rev-parse --verify HEAD`
    MATTER_TAG=`git describe --tags --always`
    CHIP_COMMIT_ID=`git -C connectedhomeip/connectedhomeip rev-parse --verify HEAD`
    echo "esp-matter Branch   : ${MATTER_BRANCH}"
    echo "esp-matter Tag      : ${MATTER_TAG}"
    echo "esp-matter Commit   : ${MATTER_COMMIT_ID}"
    echo "connectedhomeip     : ${CHIP_COMMIT_ID}"
    echo "========================================="

    # Clone the firmware SDK (fresh clone for clean state)
    echo "Cloning esp-rainmaker-neo-firmware (branch: ${ESP_RMNG_BRANCH})..."
    rm -rf ${ESP_RMNG_PATH}
    git clone "https://${GIT_USERNAME_USR}:${GIT_USERNAME_PSW}@gitlab.espressif.cn:6688/app-frameworks/esp-rainmaker-neo-firmware.git" "${ESP_RMNG_PATH}"
    cd ${ESP_RMNG_PATH}

    # Checkout the branch
    git fetch origin ${ESP_RMNG_BRANCH}
    git checkout FETCH_HEAD
    git submodule update --init --recursive

    # Print SDK version details for debug
    RMNG_BRANCH=${ESP_RMNG_BRANCH}
    RMNG_COMMIT_ID=`git rev-parse --verify HEAD`
    RMNG_TAG=`git describe --tags --always`
    echo "SDK Branch : ${RMNG_BRANCH}"
    echo "SDK Tag    : ${RMNG_TAG}"
    echo "SDK Commit : ${RMNG_COMMIT_ID}"
    echo "========================================="

    # Write build details
    echo "esp-idf: ${IDF_BRANCH}: ${IDF_TAG}: ${IDF_COMMIT_ID}" >> ${REPOS_PATH}/build_details.txt
    echo "esp-matter: ${MATTER_BRANCH}: ${MATTER_TAG}: ${MATTER_COMMIT_ID}" >> ${REPOS_PATH}/build_details.txt
    echo "connectedhomeip: ${CHIP_COMMIT_ID}" >> ${REPOS_PATH}/build_details.txt
    echo "esp-rainmaker-neo-firmware: ${RMNG_BRANCH}: ${RMNG_TAG}: ${RMNG_COMMIT_ID}" >> ${REPOS_PATH}/build_details.txt
    printf "\n\n" >> ${REPOS_PATH}/build_details.txt

    echo "product: ${product}" >> ${REPOS_PATH}/build_details.txt
    echo "chip: ${chip}" >> ${REPOS_PATH}/build_details.txt
    echo "onboarding_mode: ${onboarding_mode}" >> ${REPOS_PATH}/build_details.txt
    echo "flash_size: 4MB" >> ${REPOS_PATH}/build_details.txt

    if [ ! -f "${CUSTOM_SDK_CONFIG_FILE}" ]; then
        echo "FATAL: custom_sdk_config file missing at ${CUSTOM_SDK_CONFIG_FILE}"
        exit 1
    fi

    if [ -s "${CUSTOM_SDK_CONFIG_FILE}" ]; then
        echo "custom_sdk_config: YES" >> ${REPOS_PATH}/build_details.txt
        echo "custom_sdk_config_content:" >> ${REPOS_PATH}/build_details.txt
        cat "${CUSTOM_SDK_CONFIG_FILE}" >> ${REPOS_PATH}/build_details.txt
    else
        echo "custom_sdk_config: NO" >> ${REPOS_PATH}/build_details.txt
    fi

    printf "\n\n" >> ${REPOS_PATH}/build_details.txt
    '''
}

def discover_products(String base = null) {
    def root = base ?: "${env.ESP_RMNG_PATH}/examples/matter"
    // A Matter example is any examples/matter/<app> with a `main/` subdir -- the same rule as
    // build_examples_matter in .gitlab/matter.yml, so a new example needs no job change.
    // common/ has no main/ and is skipped.
    def raw = sh(returnStdout: true, script: """
        cd '${root}'
        find . -mindepth 2 -maxdepth 2 -type d -name main -print \\
          | sed -e 's|/main\$||' -e 's|.*/||' \\
          | sort -u
    """).trim()
    if (!raw) {
        return []
    }
    return raw.split('\\r?\\n').collect { it.trim() }.findAll { it }
}

def discover_onboarding_modes(String base = null) {
    def root = base ?: "${env.ESP_RMNG_PATH}/examples/matter"
    // 'default' is the example's own sdkconfig.defaults (Matter-first onboarding). Every
    // common/ci-sdkconfig/sdkconfig.ci.<mode> is one more mode layered on top of it -- the
    // variants GitLab CI builds.
    def raw = sh(returnStdout: true, script: """
        cd '${root}/common/ci-sdkconfig' 2>/dev/null || exit 0
        for f in sdkconfig.ci.*; do
            [ -f "\$f" ] && echo "\${f#sdkconfig.ci.}"
        done | sort -u
    """).trim()
    def modes = raw ? raw.split('\\r?\\n').collect { it.trim() }.findAll { it } : []
    return ['default'] + modes
}

def discover_chips(String repo = null, boolean required = true) {
    def root = repo ?: env.ESP_RMNG_PATH
    // The chips GitLab CI builds: every entry of build_examples_matter's parallel matrix in
    // .gitlab/matter.yml, whatever the variable is called and whether an entry holds one chip
    // or a space-separated group. Read from there so this job never offers a chip CI does not
    // validate (e.g. esp32c6).
    def raw = sh(returnStdout: true, script: """
        sed -n '/^build_examples_matter:/,/^ *variables:/p' '${root}/.gitlab/matter.yml' \\
          | sed -n '/matrix:/,\$p' | grep -E '^ *- ' | grep -vE ':[[:space:]]*\$' \\
          | sed -e 's/#.*//' -e 's/^ *- *//' -e 's/"//g' | tr ' ' '\\n' | grep -v '^\$' || true
    """).trim()
    def chips = raw ? raw.split('\\r?\\n').collect { it.trim() }.findAll { it }.unique() : []
    if (!chips && required) {
        error("No Matter CI targets (build_examples_matter parallel matrix) found in ${root}/.gitlab/matter.yml")
    }
    // esp32c3 first: the default choice, and the target CI has validated longest.
    return chips.contains('esp32c3') ? ['esp32c3'] + (chips - 'esp32c3') : chips
}

def discover_image_tag(String repo = null) {
    def root = repo ?: env.ESP_RMNG_PATH
    // ESP_MATTER_IMAGE_TAG in .gitlab/matter.yml: the esp-matter/IDF pin CI builds with.
    def tag = sh(returnStdout: true, script: """
        grep -m1 'ESP_MATTER_IMAGE_TAG:' '${root}/.gitlab/matter.yml' | sed -e 's/.*: *//' -e 's/"//g'
    """).trim()
    if (!tag) {
        error("ESP_MATTER_IMAGE_TAG not found in ${root}/.gitlab/matter.yml")
    }
    return tag
}

def firmware_build() {
    sh '''
    printf "\n\n" >> ${REPOS_PATH}/build_details.txt
    echo "firmware_type: ${FIRMWARE_TYPE}" >> ${REPOS_PATH}/build_details.txt

    if [ ! -f "${CUSTOM_SDK_CONFIG_FILE}" ]; then
        echo "FATAL: custom_sdk_config file missing at ${CUSTOM_SDK_CONFIG_FILE}"
        exit 1
    fi

    cd ${IDF_PATH}
    . ./export.sh
    cd ${ESP_MATTER_PATH}
    . ./export.sh

    echo "product_list: ${product_list}"
    echo "onboarding_mode: ${onboarding_mode}"

    IFS=','; for product in ${product_list}; do
        echo "Building product: ${product} (${onboarding_mode})"
        PRODUCT_DIR=${ESP_RMNG_PATH}/examples/matter/${product}
        cd ${PRODUCT_DIR}
        SDKCONFIG_FILE=${PRODUCT_DIR}/sdkconfig.defaults

        # Layer the onboarding mode, then the custom config, on the product defaults. idf.py
        # appends "<file>.<target>" per entry, so later entries win over target files too.
        SDKCONFIG_DEFAULTS_ARG="${SDKCONFIG_FILE}"
        if [ "${onboarding_mode}" != "default" ]; then
            SDKCONFIG_DEFAULTS_ARG="${SDKCONFIG_DEFAULTS_ARG};${ESP_RMNG_PATH}/examples/matter/common/ci-sdkconfig/sdkconfig.ci.${onboarding_mode}"
        fi
        if [ -s "${CUSTOM_SDK_CONFIG_FILE}" ]; then
            SDKCONFIG_DEFAULTS_ARG="${SDKCONFIG_DEFAULTS_ARG};${CUSTOM_SDK_CONFIG_FILE}"
        fi

        # dependencies.lock is kept (unlike the normal job) so the OTA build uses the same
        # component versions as this run's Evaluation build.
        rm -rf build sdkconfig sdkconfig.old managed_components

        MAX_PROJECT_VER=1
        MIN_PROJECT_VER=0

        if [ "${FIRMWARE_TYPE}" = "OTA" ]; then
            MIN_PROJECT_VER=$((MIN_PROJECT_VER + 1 + (RANDOM % 10)))
            PROJECT_VERSION="${MAX_PROJECT_VER}.${MIN_PROJECT_VER}"
            PROJECT_VERSION_STR="${MAX_PROJECT_VER}.${MIN_PROJECT_VER}"
            echo "OTA Project Version Number: ${PROJECT_VERSION}" >> ${REPOS_PATH}/build_details.txt
            echo "OTA Project Version String: ${PROJECT_VERSION_STR}" >> ${REPOS_PATH}/build_details.txt
        fi

        PROJECT_VERSION_STR="${MAX_PROJECT_VER}.${MIN_PROJECT_VER}"

        # Override the PROJECT_VER cache entry from the command line instead of rewriting
        # CMakeLists.txt: a -D always wins over set(... CACHE ...).
        idf.py -DPROJECT_VER="${PROJECT_VERSION_STR}" "-DSDKCONFIG_DEFAULTS=${SDKCONFIG_DEFAULTS_ARG}" set-target ${chip}
        # Pin the build to MATTER_BUILD_JOBS cores, as .gitlab/matter.yml does: connectedhomeip
        # compiles take ~2 GB RSS each and its inner ninja (an ExternalProject with no -j) sizes
        # itself from the CPU affinity, so -j alone does not stop it from OOMing the agent.
        taskset -c 0-$((MATTER_BUILD_JOBS - 1)) ninja -C build -j ${MATTER_BUILD_JOBS}

        # The override is silent if it ever stops taking effect, so assert on what was built.
        BUILT_PROJECT_VER=$(python -c 'import json; print(json.load(open("build/project_description.json"))["project_version"])')
        if [ "${BUILT_PROJECT_VER}" != "${PROJECT_VERSION_STR}" ]; then
            echo "FATAL: ${product} built with project_ver '${BUILT_PROJECT_VER}', expected '${PROJECT_VERSION_STR}'"
            exit 1
        fi
        echo "${product} project_ver: ${BUILT_PROJECT_VER}" >> ${REPOS_PATH}/build_details.txt
    done
    '''
}

return this
