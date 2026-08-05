# Download the esp-idf-protocomm component (at most once per CMake configure; global guard)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/file_sync.cmake)
set(ESP_PROTOCOMM_VENDOR_DIR "${CMAKE_CURRENT_LIST_DIR}/vendor/esp-idf-protocomm")

# The vendored copy is gitignored and fetched on demand, pinned to an ESP-IDF release tag for reproducible builds;
# delete the vendor dir and bump the tag to refresh it.
set(ESP_PROTOCOMM_VENDOR_IDF_TAG "v6.0.2")
get_property(_protocomm_posix_vendor_downloaded GLOBAL PROPERTY _PROTOCOMM_POSIX_VENDOR_DOWNLOADED)
if (NOT _protocomm_posix_vendor_downloaded AND NOT EXISTS "${ESP_PROTOCOMM_VENDOR_DIR}/CMakeLists.txt")
    download_github_folder(
        "espressif" "esp-idf" "${ESP_PROTOCOMM_VENDOR_IDF_TAG}" "components/protocomm" "${ESP_PROTOCOMM_VENDOR_DIR}"
    )
endif ()
set_property(GLOBAL PROPERTY _PROTOCOMM_POSIX_VENDOR_DOWNLOADED TRUE)

# Construct vendor directories
set(ESP_PROTOCOMM_VENDOR_INCLUDE_DIR "${ESP_PROTOCOMM_VENDOR_DIR}/include")
set(ESP_PROTOCOMM_VENDOR_SRC_DIR "${ESP_PROTOCOMM_VENDOR_DIR}/src")
set(ESP_PROTOCOMM_VENDOR_PROTOC_DIR "${ESP_PROTOCOMM_VENDOR_DIR}/proto-c")

# Get the Kconfig file
set(ESP_PROTOCOMM_VENDOR_KCONFIG ${ESP_PROTOCOMM_VENDOR_DIR}/Kconfig)
