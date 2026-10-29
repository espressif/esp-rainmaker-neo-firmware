# osal/http

A unified HTTP client interface across platforms.

## Features

- Platform-agnostic HTTP client interface
- Support for common HTTP methods (GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS)
- Header management (set, remove, delete all)
- SSL/TLS support
- Configurable timeouts and buffer sizes
- Keep-alive connections
- Synchronous callback-based response handling

## Usage Guide

### Initialization

1. **Setup the implementation**: First call `osal_http_impl_setup()` to populate the interface.

   ```c
   osal_http_impl_t http_impl;
   osal_err_t status = osal_http_impl_setup(&http_impl);
   if (status != OSAL_ERR_OK) {
       // Handle error
   }
   ```

2. **Initialize the HTTP client**: Call `impl.init()` with your configuration.

   ```c
   osal_http_config_t config = {
       .url = "https://example.com/api",
       .method = OSAL_HTTP_METHOD_GET,
       .timeout_ms = 5000,
       // ... other configuration options
   };

   osal_http_handle_t handle;
   status = http_impl.init(&config, &handle);
   if (status != OSAL_ERR_OK) {
       // Handle error
   }
   ```

### Making HTTP Requests

For each HTTP request, follow these steps:

3. **Configure request (optional)**:
   - Set the URL and method if not already set in config:
     ```c
     http_impl.set_url(handle, "https://example.com/api/data");
     http_impl.set_method(handle, OSAL_HTTP_METHOD_POST);
     ```

   - Set headers using `set_header()`:
     ```c
     http_impl.set_header(handle, "Content-Type", "application/json");
     ```

   - Remove a header with a NULL value. The removal of a header that was never set succeeds:
     ```c
     http_impl.set_header(handle, "Content-Type", NULL);
     ```

   - For requests with a body, set the request body:
     ```c
     const char *json_payload = "{\"key\": \"value\"}";
     http_impl.set_request_body(handle, (uint8_t *)json_payload, strlen(json_payload), "application/json");
     ```

   - A NULL content type keeps the current `Content-Type` header. A NULL body with a size of 0 sets an empty body.
     On ESP-IDF, a NULL body also removes the `Content-Type` header:
     ```c
     http_impl.set_request_body(handle, NULL, 0, NULL);
     ```

   > A handle keeps the method and the body independently. Call `set_method()` and
   > `set_request_body()` in either order; the body does not change the method. A body on
   > `GET`, `DELETE` or `OPTIONS` stays with that method. `POST` and `PUT` with an empty body
   > send `Content-Length: 0`. The body stays on the handle for later requests. Clear it with
   > `set_request_body(handle, NULL, 0, NULL)`.

4. **Set response callback**: Register a callback function to receive response data as it arrives.

   ```c
   int my_read_callback(uint8_t *data, size_t data_len, int64_t content_length) {
       // Process the received data chunk
       printf("Received %zu bytes of data\n", data_len);
       // Return the number of bytes processed (typically data_len)
       return data_len;
   }

   status = http_impl.set_read_callback(handle, my_read_callback);
   if (status != OSAL_ERR_OK) {
       // Handle error
   }
   ```

5. **Perform the request**: Call `perform()` to execute the HTTP request synchronously. The request blocks until complete, and your callback receives response data as it arrives.

   ```c
   status = http_impl.perform(handle);
   if (status != OSAL_ERR_OK) {
       // Handle error
   }
   ```

6. **Process response metadata**:
   - Get status code:
     ```c
     int status_code = http_impl.get_status_code(handle);
     printf("HTTP Status: %d\n", status_code);
     ```

   - Get content length:
     ```c
     int64_t content_len = http_impl.get_content_length(handle);
     printf("Content-Length: %lld\n", content_len);
     ```

### Cleanup

7. **Cleanup resources**: Once HTTP is no longer required, call `cleanup()`.

   ```c
   status = http_impl.cleanup(handle);
   if (status != OSAL_ERR_OK) {
       // Handle error
   }
   ```

### Complete Request Flow

The typical flow is: **1 → 2 → (3-6) for as many requests as required → 7**.

```c
// 1. Setup implementation
osal_http_impl_t http_impl;
osal_http_impl_setup(&http_impl);

// 2. Initialize
osal_http_config_t config = { /* ... */ };
osal_http_handle_t handle;
http_impl.init(&config, &handle);

// 3. Set read callback (once per handle)
http_impl.set_read_callback(handle, my_callback);

// Loop for multiple requests (steps 4-5 can be repeated)
// 4. Configure request (URL, method, headers, body)
// 5. Perform request (callback receives response data)

status = http_impl.perform(handle);

// 6. Cleanup
http_impl.cleanup(handle);
```

## Configuration

The `osal_http_config_t` structure supports the following options:

- **Connection**: `url`, `host`, `port`, `path`
- **Request**: `method`
- **SSL/TLS**: `client_cert`, `client_cert_len`, `client_key`, `client_key_len`,
  `skip_cert_common_name_check`, `common_name`
  - A length of 0 means the cert/key is a NULL-terminated PEM and the implementation derives the length itself.
  - Server CA uses the platform certificate bundle (ESP: `esp_crt_bundle_attach`; POSIX: `ca-bundle-posix`).
- **Transport**: `transport_type`
- **Timeouts**: `timeout_ms`
- **Buffers**: `buffer_size_tx`, `buffer_size_rx`
- **Keep-alive**: `keep_alive_enable`, `keep_alive_idle`, `keep_alive_interval`, `keep_alive_count`

## Error Handling

All functions return `osal_err_t` values:
- `OSAL_ERR_OK`: Operation completed successfully
- `OSAL_ERR_FAIL`: Generic error
- `OSAL_ERR_INVALID_ARG`: Invalid parameter
- `OSAL_ERR_INVALID_STATE`: Invalid state for operation
- `OSAL_ERR_NO_MEM`: Buffer too small
- `OSAL_ERR_TIMEOUT`: Operation timed out
- `OSAL_ERR_HTTP_CONNECTION_FAILED`: Connection failed
- `OSAL_ERR_INVALID_RESPONSE`: Invalid HTTP response


## Examples

See the `test_osal_http/` directory for comprehensive examples including:

- Basic HTTP operations (GET, POST)
- Header management
- Streaming responses
- Request body handling
- Multiple request scenarios
- Error handling
