set(TEST_APP_SOURCES test_app.c)
set(TEST_APP_PRIV_INCLUDE_DIRS ".")
set(TEST_APP_PRIV_REQUIRES
    # Required ESP components
    "unity"
    "nvs_flash"
    "esp_event"
    "app_network"
    # osal test subcomponents
    "test_osal_platform"
    "test_osal_storage"
    "test_osal_json"
    "test_osal_timesync"
    "test_osal_mqtt"
    "test_osal_http"
    # esp_rmaker_neo_common test
    "test_rmaker_neo_common"
    # esp_rmaker_neo test
    "test_rmaker_neo"
    # esp_rmaker_neo_ota test
    "test_rmaker_neo_ota"
)

idf_component_register(
    SRCS ${TEST_APP_SOURCES}
    PRIV_INCLUDE_DIRS ${TEST_APP_PRIV_INCLUDE_DIRS}
    PRIV_REQUIRES ${TEST_APP_PRIV_REQUIRES}
)
