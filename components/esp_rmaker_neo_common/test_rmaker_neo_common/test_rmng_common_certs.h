/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file test_rmng_common_certs.h
 * @brief Throwaway self-signed P-256 certificates for the RainMaker Neo common tests.
 */

#ifndef __TEST_RMNG_COMMON_CERTS_H__
#define __TEST_RMNG_COMMON_CERTS_H__

/** Subject "CN=rmng-unit-test-do-not-use". */
#define TEST_CERT_CN    "-----BEGIN CERTIFICATE-----\nMIIBnjCCAUWgAwIBAgIUOTYoQNMLZHsx3iPyN39RYG8oSRswCgYIKoZIzj0EAwIw\nJDEiMCAGA1UEAwwZcm1uZy11bml0LXRlc3QtZG8tbm90LXVzZTAgFw0yNjA3MzEx\nMTQ3NTJaGA8yMTI2MDcwNzExNDc1MlowJDEiMCAGA1UEAwwZcm1uZy11bml0LXRl\nc3QtZG8tbm90LXVzZTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABHnEF75bu35w\noe5HG9NAqweHXCkqc4kCwYmCME4vzxe6q6Qkm3iHRjk7/dUvbrLRcwQ7WEc69NGg\npx4Pd+gOk7yjUzBRMB0GA1UdDgQWBBRBfFfVvr/aFrpwwwMfik3V786EezAfBgNV\nHSMEGDAWgBRBfFfVvr/aFrpwwwMfik3V786EezAPBgNVHRMBAf8EBTADAQH/MAoG\nCCqGSM49BAMCA0cAMEQCIA48t8EJotP4GTyKQ2bav/muY1OYoD8kfYe+w2ko8/4d\nAiA5frtr4uL2TAdJbayY7e9+/9m/rBiwGEHdO9i8Mx+KbA==\n-----END CERTIFICATE-----\n"

/** Subject "O=rmng-unit-test-no-cn", with no Common Name. */
#define TEST_CERT_NO_CN "-----BEGIN CERTIFICATE-----\nMIIBlTCCATugAwIBAgIUTi2bwblo7xH0P7vIQ3glkjzX/igwCgYIKoZIzj0EAwIw\nHzEdMBsGA1UECgwUcm1uZy11bml0LXRlc3Qtbm8tY24wIBcNMjYxMDAyMTAxMDMx\nWhgPMjEyNjA5MDgxMDEwMzFaMB8xHTAbBgNVBAoMFHJtbmctdW5pdC10ZXN0LW5v\nLWNuMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE6T/OKOgGcuOv92vsEnQrZBjT\nnfYWTtJaNX9HvI5soJhBH2et6OVYp5VK+Vtan7o2ZmVHO3nN5/ZjAlfJRCzs4KNT\nMFEwHQYDVR0OBBYEFKGaiCFUAVmtQR7ECROk7k97kwZmMB8GA1UdIwQYMBaAFKGa\niCFUAVmtQR7ECROk7k97kwZmMA8GA1UdEwEB/wQFMAMBAf8wCgYIKoZIzj0EAwID\nSAAwRQIhALBAImAsg+MhE9wRjhKaoDYztMDyq5GL+TfYGOrS+0ySAiBXz5dlsiAu\nKiFYHDA+K5X38z2FHk5przt77kZevfTQLw==\n-----END CERTIFICATE-----\n"

#endif /* __TEST_RMNG_COMMON_CERTS_H__ */
