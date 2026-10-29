# rmaker_aws_sign Integration

Build glue and an OSAL port for the registry component `espressif/rmaker_aws_sign ~1.0.0`. The component itself is not
vendored here — it is fetched from the ESP Component Registry.

The component signs AWS requests with SigV4 using the device X.509 certificate, via the AWS IoT Credential Provider.
Everything platform-specific reaches it through the function pointers in `esp_rmaker_aws_sign.h`, so the SDK supplies
its own port rather than the ESP-IDF one the component ships with.

## Only the platform-independent half is built here

`CMakeLists.txt` takes `RMAKER_AWS_SIGN_SRCS` from the component's `rmaker_aws_sign_variables.cmake` and appends one
of its ESP port files, `port/esp/crypto.c` — it names only PSA and the ops struct, nothing ESP-specific, and the
mbedtls this SDK builds off-target supplies PSA as well.

`src/esp_rmaker_aws_sign_default.c` is excluded by the variables file, and it is the only referrer of the remaining
`port/esp/*` files — so naming it would drag in `esp_http_client`, `esp-tls` and `esp_log` and fail to link
off-target. Being able to take exactly one port file is why the component keeps one file per concern.

## Everything except crypto runs on osal, via the port

`esp_rmaker_aws_sign_port_install(esp_rmaker_aws_sign_port_osal_get())` is called instead of
`esp_rmaker_aws_sign_port_install_default()`, so the component never installs its built-in ESP-IDF port. The port is
global: a feature installs it only when `esp_rmaker_aws_sign_port_is_installed()` is false, then creates its own handle
with `esp_rmaker_aws_sign_create()`. `esp_rmaker_aws_sign_port_osal_get()` returns NULL when the osal HTTP setup fails,
and a later call tries again. That does two things:

- the credential fetch and signed requests go through osal's libcurl client and the clock through `osal_get_time`, so
  firmware tests drive both — the reason those paths are testable off-target at all;
- the component's `esp_http_client` / `esp-tls` / `esp_log` dependencies stay out of the link.

`port/` is SDK code, not part of the component, so it is compiled into esp_rmaker_neo_common — see
`esp_rmaker_neo_common/sources.cmake`, not the `CMakeLists.txt` here. One file per concern: `_osal.c` assembles the table and
holds mem/time/mutex/log, `_osal_http.c` and `_osal_json.c` hold those groups. The same port is used on ESP-IDF for
firmware tests.

### JSON is jsmn, not a second scanner

`osal/json` already ships `json_parser` (jsmn), so the port uses it instead of the component's own scanner. It
tokenizes only: the member walk, the duplicate-key refusal, the string-type check and the RFC 8259 unescaping
(including surrogate pairs) are in `_osal_json.c`, because `json_parser` copies token bytes verbatim and cannot tell an
absent key from a wrongly-typed one. Two things jsmn accepts that the contract does not — a member with no colon, and
raw control characters inside a string — are rejected there as well.

### osal's HTTP client is required

osal builds it only when `OSAL_INCLUDE_HTTP` is on. The component's whole purpose is signing requests it then sends,
so the port `#error`s instead of degrading to a signer with `http` left NULL — enable `CONFIG_OSAL_INCLUDE_HTTP`.

## Tests

`CONFIG_ESP_RMAKER_NEO_TESTING` selects the port, so both unit-test apps build it.
`test_rmaker_neo_common/tests_src/aws_sign/test_aws_sign_port.c` covers the JSON ops on both platforms. On POSIX it also
drives the HTTP ops against the httpbin sidecar of `test_osal_http`: the hint before the body, the hint for an empty
body, an abort from the hint, and a POST with its body and content type.
