FROM ubuntu:24.04

# Set folder names
ARG APP_DIR=/app
ARG EXPOSED_FILES_DIR=$APP_DIR/exposed_files
ARG EXPOSED_TEST_MQTT_COMMON_DIR=$EXPOSED_FILES_DIR/test-mqtt-common
ENV EXPOSED_TEST_MQTT_COMMON_DIR=$EXPOSED_TEST_MQTT_COMMON_DIR
ARG EXPOSED_TEST_HTTP_COMMON_DIR=$EXPOSED_FILES_DIR/test-http-common
ENV EXPOSED_TEST_HTTP_COMMON_DIR=$EXPOSED_TEST_HTTP_COMMON_DIR

# Make sure install is non-interactive
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# Install basic build tools and dependencies for mosquitto and openssl
RUN apt-get update && apt-get install -y \
    cmake \
    gcc \
    libbsd-dev \
    libffi-dev \
    ninja-build \
    build-essential \
    gcovr \
    mosquitto \
    mosquitto-clients \
    openssl \
    git \
    curl \
    jq \
    python3 \
    python3-venv \
    python3-pip \
    unzip \
    && rm -rf /var/lib/apt/lists/*

# Install AWS CLI
ADD "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" ./awscliv2.zip
RUN unzip ./awscliv2.zip && \
    ./aws/install && \
    rm -rf ./awscliv2.zip ./aws

# Python httpbin: local HTTP test server for test-http-common (replaces flaky public httpbin.org).
# Use the Python implementation (same as httpbin.org) so its JSON shape matches the test
# assertions exactly; go-httpbin differs (header values as arrays, no HEAD Content-Length).
# Installed in a dedicated venv. hbapp.py enables pretty-printed JSON so header/body echoes
# render as `"Key": "value"` (with the spacing httpbin.org uses), which the tests grep for.
RUN python3 -m venv /opt/httpbin-venv && \
    /opt/httpbin-venv/bin/pip install --no-cache-dir httpbin==0.10.2 gunicorn==23.0.0 && \
    printf 'from httpbin import app\napp.json.compact = False\n' > /opt/httpbin-venv/hbapp.py

# Pre-warm a pip download cache with the static test tooling the CI jobs install
# on every run (pytest plugins + gcovr from posix_requirements.txt). The wheels
# live in PIP_CACHE_DIR so the per-job `pip install` is download-free from run 1
# (no GitLab cache restore needed). The throwaway venv is removed; the cache stays.
ENV PIP_CACHE_DIR=/opt/pip-cache
RUN python3 -m venv /opt/_pipwarm && \
    /opt/_pipwarm/bin/pip install \
        pytest pytest-xdist pytest-rerunfailures pytest-html gcovr==8.6 && \
    rm -rf /opt/_pipwarm

# Avahi: libavahi-client-dev supplies pkg-config (avahi-client) for POSIX mDNS;
# avahi-daemon is required at runtime for the client library to publish services.
RUN apt-get update && apt-get install -y \
    libavahi-client-dev \
    avahi-daemon \
    && rm -rf /var/lib/apt/lists/*

# Create a non-root user for running the broker
RUN useradd -r -s /bin/false mosquitto_user

# Create directory for certificates
RUN mkdir -p $EXPOSED_TEST_MQTT_COMMON_DIR

# Create mosquitto setup and run script
RUN echo '#!/bin/bash\n\
set -e\n\
\n\
CERTS_DIR="$(realpath "$1")"\n\
CONFIG_OUT="$(realpath "$2")"\n\
VALIDITY_DAYS="${3:-365}"\n\
\n\
if [ -z "$CERTS_DIR" ] || [ -z "$CONFIG_OUT" ]; then\n\
    echo "Usage: $0 <certs_directory> <config_output_file> [validity_days]"\n\
    echo "Example: $0 /app/exposed_files/test-mqtt-common/certs /app/mosquitto_config/mosquitto.conf 365"\n\
    exit 1\n\
fi\n\
\n\
echo "Setting up MQTT certificates and configuration..."\n\
echo "Certificate directory: $CERTS_DIR"\n\
echo "Config output file: $CONFIG_OUT"\n\
\n\
# Generate TLS certificates\n\
echo "Generating TLS certificates in $CERTS_DIR (valid for $VALIDITY_DAYS days)..."\n\
mkdir -p "$CERTS_DIR"\n\
\n\
# Generate CA private key and certificate\n\
openssl genrsa -out "$CERTS_DIR/ca.key" 2048\n\
openssl req -new -x509 -days "$VALIDITY_DAYS" -key "$CERTS_DIR/ca.key" -out "$CERTS_DIR/test_mqtt_common_host_crt.pem" -subj "/C=US/ST=Test/L=Test/O=Test/CN=TestCA"\n\
\n\
# Generate server private key and certificate\n\
openssl genrsa -out "$CERTS_DIR/server.key" 2048\n\
openssl req -new -out "$CERTS_DIR/server.csr" -key "$CERTS_DIR/server.key" -subj "/C=US/ST=Test/L=Test/O=Test/CN=localhost"\n\
openssl x509 -req -in "$CERTS_DIR/server.csr" -CA "$CERTS_DIR/test_mqtt_common_host_crt.pem" -CAkey "$CERTS_DIR/ca.key" -CAcreateserial -out "$CERTS_DIR/server.crt" -days "$VALIDITY_DAYS"\n\
\n\
# Generate client private key and certificate\n\
openssl genrsa -out "$CERTS_DIR/test_mqtt_common_client.key" 2048\n\
openssl req -new -out "$CERTS_DIR/client.csr" -key "$CERTS_DIR/test_mqtt_common_client.key" -subj "/C=US/ST=Test/L=Test/O=Test/CN=client"\n\
openssl x509 -req -in "$CERTS_DIR/client.csr" -CA "$CERTS_DIR/test_mqtt_common_host_crt.pem" -CAkey "$CERTS_DIR/ca.key" -CAcreateserial -out "$CERTS_DIR/test_mqtt_common_client.crt" -days "$VALIDITY_DAYS"\n\
\n\
# Clean up intermediate files\n\
rm "$CERTS_DIR/ca.key" "$CERTS_DIR/server.csr" "$CERTS_DIR/client.csr"\n\
\n\
# Fix permissions for mosquitto access\n\
chmod 644 "$CERTS_DIR/server.key" "$CERTS_DIR/test_mqtt_common_client.key"\n\
\n\
echo "Certificates generated successfully:" && ls -la "$CERTS_DIR"/*.crt "$CERTS_DIR"/*.key\n\
\n\
# Generate mosquitto configuration\n\
echo "Generating mosquitto configuration at $CONFIG_OUT..."\n\
CONFIG_DIR="$(dirname "$CONFIG_OUT")"\n\
mkdir -p "$CONFIG_DIR"\n\
\n\
cat > "$CONFIG_OUT" << EOF\n\
# Mosquitto configuration for TLS testing\n\
per_listener_settings true\n\
\n\
# Port 8883 (TLS without client authentication)\n\
listener 8883\n\
cafile $CERTS_DIR/test_mqtt_common_host_crt.pem\n\
certfile $CERTS_DIR/server.crt\n\
keyfile $CERTS_DIR/server.key\n\
require_certificate false\n\
allow_anonymous true\n\
\n\
# Port 8884 (TLS with client authentication)\n\
listener 8884\n\
cafile $CERTS_DIR/test_mqtt_common_host_crt.pem\n\
certfile $CERTS_DIR/server.crt\n\
keyfile $CERTS_DIR/server.key\n\
require_certificate true\n\
allow_anonymous true\n\
EOF\n\
\n\
echo "Mosquitto configuration created at $CONFIG_OUT"\n\
echo "MQTT certificates and configuration setup complete!"\n\
' > /usr/local/bin/mosquitto_setup_conf.sh && chmod +x /usr/local/bin/mosquitto_setup_conf.sh

# Create mosquitto run script
RUN echo '#!/bin/bash\n\
set -e\n\
\n\
if [ -z "$1" ]; then\n\
    echo "Usage: $0 <config_file> [log_file] [pid_file]"\n\
    echo "Example: $0 /app/mosquitto_config/mosquitto.conf /tmp/mosquitto.log /tmp/mosquitto.pid"\n\
    exit 1\n\
fi\n\
\n\
CONFIG_FILE="$(realpath "$1")"\n\
LOG_FILE="${2:-/tmp/mosquitto.log}"\n\
PID_FILE="${3:-/tmp/mosquitto.pid}"\n\
\n\
if [ ! -f "$CONFIG_FILE" ]; then\n\
    echo "Error: Config file $CONFIG_FILE does not exist"\n\
    exit 1\n\
fi\n\
\n\
echo "Starting Mosquitto MQTT broker with config: $CONFIG_FILE"\n\
echo "  Log: $LOG_FILE"\n\
echo "  PID: $PID_FILE"\n\
\n\
mosquitto -c "$CONFIG_FILE" -v > "$LOG_FILE" 2>&1 &\n\
echo $! > "$PID_FILE"\n\
\n\
# Wait a moment for broker to start\n\
sleep 2\n\
\n\
# Check broker still alive via PID\n\
if kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then\n\
    echo "Mosquitto broker started successfully on ports 8883 (TLS) and 8884 (mutual TLS)"\n\
else\n\
    echo "Failed to start Mosquitto broker. Log:" >&2\n\
    cat "$LOG_FILE" >&2\n\
    exit 1\n\
fi\n\
\n\
echo "Mosquitto broker is running!"\n\
' > /usr/local/bin/mosquitto_run.sh && chmod +x /usr/local/bin/mosquitto_run.sh

# Create go-httpbin certificate setup script for test-http-common.
# Generates a self-signed cert for CN=localhost (with SAN for localhost/127.0.0.1).
# The cert doubles as the TLS server cert AND the trust anchor: CI appends it to the
# POSIX CA bundle, so curl's CURLOPT_SSL_VERIFYPEER passes for https://localhost:8443.
RUN echo '#!/bin/bash\n\
set -e\n\
\n\
CERTS_DIR="$(realpath "$1")"\n\
VALIDITY_DAYS="${2:-3650}"\n\
\n\
if [ -z "$CERTS_DIR" ]; then\n\
    echo "Usage: $0 <certs_directory> [validity_days]"\n\
    exit 1\n\
fi\n\
\n\
echo "Generating go-httpbin TLS certificate in $CERTS_DIR (valid for $VALIDITY_DAYS days)..."\n\
mkdir -p "$CERTS_DIR"\n\
\n\
openssl req -x509 -newkey rsa:2048 -nodes \\\n\
    -keyout "$CERTS_DIR/httpbin_server.key" \\\n\
    -out "$CERTS_DIR/httpbin_server.crt" \\\n\
    -days "$VALIDITY_DAYS" \\\n\
    -subj "/C=US/ST=Test/L=Test/O=Test/CN=localhost" \\\n\
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"\n\
\n\
chmod 644 "$CERTS_DIR/httpbin_server.key"\n\
echo "go-httpbin certificate generated:" && ls -la "$CERTS_DIR"/httpbin_server.*\n\
' > /usr/local/bin/httpbin_setup_conf.sh && chmod +x /usr/local/bin/httpbin_setup_conf.sh

# Create httpbin run script: starts plain HTTP on 8080 and TLS on 8443 via gunicorn.
RUN echo '#!/bin/bash\n\
set -e\n\
\n\
CERTS_DIR="$(realpath "$1")"\n\
LOG_FILE="${2:-/tmp/httpbin.log}"\n\
PID_FILE="${3:-/tmp/httpbin.pid}"\n\
GUNICORN=/opt/httpbin-venv/bin/gunicorn\n\
APP_DIR=/opt/httpbin-venv\n\
\n\
if [ -z "$CERTS_DIR" ]; then\n\
    echo "Usage: $0 <certs_directory> [log_file] [pid_file]"\n\
    exit 1\n\
fi\n\
\n\
echo "Starting httpbin (HTTP :8080, HTTPS :8443)"\n\
echo "  Log: $LOG_FILE"\n\
echo "  PID: $PID_FILE"\n\
\n\
: > "$PID_FILE"\n\
"$GUNICORN" --chdir "$APP_DIR" -b 0.0.0.0:8080 hbapp:app >> "$LOG_FILE" 2>&1 &\n\
echo $! >> "$PID_FILE"\n\
"$GUNICORN" --chdir "$APP_DIR" -b 0.0.0.0:8443 \\\n\
    --certfile "$CERTS_DIR/httpbin_server.crt" \\\n\
    --keyfile "$CERTS_DIR/httpbin_server.key" hbapp:app >> "$LOG_FILE" 2>&1 &\n\
echo $! >> "$PID_FILE"\n\
\n\
sleep 3\n\
for pid in $(cat "$PID_FILE"); do\n\
    if ! kill -0 "$pid" 2>/dev/null; then\n\
        echo "Failed to start httpbin. Log:" >&2\n\
        cat "$LOG_FILE" >&2\n\
        exit 1\n\
    fi\n\
done\n\
echo "httpbin is running!"\n\
' > /usr/local/bin/httpbin_run.sh && chmod +x /usr/local/bin/httpbin_run.sh

# Create sdkconfig.defaults for POSIX HTTP test configuration.
# Overrides the Kconfig defaults (public httpbin.org) to the local go-httpbin
# sidecar started by httpbin_run.sh (HTTP :8080, HTTPS :8443).
RUN mkdir -p $EXPOSED_TEST_HTTP_COMMON_DIR && echo "\
CONFIG_TEST_HTTP_COMMON_URL_TCP=\"http://localhost:8080\"\n\
CONFIG_TEST_HTTP_COMMON_URL_TLS=\"https://localhost:8443\"\n\
" > $EXPOSED_TEST_HTTP_COMMON_DIR/sdkconfig.defaults

# Create sdkconfig.defaults for POSIX MQTT test configuration
RUN echo "# MQTT Common Test Configuration for POSIX\n\
CONFIG_TEST_MQTT_COMMON_BROKER_URI=\"localhost\"\n\
CONFIG_TEST_MQTT_COMMON_TCP_USERNAME=\"username\"\n\
CONFIG_TEST_MQTT_COMMON_TCP_PASSWORD=\"password\"\n\
CONFIG_TEST_MQTT_COMMON_PORT_TCP=1883\n\
CONFIG_TEST_MQTT_COMMON_PORT_TLS_SERVER_ONLY=8883\n\
CONFIG_TEST_MQTT_COMMON_PORT_TLS_MUTUAL_AUTH=8884\n\
" > $EXPOSED_TEST_MQTT_COMMON_DIR/sdkconfig.defaults

# Expose MQTT TLS ports and go-httpbin ports
EXPOSE 8883 8884 8080 8443
