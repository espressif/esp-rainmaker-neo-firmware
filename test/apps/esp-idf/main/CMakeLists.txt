set(RMNG_TEST_APP_SOURCES test_app.c)
set(RMNG_TEST_APP_PRIV_INCLUDE_DIRS ".")
set(RMNG_TEST_APP_PRIV_REQUIRES
    # Required ESP components
    "unity"
    "nvs_flash"
    "esp_event"
    "app_network"
    # osal test subcomponents
    "test-platform-common"
    "test-nvs-common"
    "test-json-common"
    "test-timesync-common"
    "test-mqtt-common"
    "test-http-common"
    # esp_rmaker_neo_common test
    "test_rmaker_neo_common"
    # esp_rmaker_neo test
    "test_rmaker_neo"
    # esp_rmaker_neo_ota test
    "test_rmaker_neo_ota"
)

idf_component_register(
    SRCS ${RMNG_TEST_APP_SOURCES}
    PRIV_INCLUDE_DIRS ${RMNG_TEST_APP_PRIV_INCLUDE_DIRS}
    PRIV_REQUIRES ${RMNG_TEST_APP_PRIV_REQUIRES}
)
