# Kconfig registration for esp_rmaker_neo_ota component. This CMake file is for normal CMake systems to register the
# component outside of the CMake process. It allows the component's Kconfig to be included in the combined menuconfig
# before the component itself is added.

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/menuconfig.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/test_rmaker_neo_ota/kconfig_reg.cmake)

kconfig_register(
    NAME esp_rmaker_neo_ota
    KCONFIGS ${CMAKE_CURRENT_LIST_DIR}/Kconfig
    CHILDREN test_rmaker_neo_ota
)
