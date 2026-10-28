# Kconfig registration for test_osal_http component. This CMake file is for normal CMake systems to register the
# component outside of the CMake process. It allows the component's Kconfig to be included in the combined menuconfig
# before the component itself is added.

include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/menuconfig.cmake)
set(TEST_OSAL_HTTP_KCONFIGS ${CMAKE_CURRENT_LIST_DIR}/Kconfig)
kconfig_register(NAME test_osal_http KCONFIGS ${TEST_OSAL_HTTP_KCONFIGS})
