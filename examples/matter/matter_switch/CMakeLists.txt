cmake_minimum_required(VERSION 3.16)

if (NOT ESP_PLATFORM)
    message(FATAL_ERROR "The matter_switch example is ESP-IDF only (it needs the esp-matter SDK)")
endif ()

if (NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of the esp-matter repo")
endif (NOT DEFINED ENV{ESP_MATTER_PATH})

if (NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})
    if ("${IDF_TARGET}" STREQUAL "esp32" OR "${IDF_TARGET}" STREQUAL "")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32_devkit_c)
    elseif ("${IDF_TARGET}" STREQUAL "esp32c3")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32c3_devkit_m)
    elseif ("${IDF_TARGET}" STREQUAL "esp32s3")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32s3_devkit_c)
    elseif ("${IDF_TARGET}" STREQUAL "esp32c6")
        set(ENV{ESP_MATTER_DEVICE_PATH} $ENV{ESP_MATTER_PATH}/device_hal/device/esp32c6_devkit_c)
    else ()
        message(FATAL_ERROR "Unsupported IDF_TARGET")
    endif ()
endif (NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# Define the firmware version (done before project())
set(PROJECT_VER
    "1.0.0"
    CACHE STRING "Firmware version"
)

# Force minimal build (keeps unused esp-matter components out of the build graph)
set(COMPONENTS main)
# esp_matter_controller is unused here; excluding it keeps its dependency manifest (json_generator pin on older
# esp-matter checkouts) out of the version solver.
set(EXCLUDE_COMPONENTS esp_matter_controller)

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include($ENV{ESP_MATTER_DEVICE_PATH}/esp_matter_device.cmake)

set(EXTRA_COMPONENT_DIRS "${MATTER_SDK_PATH}/config/esp32/components" "${ESP_MATTER_PATH}/components"
                         "${ESP_MATTER_PATH}/device_hal/device" ${extra_components_dirs_append}
)

project(matter_switch)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
# For RISCV chips, project_include.cmake sets -Wno-format, but does not clear various flags that depend on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
# esp-matter main carries `constexpr char *TAG` (esp_matter_icd_configuration.cpp), which newer GCC (IDF >= 6.0
# toolchains) rejects under -Werror=write-strings. Downgrade until fixed upstream.
idf_build_set_property(CXX_COMPILE_OPTIONS "-Wno-error=write-strings" APPEND)
