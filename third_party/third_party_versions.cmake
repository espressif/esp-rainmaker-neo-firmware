include_guard(GLOBAL)

# Pinned release archives for FetchContent under third_party/ (Unity, mbedtls, protobuf-c). *_COMMIT is the commit of
# the release tag; a clean git clone at that commit is replaced by the archive.

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
