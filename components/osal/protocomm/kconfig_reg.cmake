# Kconfig registration for protocomm-posix component. This CMake file is for normal CMake systems to register the
# component outside of the CMake process. It allows the component's Kconfig to be included in the combined menuconfig
# before the component itself is added.

# Get the Kconfig file
include(${CMAKE_CURRENT_LIST_DIR}/component_dir_setup.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/menuconfig.cmake)
set(PROTOCOMM_POSIX_KCONFIGS ${ESP_PROTOCOMM_VENDOR_KCONFIG})
kconfig_register(NAME protocomm-posix KCONFIGS ${PROTOCOMM_POSIX_KCONFIGS})
