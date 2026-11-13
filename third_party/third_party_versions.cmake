include_guard(GLOBAL)

# Pinned archives for FetchContent under third_party/ (Unity, mbedtls, protobuf-c, esp-aws-iot). *_COMMIT is the commit
# of the archive; a clean git clone at that commit is replaced by the archive.

# Unity: v2.6.1
set(RMNG_THIRD_PARTY_UNITY_URL "https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.6.1.tar.gz")
set(RMNG_THIRD_PARTY_UNITY_SHA256 "b41a66d45a6b99758fb3202ace6178177014d52fc524bf1f72687d93e9867292")
set(RMNG_THIRD_PARTY_UNITY_COMMIT "cbcd08fa7de711053a3deec6339ee89cad5d2697")

# Mbed TLS: v4.1.0 (the release tarball includes the framework and tf-psa-crypto submodules)
set(RMNG_THIRD_PARTY_MBEDTLS_URL
    "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-4.1.0/mbedtls-4.1.0.tar.bz2"
)
set(RMNG_THIRD_PARTY_MBEDTLS_SHA256 "377a09cf8eb81b5fb2707045e5522d5489d3309fed5006c9874e60558fc81d10")
set(RMNG_THIRD_PARTY_MBEDTLS_COMMIT "0fe989b6b514192783c469039edd325fd0989806")

# protobuf-c: v1.5.2
set(RMNG_THIRD_PARTY_PROTOBUF_C_URL
    "https://github.com/protobuf-c/protobuf-c/releases/download/v1.5.2/protobuf-c-1.5.2.tar.gz"
)
set(RMNG_THIRD_PARTY_PROTOBUF_C_SHA256 "e2c86271873a79c92b58fef7ebf8de1aa0df4738347a8bd5d4e65a80a16d0d24")
set(RMNG_THIRD_PARTY_PROTOBUF_C_COMMIT "4719fdd7760624388c2c5b9d6759eb6a47490626")

# esp-aws-iot (untagged master commit) and the six library submodules that rmng-sdk builds. Do not edit by hand: run
# `esp_aws_iot_pins.py --update <ref>` to re-pin, and `esp_aws_iot_pins.py --check` to verify.
set(RMNG_THIRD_PARTY_ESP_AWS_IOT_URL
    "https://github.com/espressif/esp-aws-iot/archive/9c879feedb699611e7c220b93aed84c84322ff84.tar.gz"
)
set(RMNG_THIRD_PARTY_ESP_AWS_IOT_SHA256 "f1fdfef6af91148b82aee85d61ea1ae5419a1955068cd6452610e55d05dff431")
set(RMNG_THIRD_PARTY_ESP_AWS_IOT_COMMIT "9c879feedb699611e7c220b93aed84c84322ff84")

set(RMNG_THIRD_PARTY_ESP_AWS_IOT_LIBRARIES backoffAlgorithm coreMQTT coreMQTT-Agent coreJSON
                                           Jobs-for-AWS-IoT-embedded-sdk aws-iot-core-mqtt-file-streams-embedded-c
)

set(RMNG_THIRD_PARTY_ESP_AWS_IOT_backoffAlgorithm_URL
    "https://github.com/FreeRTOS/backoffAlgorithm/archive/3cc0bf8c4d41d7cd7aa2f1ded5075c87bf6b2bbb.tar.gz"
)
set(RMNG_THIRD_PARTY_ESP_AWS_IOT_backoffAlgorithm_SHA256
    "f9ac84eb551072f161a4b8d187d43fd030c41c035e17b1ea4fc51d6d01b94d06"
)

set(RMNG_THIRD_PARTY_ESP_AWS_IOT_coreMQTT_URL
    "https://github.com/FreeRTOS/coreMQTT/archive/2beef04725328923e05e576b884212d53ec97af7.tar.gz"
)
set(RMNG_THIRD_PARTY_ESP_AWS_IOT_coreMQTT_SHA256 "bb994633910488c899f99fff59cb3a798b73bf37e2bb6c8cd22935cca2b442d7")

set(RMNG_THIRD_PARTY_ESP_AWS_IOT_coreMQTT-Agent_URL
    "https://github.com/FreeRTOS/coreMQTT-Agent/archive/498e283825dac3d45fa2f5cd47a061771896bd12.tar.gz"
)
set(RMNG_THIRD_PARTY_ESP_AWS_IOT_coreMQTT-Agent_SHA256
    "d3fc24ebcb47fed037da986ca8503704701e1732f52699d7dd733c7a1d180cb2"
)

set(RMNG_THIRD_PARTY_ESP_AWS_IOT_coreJSON_URL
    "https://github.com/FreeRTOS/coreJSON/archive/e3b7663f6392d8c10e8db57506ec37e4801b145a.tar.gz"
)
set(RMNG_THIRD_PARTY_ESP_AWS_IOT_coreJSON_SHA256 "e49a8112515eb8ffcc88b7b3b18e1400ff1d09482482e3fef6b17c373444db4d")

# esp_rmaker_neo_ota targets the Jobs 1.5.1 API; check it before you move this commit.
set(RMNG_THIRD_PARTY_ESP_AWS_IOT_Jobs-for-AWS-IoT-embedded-sdk_URL
    "https://github.com/aws/Jobs-for-AWS-IoT-embedded-sdk/archive/3ce91f56427653705891a8668568cb247b97905f.tar.gz"
)
set(RMNG_THIRD_PARTY_ESP_AWS_IOT_Jobs-for-AWS-IoT-embedded-sdk_SHA256
    "783574549bc8e19de6eb409c80b1ab5a746cc1d2d8ff6eb701ceee16648e5fa4"
)

set(RMNG_THIRD_PARTY_ESP_AWS_IOT_aws-iot-core-mqtt-file-streams-embedded-c_URL
    "https://github.com/aws/aws-iot-core-mqtt-file-streams-embedded-c/archive/73182a76fc71b0e5400e80b0e647aa58dcf4c7ad.tar.gz"
)
set(RMNG_THIRD_PARTY_ESP_AWS_IOT_aws-iot-core-mqtt-file-streams-embedded-c_SHA256
    "5768aa61546ddb957902b01855360b630f22c14c41b8dadab6c5a18641e20ec8"
)
