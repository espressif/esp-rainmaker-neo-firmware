# Kconfig registration for esp_rmaker_neo_common component. This CMake file is for normal CMake systems to register the
# component outside of the CMake process. It allows the component's Kconfig to be included in the combined menuconfig
# before the component itself is added.

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/menuconfig.cmake)

# component_dir_setup.cmake fetches rmaker_aws_sign and copies its Kconfig into the override dir, publishing
# RMAKER_AWS_SIGN_KCONFIG.
include(${CMAKE_CURRENT_LIST_DIR}/overrides/rmaker_aws_sign/component_dir_setup.cmake)
kconfig_register(NAME rmaker_aws_sign KCONFIGS ${RMAKER_AWS_SIGN_KCONFIG})

kconfig_register(
    NAME esp_rmaker_neo_common
    KCONFIGS ${CMAKE_CURRENT_LIST_DIR}/Kconfig.projbuild
    CHILDREN rmaker_aws_sign
)
