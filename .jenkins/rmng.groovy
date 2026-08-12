def setup_directories() {
    sh '''
    mkdir -p ${PACKAGE_PATH}/Firmware/Evaluation
    mkdir -p ${PACKAGE_PATH}/Script
    mkdir -p ${PACKAGE_PATH}/Tools
    '''
}

def setup_environment() {
    sh '''

    # Update ESP-IDF to latest (disabled for now)
    # echo "Updating ESP-IDF..."
    # cd ${IDF_PATH}
    # git fetch --all --tags
    # git pull
    # git submodule update --init --recursive
    # ./install.sh
    # . ./export.sh

    # Print ESP-IDF version details for debug
    echo "========================================="
    echo "ESP-IDF Docker Image Info:"
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
    echo "esp-rainmaker-neo-firmware: ${RMNG_BRANCH}: ${RMNG_TAG}: ${RMNG_COMMIT_ID}" >> ${REPOS_PATH}/build_details.txt
    printf "\n\n" >> ${REPOS_PATH}/build_details.txt

    echo "product: ${product}" >> ${REPOS_PATH}/build_details.txt
    echo "chip: ${chip}" >> ${REPOS_PATH}/build_details.txt
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
    def root = base ?: "${env.ESP_RMNG_PATH}/examples"
    // An example is any directory with a `main/` subdir — same convention as the GitLab
    // pipelines and the Launchpad workflow. maxdepth 3 also finds examples grouped one
    // level down (examples/advanced/<app>); the product is always the bare app name,
    // since it is used as the .bin basename and the package folder name.
    def raw = sh(returnStdout: true, script: """
        cd '${root}'
        find . -mindepth 2 -maxdepth 3 -type d -name main -prune -print \\
          | sed -e 's|/main\$||' -e 's|.*/||' \\
          | sort -u
    """).trim()
    if (!raw) {
        return []
    }
    return raw.split('\\r?\\n').collect { it.trim() }.findAll { it }
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

    echo "product_list: ${product_list}"

    IFS=','; for product in ${product_list}; do
        echo "Building product: ${product}"
        # The device-sim / ota-sim choices are test firmwares and live under
        # test/sims/, not examples/, so resolve every location.
        PRODUCT_DIR=${ESP_RMNG_PATH}/examples/${product}
        # Examples may be grouped one level down (examples/advanced/<app>).
        [ -d "${PRODUCT_DIR}" ] || PRODUCT_DIR=$(find ${ESP_RMNG_PATH}/examples -mindepth 2 -maxdepth 2 -type d -name "${product}" | head -n1)
        [ -d "${PRODUCT_DIR}" ] || PRODUCT_DIR=${ESP_RMNG_PATH}/test/sims/${product}
        cd ${PRODUCT_DIR}
        SDKCONFIG_FILE=${PRODUCT_DIR}/sdkconfig.defaults

        # Chain the custom config after the product defaults instead of rewriting
        # sdkconfig.defaults: idf.py appends "<file>.<target>" per entry, so listing it
        # last keeps it winning over sdkconfig.defaults.<target> too. OTA is included
        # because it used to inherit the mutated file.
        SDKCONFIG_DEFAULTS_ARG=""
        if [ -s "${CUSTOM_SDK_CONFIG_FILE}" ]; then
            SDKCONFIG_DEFAULTS_ARG="-DSDKCONFIG_DEFAULTS=${SDKCONFIG_FILE};${CUSTOM_SDK_CONFIG_FILE}"
        fi

        rm -rf build sdkconfig sdkconfig.old managed_components dependencies.lock

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
        # CMakeLists.txt: a -D always wins over set(... CACHE ...), so this is immune to
        # formatting changes in the example's CMakeLists.txt.
        idf.py -DPROJECT_VER="${PROJECT_VERSION_STR}" ${SDKCONFIG_DEFAULTS_ARG} set-target ${chip} build

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

        PRODUCT_DIR=${ESP_RMNG_PATH}/examples/${product}
        # Examples may be grouped one level down (examples/advanced/<app>).
        [ -d "${PRODUCT_DIR}" ] || PRODUCT_DIR=$(find ${ESP_RMNG_PATH}/examples -mindepth 2 -maxdepth 2 -type d -name "${product}" | head -n1)
        [ -d "${PRODUCT_DIR}" ] || PRODUCT_DIR=${ESP_RMNG_PATH}/test/sims/${product}
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

        # Make merged binary
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
