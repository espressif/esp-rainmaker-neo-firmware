# MQTT Common Tests

This is an integration test component for the `mqtt-common` component. It tests MQTT connectivity with a TLS-enabled broker. Current testing pathways include:
- sub-pub-unsub-pub over TLS
- sub-pub-unsub-pub over TLS, with mutual authentication

## Important setup

### Broker

Before running this test, ensure that you are using a broker with **two separate ports**, both with **no username/password authentication**:
- TLS
- TLS with mutual authentication

The easiest way to do this is to install [mosquitto](https://mosquitto.org) and run it with the following configurations:
```
# allow different settings per port
per_listener_settings true

# Port 8883 (TLS without client authentication)
listener 8883
cafile <path to local CA certificate>
certfile <path to server certificate>
keyfile <path to server private key>
require_certificate false
allow_anonymous true

# Port 8884 (TLS with client authentication)
listener 8884
cafile <path to local CA certificate>
certfile <path to server certificate>
keyfile <path to server private key>
require_certificate true
allow_anonymous true
```

Refer to [credential file generation](#generation) if you are unsure on how to generate required files.

### Credential files

There are 3 credential files to provide under a `certs` folder (or you can modify the folder name in `CMakeLists.txt`):
- `test_mqtt_common_host_crt.pem`: local CA certificate
- `test_mqtt_common_client.crt`: client certificate
- `test_mqtt_common_client.key`: client key

#### Generation

The following steps are with reference to [this](http://www.steves-internet-guide.com/mosquitto-tls/), and using `openssl`.

To generate all the files, you'll need to:
1. Create a local CA (private key + certificate).
2. Get server and client private key + certificate pairs, each following the steps under *Getting CA-signed certificates*.

You'll end up with:
- Local CA private key + certificate
- Server private key + certificate (used by the broker)
- Client private key + certificate (used by clients of the broker)

##### Common

- RSA private key: `openssl genrsa -out <key file name> [-des3 if password protected] 2048`

###### Creating local CA

1. Make private key for CA
2. Make CA's certificate: `openssl req -new -x509 -days <validity days> -key <CA private key file name> -out <CA certificate file name>`

##### Getting CA-signed certificates

1. Make private key for certificate
2. Make a certificate signing request (CSR): `openssl req -new -out <CSR file name> -key <private key file name>`
3. Use CA to process CSR and generate certificate: `openssl x509 -req -in <CSR file name> -CA <CA certificate file name> -CAkey <CA private key file name> -CAcreateserial -out <certificate file name> -days <certificate validity days>`

### Configuration

On **ESP-IDF**, use `idf.py menuconfig` and configure the required menu `Component config > (test-mqtt-common) MQTT Common Test Configuration`:
- Provide the CA certificate under `MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH`.

On **normal POSIX builds**, use `[make/ninja] menuconfig` and configure the required menu `Component config > (test-mqtt-common) MQTT Common Test Configuration`:
- Provide the CA certificate under `OSAL_MQTT_CUSTOM_CERTIFICATE_BUNDLE_PATH`.

*NOTE*: TCP-only is not supported yet, and so the configurations relating to it are not used.
