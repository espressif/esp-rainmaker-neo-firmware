FROM ubuntu:24.04

# Set folder names
ARG APP_DIR=/app

# Environment facts: the single source of truth for what this image serves.
ENV MQTT_HOST=localhost
ENV MQTT_PORT_TCP=1883
ENV MQTT_PORT_TLS_SERVER_ONLY=8883
ENV MQTT_PORT_TLS_MUTUAL_AUTH=8884
ENV MQTT_USERNAME=username
ENV MQTT_PASSWORD=password
ENV MQTT_CA_CERT=mqtt_ca.crt
ENV MQTT_CLIENT_CERT=mqtt_client.crt
ENV MQTT_CLIENT_KEY=mqtt_client.key
ENV HTTP_HOST=localhost
ENV HTTP_PORT_TCP=8080
ENV HTTP_PORT_TLS=8443
ENV HTTP_SERVER_CERT=httpbin_server.crt
ENV HTTP_SERVER_KEY=httpbin_server.key
ENV HTTP_URL_TCP=http://$HTTP_HOST:$HTTP_PORT_TCP
ENV HTTP_URL_TLS=https://$HTTP_HOST:$HTTP_PORT_TLS

# The generators themselves, so a consumer can discover what produces the files
# above without grepping the image. All are on PATH.
ENV MQTT_SETUP_CMD=mosquitto_setup_conf.sh
ENV MQTT_RUN_CMD=mosquitto_run.sh
ENV HTTP_SETUP_CMD=httpbin_setup_conf.sh
ENV HTTP_RUN_CMD=httpbin_run.sh

# Same facts as a readable file.
ENV POSIX_TEST_ENV_FILE=$APP_DIR/posix_test_env.env
RUN mkdir -p $APP_DIR && { \
        echo "# Environment facts for this image. Source with:"; \
        echo "#   set -a; . \"\$POSIX_TEST_ENV_FILE\"; set +a"; \
        echo "#"; \
        echo "# Directories are NOT declared here: each *_SETUP_CMD takes its output"; \
        echo "# directory as argv[1], so the consumer chooses the location and the"; \
        echo "# *_CERT/*_KEY values below are the filenames it will find there."; \
        echo "#   \$MQTT_SETUP_CMD <certs_dir> <mosquitto_conf_out> [validity_days]"; \
        echo "#   \$HTTP_SETUP_CMD <certs_dir> [validity_days]"; \
        echo "MQTT_HOST=$MQTT_HOST"; \
        echo "MQTT_PORT_TCP=$MQTT_PORT_TCP"; \
        echo "MQTT_PORT_TLS_SERVER_ONLY=$MQTT_PORT_TLS_SERVER_ONLY"; \
        echo "MQTT_PORT_TLS_MUTUAL_AUTH=$MQTT_PORT_TLS_MUTUAL_AUTH"; \
        echo "MQTT_USERNAME=$MQTT_USERNAME"; \
        echo "MQTT_PASSWORD=$MQTT_PASSWORD"; \
        echo "MQTT_CA_CERT=$MQTT_CA_CERT"; \
        echo "MQTT_CLIENT_CERT=$MQTT_CLIENT_CERT"; \
        echo "MQTT_CLIENT_KEY=$MQTT_CLIENT_KEY"; \
        echo "HTTP_HOST=$HTTP_HOST"; \
        echo "HTTP_PORT_TCP=$HTTP_PORT_TCP"; \
        echo "HTTP_PORT_TLS=$HTTP_PORT_TLS"; \
        echo "HTTP_SERVER_CERT=$HTTP_SERVER_CERT"; \
        echo "HTTP_SERVER_KEY=$HTTP_SERVER_KEY"; \
        echo "HTTP_URL_TCP=$HTTP_URL_TCP"; \
        echo "HTTP_URL_TLS=$HTTP_URL_TLS"; \
        echo "MQTT_SETUP_CMD=$MQTT_SETUP_CMD"; \
        echo "MQTT_RUN_CMD=$MQTT_RUN_CMD"; \
        echo "HTTP_SETUP_CMD=$HTTP_SETUP_CMD"; \
        echo "HTTP_RUN_CMD=$HTTP_RUN_CMD"; \
    } > $POSIX_TEST_ENV_FILE

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

# Python httpbin: local HTTP test server.
RUN python3 -m venv /opt/httpbin-venv && \
    /opt/httpbin-venv/bin/pip install --no-cache-dir httpbin==0.10.2 gunicorn==23.0.0 && \
    printf 'from httpbin import app\napp.json.compact = False\n' > /opt/httpbin-venv/hbapp.py

# Pre-warm a pip download cache with the static test tooling the CI jobs install
# on every run.
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
    echo "Example: $0 /tmp/mqtt_certs /app/mosquitto_config/mosquitto.conf 365"\n\
    exit 1\n\
