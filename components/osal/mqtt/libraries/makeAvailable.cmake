# Locates backoffAlgorithm, coreMQTT-Agent, coreMQTT, and the ESP port layer inside the esp-aws-iot checkout. Included
# from the osal/mqtt backoff / coreMQTT-Agent wrappers.
#
# ESP target uses esp-aws-iot's own ports (network_transport + freertos agent glue). POSIX target uses rmng-local ports
# (port/posix + port/common). See coreMQTT-Agent/CMakeLists.txt.

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/esp_aws_iot.cmake")

# Library source trees (submodules of esp-aws-iot).
set(RMNG_EAI_BACKOFF_SRC "${RMNG_ESP_AWS_IOT_LIBS_DIR}/backoffAlgorithm/backoffAlgorithm")
set(RMNG_EAI_AGENT_SRC "${RMNG_ESP_AWS_IOT_LIBS_DIR}/coreMQTT-Agent/coreMQTT-Agent")
set(RMNG_EAI_COREMQTT_SRC "${RMNG_ESP_AWS_IOT_LIBS_DIR}/coreMQTT/coreMQTT")

# esp-aws-iot ESP port layer (used on ESP_PLATFORM only). Only the coreMQTT transport port is taken from esp-aws-iot;
# the agent glue stays esp_rmaker_neo's own, so esp-aws-iot's coreMQTT-Agent/port is deliberately not wired up here.
set(RMNG_EAI_COREMQTT_PORT_DIR "${RMNG_ESP_AWS_IOT_LIBS_DIR}/coreMQTT/port/network_transport")
