cmake_minimum_required(VERSION 3.16)

project(posix_rmng_sdk_test_app C ASM)

enable_testing()
set(TEST_RMNG ON)
set(TEST_RMNG_OTA ON)
set(CMAKE_BUILD_TYPE Debug)

# Opt-in AddressSanitizer for the POSIX unit-test build. Catches use-after-free, buffer overflows, etc. across all
# instrumented TUs. Enable with -DRMNG_TEST_ASAN=ON (CI turns this on). Directory-scoped flags apply to every
# subdirectory added below (esp_rmaker_neo, esp_rmaker_neo_ota, mqtt tests).
option(RMNG_TEST_ASAN "Build POSIX unit tests with AddressSanitizer" OFF)
if (RMNG_TEST_ASAN)
    message(STATUS "RMNG_TEST_ASAN=ON: building POSIX unit tests with AddressSanitizer")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif ()

# Downgrade -Wdeprecated-declarations from error to warning. Independent of ASan: vendored mbedtls sample programs build
# with -Werror and use sprintf(), which the macOS SDK marks deprecated — a clean (or ccache-busted) build then fails on
# macOS. Off by default; enable for full local macOS builds. CI (Linux) doesn't need it.
option(RMNG_TEST_ALLOW_DEPRECATED "Don't treat -Wdeprecated-declarations as an error" OFF)
if (RMNG_TEST_ALLOW_DEPRECATED)
    message(STATUS "RMNG_TEST_ALLOW_DEPRECATED=ON: -Wdeprecated-declarations downgraded to warning")
    add_compile_options(-Wno-error=deprecated-declarations)
endif ()

# Config for esp_rmaker_neo (Kconfig/menuconfig); esp_rmaker_neo submodule must be added first.
list(PREPEND SDKCONFIG_DEFAULTS ${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults)

set(COMPONENTS_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../components)
# Add esp_rmaker_neo first so it pulls in esp_rmaker_neo_common and osal.
add_subdirectory(${COMPONENTS_DIR}/esp_rmaker_neo components/esp_rmaker_neo)
add_subdirectory(${COMPONENTS_DIR}/esp_rmaker_neo_ota components/esp_rmaker_neo_ota)

# Optional test-mqtt-common.
add_subdirectory(${COMPONENTS_DIR}/osal/mqtt/test-mqtt-common components/osal/mqtt/test-mqtt-common)
