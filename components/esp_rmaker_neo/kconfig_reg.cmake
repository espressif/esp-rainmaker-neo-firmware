# Kconfig registration for esp_rmaker_neo component. This CMake file is for normal CMake systems to register the
# component outside of the CMake process. It allows the component's Kconfig to be included in the combined menuconfig
# before the component itself is added.

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/menuconfig.cmake)

# Include children's Kconfig registrations first

# NOTE: esp_rmaker_neo_ota is optional, so registration will fail silently if it is not present
include(${CMAKE_CURRENT_LIST_DIR}/../esp_rmaker_neo_common/kconfig_reg.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../osal/kconfig_reg.cmake)
# esp_schedule has no registration file of its own: component_dir_setup.cmake is what fetches the component and copies
# its Kconfig into the override dir, and it publishes ESP_SCHEDULE_KCONFIG pointing there. Do not substitute
# ${CMAKE_CURRENT_LIST_DIR}/Kconfig — that resolves to this component's own Kconfig.
include(${CMAKE_CURRENT_LIST_DIR}/overrides/esp_schedule/component_dir_setup.cmake)
kconfig_register(NAME esp_schedule KCONFIGS ${ESP_SCHEDULE_KCONFIG})
include(${CMAKE_CURRENT_LIST_DIR}/../esp_rmaker_neo_ota/kconfig_reg.cmake)

set(_children esp_rmaker_neo_common osal protocomm-posix esp_schedule esp_rmaker_neo_ota)
kconfig_register(
    NAME esp_rmaker_neo
    KCONFIGS ${CMAKE_CURRENT_LIST_DIR}/Kconfig
    CHILDREN ${_children}
)
