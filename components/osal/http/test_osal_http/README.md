# OSAL HTTP Tests

Unit tests for the osal HTTP area.

## Overview

This test component provides comprehensive unit tests for the osal HTTP area (`components/osal/http`), covering:

- Basic HTTPS GET operations
- Basic HTTPS POST operations
- HTTPS requests with custom headers
- HTTPS streaming operations
- HTTP method testing (HEAD, OPTIONS, PUT, DELETE, PATCH)
- Multiple request scenarios
- Authentication testing (Basic Auth)
- Header management operations
- NULL-argument and method/body contracts (`tests_src/test_http_contracts.c`)

## Configuration

The server base URLs are Kconfig options (see `Kconfig`), so they can be repointed without
touching test code:

- `CONFIG_TEST_OSAL_HTTP_URL_TCP` — plain-HTTP base URL (default `http://eu.httpbin.org`)
- `CONFIG_TEST_OSAL_HTTP_URL_TLS` — HTTPS/TLS base URL (default `https://httpbin.org`)

Tests append endpoint paths (`/get`, `/post`, ...) to these bases.

### Running against a local instance

The public `httpbin.org` rate-limits and intermittently returns `503`, which makes the tests
flaky. You can instead run a local [httpbin](https://github.com/postmanlabs/httpbin) instance
and point the tests at it — this is exactly what CI does. Use the Python implementation (the
same one behind httpbin.org); its JSON shape matches the test assertions. `go-httpbin` does
**not** work here: it renders header values as arrays and omits `Content-Length` on `HEAD`.

1. Start httpbin locally, e.g. plain HTTP on `:8080` and TLS on `:8443`. Pretty-printed JSON
   (`app.json.compact = False`) is required so echoes render as `"Key": "value"`:

   ```sh
   pip install httpbin gunicorn
   printf 'from httpbin import app\napp.json.compact = False\n' > hbapp.py
   gunicorn -b 0.0.0.0:8080 hbapp:app &
   gunicorn -b 0.0.0.0:8443 --certfile server.crt --keyfile server.key hbapp:app &
   ```

2. Override the Kconfig URLs (via `menuconfig` or an `sdkconfig.defaults` fragment):

   ```
   CONFIG_TEST_OSAL_HTTP_URL_TCP="http://localhost:8080"
   CONFIG_TEST_OSAL_HTTP_URL_TLS="https://localhost:8443"
   ```

3. For the TLS tests, the server certificate must be trusted by the POSIX CA bundle. Use a
   self-signed `localhost` cert (with `subjectAltName=DNS:localhost,IP:127.0.0.1`) and append
   it to the custom CA bundle via `CONFIG_OSAL_CA_BUNDLE_CUSTOM_CERTIFICATE_BUNDLE_PATH` so
   `CURLOPT_SSL_VERIFYPEER` still passes.
