# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
from urllib.parse import urlparse
from urllib.request import urlopen
import json

### --- General credentials ---

# Default merged stack-outputs file location
_rmng_outputs_file = "rmng-outputs.json"
_rmng_outputs_path = Path(__file__).resolve().parent / "general" / _rmng_outputs_file


# Hardcoded values (previously from test_config.json)
CA_CERT = """-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----"""
DEBUG = False


def _is_url(source) -> bool:
    """True if ``source`` looks like an http(s) URL rather than a filesystem path."""
    if not isinstance(source, str):
        return False
    return urlparse(source).scheme in ("http", "https")


def load_rmng_outputs(source) -> dict:
    """
    Load and parse a merged ``rmng-outputs.json`` from a filesystem path or a client outputs URL.

    ``source`` may be a ``str``/``Path`` filesystem path or a client outputs URL.
    """
    if _is_url(source):
        with urlopen(source, timeout=60) as resp:
            return json.load(resp)

    rmng_outputs_path = Path(source)
    if not rmng_outputs_path.exists():
        raise FileNotFoundError(f"RMNG outputs file not found at '{rmng_outputs_path}'")
    with open(rmng_outputs_path, "r") as f:
        return json.load(f)


def build_rm_config(rmng_outputs_source) -> dict:
    """
    Build an ``RM_CONFIG`` dict from a merged ``rmng-outputs.json``.

    ``rmng_outputs_source`` may be a filesystem path or a client outputs URL. Lets tools
    point at an alternate stack-outputs file (e.g. a staging export or an S3 URL)
    instead of the bundled ``general/rmng-outputs.json``.
    """
    rmng_outputs = load_rmng_outputs(rmng_outputs_source)

    # Extract ESP User stack outputs from merged file
    esp_user_base_outputs = rmng_outputs["espuser-base"]
    esp_user_core_outputs = rmng_outputs.get("espuser-core", {})

    # User pools/clients come from the ESP User stack; node/IoT infra from rmng-base
    config = {
        "StackRegion": rmng_outputs["rmng-base"]["StackRegion"],
        "UserPoolId": esp_user_base_outputs["EspEndUserPoolId"],
        "UserPoolClientId": esp_user_base_outputs["EspUserClientId"],
        "AdminUserPoolId": esp_user_base_outputs["EspAdminUserPoolId"],
        "AdminUserPoolClientId": esp_user_base_outputs["EspAdminUserPoolClientId"],
        "IdentityPoolId": rmng_outputs["rmng-base"]["IdentityPoolId"],
        "StackAccountId": str(rmng_outputs["rmng-base"]["StackAccountId"]).strip(),
        "ApiGatewayUrl": rmng_outputs["rmng-base"]["ApiGatewayUrl"],
        "UserApiGatewayUrl": esp_user_base_outputs["EspUserApiUrl"],
        "RegisterUserLambdaArn": esp_user_core_outputs.get("UserFunctionArn"),
        "IoTEndpointUrl": rmng_outputs["rmng-base"]["IoTEndpointUrl"],
        "IoTUserRoleArn": rmng_outputs["rmng-base"]["IoTUserRoleArn"],
        "CACert": CA_CERT,
        "Debug": DEBUG,
    }

    # Collect Alexa skill function ARNs from alexa-stack-<REGION> outputs
    alexa_stack_outputs = rmng_outputs.get(
        f"rmng-alexa-core-{config['StackRegion']}", {}
    )
    config["AlexaSkillFunctionArns"] = {
        region: stack_outputs.get("AlexaSkillFunctionArn")
        for region, stack_outputs in alexa_stack_outputs.get("regions", {}).items()
    }

    # Collect SmartThings Schema App function ARNs from rmng-st-core-<REGION> outputs
    st_stack_outputs = rmng_outputs.get(f"rmng-st-core-{config['StackRegion']}", {})
    config["STSchemaAppFunctionArns"] = {
        region: stack_outputs.get("STSchemaAppFunctionArn")
        for region, stack_outputs in st_stack_outputs.get("regions", {}).items()
        if stack_outputs.get("STSchemaAppFunctionArn")
    }
    return config


# Default config from the bundled stack-outputs file, built lazily on first
# access so importing this module never requires _rmng_outputs_path to
# exist. Tools that only import build_rm_config()/_is_url() (e.g. factory_autoreg
# with a --config URL) work without the bundled file present.
_RM_CONFIG_CACHE = None


def __getattr__(name):
    if name == "RM_CONFIG":
        global _RM_CONFIG_CACHE
        if _RM_CONFIG_CACHE is None:
            _RM_CONFIG_CACHE = build_rm_config(_rmng_outputs_path)
        return _RM_CONFIG_CACHE
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


### --- OTA credentials ---

# Set the directory for the OTA credentials
OTA_CREDENTIALS_DIR = Path(__file__).resolve().parent / "ota"
