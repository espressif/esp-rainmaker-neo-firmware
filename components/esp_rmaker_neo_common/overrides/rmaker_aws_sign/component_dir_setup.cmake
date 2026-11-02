# Get required registry components
include(${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/esp_component_manager.cmake)
rmng_idf_components_import(ESP_COMMON_COMPONENTS_DIR DEPENDENCIES "espressif/rmaker_aws_sign:1.0.0")

set(RMAKER_AWS_SIGN_COMPONENT_DIR ${ESP_COMMON_COMPONENTS_DIR}/espressif__rmaker_aws_sign)

# Copy Kconfig file from rmaker_aws_sign component to the current directory
set(RMAKER_AWS_SIGN_KCONFIG ${CMAKE_CURRENT_LIST_DIR}/Kconfig)
file(COPY ${RMAKER_AWS_SIGN_COMPONENT_DIR}/Kconfig DESTINATION ${CMAKE_CURRENT_LIST_DIR})
