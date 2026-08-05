# Internal osal sub-build: the POSIX HTTP client implementation (libcurl). Added via add_subdirectory() from the osal
# CMakeLists when HTTP is enabled; the ESP-IDF implementation is compiled directly into the osal component.
if (ESP_PLATFORM)
    message(FATAL_ERROR "osal/http is added as a subdirectory on POSIX only")
else ()
    set(HTTP_COMMON_NAME osal_http)
    set(HTTP_COMMON_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include")
    set(HTTP_COMMON_REQUIRES osal)
    set(HTTP_COMMON_PRIV_REQUIRES osal mbedtls libcurl)

    # Add third-party libraries (without unit tests)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW) # allow option override
    set(ENABLE_TESTING OFF)

    # Add mbedtls
    include(${CMAKE_CURRENT_LIST_DIR}/../../../third_party/install_scripts/mbedtls.cmake)
    mbedtls_make_available()

    # Set up libcurl to use mbedtls (must be set before curl is configured via FetchContent). Forces curl to use mbedtls
    # and disables the other SSL backends.
    set(CURL_USE_MBEDTLS
        ON
        CACHE BOOL "" FORCE
    )
    set(CURL_USE_OPENSSL
        OFF
        CACHE BOOL "" FORCE
    )
    set(CURL_USE_SCHANNEL
        OFF
        CACHE BOOL "" FORCE
    ) # Windows native
    set(CURL_USE_SECTRANSP
        OFF
        CACHE BOOL "" FORCE
    ) # macOS native

    # Disable all other non-required things
    set(CURL_USE_LIBPSL
        OFF
        CACHE BOOL "" FORCE
    )
    set(USE_LIBIDN2
        OFF
        CACHE BOOL "" FORCE
    )
    set(CURL_DISABLE_LDAP
        ON
        CACHE BOOL "" FORCE
    )
    set(CURL_DISABLE_LDAPS
        ON
        CACHE BOOL "" FORCE
    )
    set(CURL_ZSTD
        OFF
        CACHE BOOL "" FORCE
    )
    set(CURL_BROTLI
        OFF
        CACHE BOOL "" FORCE
    )
    set(USE_NGHTTP2
        OFF
        CACHE BOOL "" FORCE
    )
    set(CURL_USE_LIBSSH2
        OFF
        CACHE BOOL "" FORCE
    )
    set(CURL_USE_LIBSSH
        OFF
        CACHE BOOL "" FORCE
    )
    set(CURL_USE_GSSAPI
        OFF
        CACHE BOOL "" FORCE
    )
    set(CURL_USE_OPENLDAP
        OFF
        CACHE BOOL "" FORCE
    )

    # Disable unused protocols (keep HTTP/HTTPS only)
    foreach (
        _proto
        FTP
        FILE
        TELNET
        TFTP
        RTSP
        DICT
        GOPHER
        POP3
        IMAP
        SMB
        SMTP
        MQTT
    )
        set(CURL_DISABLE_${_proto}
            ON
            CACHE BOOL "" FORCE
        )
    endforeach ()

    # Disable unused features
    foreach (
        _feat
        ALTSVC
        HSTS
        COOKIES
        NTLM
        DOH
        AWS
        NEGOTIATE_AUTH
        KERBEROS_AUTH
        SRP
        NETRC
        PROGRESS_METER
        VERBOSE_STRINGS
        SHUFFLE_DNS
        FORM_API
        HEADERS_API
        WEBSOCKETS
        CA_SEARCH
        BINDLOCAL
        LIBCURL_OPTION
        GETOPTIONS
        PARSEDATE
    )
        set(CURL_DISABLE_${_feat}
            ON
            CACHE BOOL "" FORCE
        )
    endforeach ()

    # Skip curl CLI, manual, docs, tests, examples
    set(BUILD_CURL_EXE
        OFF
        CACHE BOOL "" FORCE
    )
    set(ENABLE_CURL_MANUAL
        OFF
        CACHE BOOL "" FORCE
    )
    set(BUILD_TESTING
        OFF
        CACHE BOOL "" FORCE
    )
    set(BUILD_EXAMPLES
        OFF
        CACHE BOOL "" FORCE
    )
    set(BUILD_LIBCURL_DOCS
        OFF
        CACHE BOOL "" FORCE
    )
    set(BUILD_MISC_DOCS
        OFF
        CACHE BOOL "" FORCE
    )
    set(ENABLE_ARES
        OFF
        CACHE BOOL "" FORCE
    )
    set(CURL_LTO
        OFF
        CACHE BOOL "" FORCE
    )
    set(CURL_WERROR
        OFF
        CACHE BOOL "" FORCE
    )
    set(PICKY_COMPILER
        OFF
        CACHE BOOL "" FORCE
    )

    # Point curl to the mbedtls include directory (exported by mbedtls_make_available())
    set(MBEDTLS_INCLUDE_DIR
        "${RMNG_MBEDTLS_INCLUDE_DIR}"
        CACHE PATH "" FORCE
    )

    # Force curl to link against the mbedtls CMake targets you just built (mbedtls splits into crypto, x509, and mbedtls
    # lib)
    set(MBEDTLS_LIBRARY
        mbedtls
        CACHE STRING "" FORCE
    )
    set(MBEDX509_LIBRARY
        mbedx509
        CACHE STRING "" FORCE
    )
    set(MBEDCRYPTO_LIBRARY
        tfpsacrypto
        CACHE STRING "" FORCE
    )

    set(CURL_DISABLE_INSTALL
        ON
        CACHE BOOL "" FORCE
    )

    # Use static library
    set(BUILD_SHARED_LIBS
        OFF
        CACHE BOOL "" FORCE
    )

    include(${CMAKE_CURRENT_LIST_DIR}/fetch_curl.cmake)
    http_common_curl_make_available()

    set(ENABLE_TESTING ${CMAKE_TESTING_ENABLED})

    list(APPEND HTTP_COMMON_SRCS "src/http_libcurl_impl.c")

    add_library(${HTTP_COMMON_NAME} STATIC ${HTTP_COMMON_SRCS})
    target_include_directories(${HTTP_COMMON_NAME} PUBLIC ${HTTP_COMMON_INCLUDE_DIRS})
    target_link_libraries(${HTTP_COMMON_NAME} PUBLIC ${HTTP_COMMON_REQUIRES})
    target_link_libraries(${HTTP_COMMON_NAME} PRIVATE ${HTTP_COMMON_PRIV_REQUIRES})

    # Unit tests
    if (CMAKE_TESTING_ENABLED)
        add_subdirectory(test-http-common)
    endif ()
endif ()
