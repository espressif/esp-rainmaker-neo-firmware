# HTTP Common Component

The HTTP Common component provides a unified interface for HTTP client operations across different platforms and implementations. It abstracts the underlying HTTP implementation details and provides a consistent API for making HTTP requests.

## Features

- Platform-agnostic HTTP client interface
- Support for common HTTP methods (GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS)
- Header management (set, get, delete)
- SSL/TLS support
- Configurable timeouts and buffer sizes
- Keep-alive connections
- Synchronous callback-based response handling

## Usage Guide

### Initialization

1. **Setup the implementation**: First call `http_common_impl_setup()` to populate the interface.

   ```c
   http_common_impl_t http_impl;
   os_err_t status = http_common_impl_setup(&http_impl);
   if (status != OS_ERR_OK) {
       // Handle error
   }
   ```

2. **Initialize the HTTP client**: Call `impl.init()` with your configuration.

   ```c
   http_common_config_t config = {
       .url = "https://example.com/api",
       .method = HTTP_COMMON_METHOD_GET,
       .timeout_ms = 5000,
       // ... other configuration options
   };

   http_common_handle_t handle;
   status = http_impl.init(&config, &handle);
   if (status != OS_ERR_OK) {
       // Handle error
   }
   ```

### Making HTTP Requests

For each HTTP request, follow these steps:

3. **Configure request (optional)**:
   - Set the URL and method if not already set in config:
     ```c
     http_impl.set_url(handle, "https://example.com/api/data");
     http_impl.set_method(handle, HTTP_COMMON_METHOD_POST);
     ```

   - Set headers using `set_header()`:
     ```c
     http_impl.set_header(handle, "Content-Type", "application/json");
     ```

   - For requests with a body, set the request body:
     ```c
     const char *json_payload = "{\"key\": \"value\"}";
     http_impl.set_request_body(handle, (uint8_t *)json_payload, strlen(json_payload), "application/json");
     ```

4. **Set response callback**: Register a callback function to receive response data as it arrives.

   ```c
   int my_read_callback(uint8_t *data, size_t data_len, int64_t content_length) {
       // Process the received data chunk
       printf("Received %zu bytes of data\n", data_len);
       // Return the number of bytes processed (typically data_len)
       return data_len;
   }

   status = http_impl.set_read_callback(handle, my_read_callback);
   if (status != OS_ERR_OK) {
       // Handle error
   }
   ```

5. **Perform the request**: Call `perform()` to execute the HTTP request synchronously. The request blocks until complete, and your callback receives response data as it arrives.

   ```c
   status = http_impl.perform(handle);
   if (status != OS_ERR_OK) {
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
   if (status != OS_ERR_OK) {
       // Handle error
   }
   ```

### Complete Request Flow

The typical flow is: **1 → 2 → (3-6) for as many requests as required → 7**.

```c
// 1. Setup implementation
http_common_impl_t http_impl;
http_common_impl_setup(&http_impl);

// 2. Initialize
http_common_config_t config = { /* ... */ };
http_common_handle_t handle;
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

The `http_common_config_t` structure supports the following options:

- **Connection**: `url`, `host`, `port`, `path`
- **Request**: `method`
- **SSL/TLS**: `client_cert`, `client_key`, `skip_cert_common_name_check`, `common_name`
  - Server CA uses the platform certificate bundle (ESP: `esp_crt_bundle_attach`; POSIX: `ca-bundle-posix`).
- **Transport**: `transport_type`
- **Timeouts**: `timeout_ms`
- **Buffers**: `buffer_size_tx`, `buffer_size_rx`
- **Keep-alive**: `keep_alive_enable`, `keep_alive_idle`, `keep_alive_interval`, `keep_alive_count`
- **User data**: `user_data`

## Error Handling

All functions return `os_err_t` values:
- `OS_ERR_OK`: Operation completed successfully
- `OS_ERR_FAIL`: Generic error
- `OS_ERR_INVALID_ARG`: Invalid parameter
- `OS_ERR_INVALID_STATE`: Invalid state for operation
- `OS_ERR_NO_MEM`: Buffer too small
- `OS_ERR_TIMEOUT`: Operation timed out
- `OS_ERR_HTTP_CONNECTION_FAILED`: Connection failed
- `OS_ERR_INVALID_RESPONSE`: Invalid HTTP response


## Examples

See the `test-http-common/` directory for comprehensive examples including:

- Basic HTTP operations (GET, POST)
- Header management
- Streaming responses
- Request body handling
- Multiple request scenarios
- Error handling
