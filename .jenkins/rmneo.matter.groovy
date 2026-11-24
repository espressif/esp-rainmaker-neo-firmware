def setup_directories() {
    sh '''
    mkdir -p ${PACKAGE_PATH}/Firmware/Evaluation
    mkdir -p ${PACKAGE_PATH}/Script
    mkdir -p ${PACKAGE_PATH}/Tools
    '''
}

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

        # dependencies.lock is committed for the Matter examples (it pins the esp-matter
        # component set), so unlike the normal job it is kept.
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
        # ninja directly, with a fixed -j: connectedhomeip compiles take ~2 GB RSS each, and the
        # default parallelism (host core count) can OOM the agent. Same cap as .gitlab/matter.yml.
        ninja -C build -j ${MATTER_BUILD_JOBS}

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

def firmware_build_save() {
    sh '''

    IFS=','; set -- ${product_list}; product_count=$#

    need_separete_firmware_folders="false"
    if [ ${product_count} -gt 1 ]; then
        need_separete_firmware_folders="true"
    fi

    IFS=','; for product in ${product_list}; do
        if [ "${need_separete_firmware_folders}" = "true" ]; then
            PACKAGE_FIRMWARE_PATH=${PACKAGE_PATH}/Firmware/${FIRMWARE_TYPE}/${product}
        else
            PACKAGE_FIRMWARE_PATH=${PACKAGE_PATH}/Firmware/${FIRMWARE_TYPE}
        fi

        PRODUCT_DIR=${ESP_RMNG_PATH}/examples/matter/${product}
        cd ${PRODUCT_DIR}

        mkdir -p ${PACKAGE_FIRMWARE_PATH}
        mkdir -p ${PACKAGE_FIRMWARE_PATH}/build
        mkdir -p ${PACKAGE_FIRMWARE_PATH}/build/bootloader
        mkdir -p ${PACKAGE_FIRMWARE_PATH}/build/partition_table

        cp build/${product}.bin ${PACKAGE_FIRMWARE_PATH}/build/${product}.bin
        cp build/bootloader/bootloader.bin ${PACKAGE_FIRMWARE_PATH}/build/bootloader/bootloader.bin
        cp build/partition_table/partition-table.bin ${PACKAGE_FIRMWARE_PATH}/build/partition_table/partition-table.bin
        if [ -f build/ota_data_initial.bin ]; then
            cp build/ota_data_initial.bin ${PACKAGE_FIRMWARE_PATH}/build/ota_data_initial.bin
        fi

        cp build/${product}.elf ${PACKAGE_FIRMWARE_PATH}/build/${product}.elf
        cp build/${product}.map ${PACKAGE_FIRMWARE_PATH}/build/${product}.map

        # Helper files
        echo -n "${product}" >> ${PACKAGE_FIRMWARE_PATH}/file_prefix_ota_0.txt
        echo -n "${product}" >> ${PACKAGE_FIRMWARE_PATH}/file_prefix_bootloader.txt

        # json
        cp build/project_description.json ${PACKAGE_FIRMWARE_PATH}/build/project_description.json
        cp build/flasher_args.json ${PACKAGE_FIRMWARE_PATH}/build/flasher_args.json

        # flash arguments
        cp build/flash_args ${PACKAGE_FIRMWARE_PATH}/build/flash_args

        # Make merged binary. The Matter fctry partition is not in flash_args: it is flashed
        # separately from the dashboard-generated bin.
        . ${IDF_PATH}/export.sh
        (cd ${PACKAGE_FIRMWARE_PATH}/build && python -m esptool --chip ${chip} merge_bin -o ${PACKAGE_FIRMWARE_PATH}/${product}-merged.bin @flash_args)
    done
    '''
}

def script_artifacts_create() {
    sh '''
        echo "Nothing in script_artifacts_create"
    '''
}

def tools_artifacts_create() {
    sh '''
        echo "Nothing in tools_artifacts_create"
    '''
}

def artifacts_save() {
    sh """
    set -e

    cd "${REPOS_PATH}"
    cp build_details.txt "${PACKAGE_NAME}/"

    mkdir -p "${WORKSPACE}/artifacts"
    cp build_details.txt "${WORKSPACE}/artifacts/"

    tar -zcvf "${WORKSPACE}/artifacts/${PACKAGE_NAME}.tar.gz" "${PACKAGE_NAME}"
    """
}

return this