fi\n\
\n\
# Source the environment file to get the cert names\n\
set -a\n\
. "$POSIX_TEST_ENV_FILE"\n\
set +a\n\
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
openssl req -new -x509 -days "$VALIDITY_DAYS" -key "$CERTS_DIR/ca.key" -out "$CERTS_DIR/$MQTT_CA_CERT" -subj "/C=US/ST=Test/L=Test/O=Test/CN=TestCA"\n\
\n\
# Generate server private key and certificate\n\
openssl genrsa -out "$CERTS_DIR/server.key" 2048\n\
openssl req -new -out "$CERTS_DIR/server.csr" -key "$CERTS_DIR/server.key" -subj "/C=US/ST=Test/L=Test/O=Test/CN=localhost"\n\
openssl x509 -req -in "$CERTS_DIR/server.csr" -CA "$CERTS_DIR/$MQTT_CA_CERT" -CAkey "$CERTS_DIR/ca.key" -CAcreateserial -out "$CERTS_DIR/server.crt" -days "$VALIDITY_DAYS"\n\
\n\
# Generate client private key and certificate\n\
openssl genrsa -out "$CERTS_DIR/$MQTT_CLIENT_KEY" 2048\n\
openssl req -new -out "$CERTS_DIR/client.csr" -key "$CERTS_DIR/$MQTT_CLIENT_KEY" -subj "/C=US/ST=Test/L=Test/O=Test/CN=client"\n\
openssl x509 -req -in "$CERTS_DIR/client.csr" -CA "$CERTS_DIR/$MQTT_CA_CERT" -CAkey "$CERTS_DIR/ca.key" -CAcreateserial -out "$CERTS_DIR/$MQTT_CLIENT_CERT" -days "$VALIDITY_DAYS"\n\
\n\
# Clean up intermediate files\n\
rm "$CERTS_DIR/ca.key" "$CERTS_DIR/server.csr" "$CERTS_DIR/client.csr"\n\
\n\
# Fix permissions for mosquitto access\n\
chmod 644 "$CERTS_DIR/server.key" "$CERTS_DIR/$MQTT_CLIENT_KEY"\n\
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
# TLS without client authentication\n\
listener $MQTT_PORT_TLS_SERVER_ONLY\n\
cafile $CERTS_DIR/$MQTT_CA_CERT\n\
certfile $CERTS_DIR/server.crt\n\
keyfile $CERTS_DIR/server.key\n\
require_certificate false\n\
allow_anonymous true\n\
\n\
# TLS with client authentication\n\
listener $MQTT_PORT_TLS_MUTUAL_AUTH\n\
cafile $CERTS_DIR/$MQTT_CA_CERT\n\
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
    echo "Mosquitto broker started on ports $MQTT_PORT_TLS_SERVER_ONLY (TLS) and $MQTT_PORT_TLS_MUTUAL_AUTH (mutual TLS)"\n\
else\n\
    echo "Failed to start Mosquitto broker. Log:" >&2\n\
    cat "$LOG_FILE" >&2\n\
    exit 1\n\
fi\n\
\n\
echo "Mosquitto broker is running!"\n\
' > /usr/local/bin/mosquitto_run.sh && chmod +x /usr/local/bin/mosquitto_run.sh

# Create httpbin certificate setup script.
# Generates a self-signed cert for CN=localhost (with SAN for localhost/127.0.0.1).
# The cert doubles as the TLS server cert AND the trust anchor: CI appends it to the
# consumer CA bundle, so TLS verification passes for $HTTP_URL_TLS.
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
    -keyout "$CERTS_DIR/$HTTP_SERVER_KEY" \\\n\
    -out "$CERTS_DIR/$HTTP_SERVER_CERT" \\\n\
    -days "$VALIDITY_DAYS" \\\n\
    -subj "/C=US/ST=Test/L=Test/O=Test/CN=localhost" \\\n\
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"\n\
\n\
chmod 644 "$CERTS_DIR/$HTTP_SERVER_KEY"\n\
echo "go-httpbin certificate generated:" && ls -la "$CERTS_DIR/$HTTP_SERVER_CERT" "$CERTS_DIR/$HTTP_SERVER_KEY"\n\
' > /usr/local/bin/httpbin_setup_conf.sh && chmod +x /usr/local/bin/httpbin_setup_conf.sh

# Create httpbin run script: starts plain HTTP and TLS listeners via gunicorn.
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
echo "Starting httpbin (HTTP :$HTTP_PORT_TCP, HTTPS :$HTTP_PORT_TLS)"\n\
echo "  Log: $LOG_FILE"\n\
echo "  PID: $PID_FILE"\n\
\n\
: > "$PID_FILE"\n\
"$GUNICORN" --chdir "$APP_DIR" -b 0.0.0.0:$HTTP_PORT_TCP hbapp:app >> "$LOG_FILE" 2>&1 &\n\
echo $! >> "$PID_FILE"\n\
"$GUNICORN" --chdir "$APP_DIR" -b 0.0.0.0:$HTTP_PORT_TLS \\\n\
    --certfile "$CERTS_DIR/$HTTP_SERVER_CERT" \\\n\
    --keyfile "$CERTS_DIR/$HTTP_SERVER_KEY" hbapp:app >> "$LOG_FILE" 2>&1 &\n\
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

# Expose MQTT TLS ports and httpbin ports
EXPOSE $MQTT_PORT_TLS_SERVER_ONLY $MQTT_PORT_TLS_MUTUAL_AUTH $HTTP_PORT_TCP $HTTP_PORT_TLS
