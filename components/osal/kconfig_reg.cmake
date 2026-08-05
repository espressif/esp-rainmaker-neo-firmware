# Kconfig registration for the osal component. This CMake file is for normal CMake systems to register the component
# outside of the CMake process. It allows the component's Kconfig to be included in the combined menuconfig before the
# component itself is added.

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/menuconfig.cmake)

# Test Kconfigs (registered as children)
include(${CMAKE_CURRENT_LIST_DIR}/http/test-http-common/kconfig_reg.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/mqtt/test-mqtt-common/kconfig_reg.cmake)

# protocomm registers itself (as protocomm-posix, parented directly by esp_rmaker_neo)
include(${CMAKE_CURRENT_LIST_DIR}/protocomm/kconfig_reg.cmake)
# osal/Kconfig pulls in mqtt/Kconfig.mqtt and ext-io/Kconfig.ext_io itself (via Kconfig source directives); the vendored
# coreMQTT-Agent Kconfig is a separate file (auto-discovered as a component on ESP-IDF, listed explicitly here for POSIX
# menuconfig).
set(OSAL_KCONFIGS
    ${CMAKE_CURRENT_LIST_DIR}/Kconfig ${CMAKE_CURRENT_LIST_DIR}/platform/Kconfig.posix
    ${CMAKE_CURRENT_LIST_DIR}/ca-bundle/Kconfig ${CMAKE_CURRENT_LIST_DIR}/mqtt/libraries/coreMQTT-Agent/Kconfig
)
kconfig_register(
    NAME osal
    KCONFIGS ${OSAL_KCONFIGS}
    CHILDREN test-http-common test-mqtt-common
)
