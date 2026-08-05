# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Discovery of the in-cloud notifications webhook mock.

The mock stands in for the real Alexa / Google endpoints the notifications Lambda
POSTs to. It is deployed by ``make itest-setup`` in the backend repo (app_test.py
-> rmng-test-infra-base), which exposes the API Gateway URL and the ARN of the
secret holding that gateway's API key as stack outputs.

Those outputs are read from CloudFormation rather than from the
``cdk-outputs-test.json`` that the deploy writes: that file only exists on the
machine that ran the deploy, which is not the machine running these tests.
"""

import boto3
import requests
from botocore.exceptions import ClientError

from credentials_store import RM_CONFIG

TEST_INFRA_STACK = "rmng-test-infra-base"

# (base_url, api_key) once resolved; False once known-absent, so a missing stack is
# not re-queried on every call.
_infra = None


def webhook_mock_infra():
    """Return ``(base_url, api_key)`` for the webhook mock, or None when the test
    infra is not deployed. Cached per process, so every xdist worker resolves it once.
    """
    global _infra
    if _infra is not None:
        return _infra or None

    region = RM_CONFIG["StackRegion"]
    try:
        stacks = boto3.client("cloudformation", region_name=region).describe_stacks(
            StackName=TEST_INFRA_STACK
        )["Stacks"]
    except ClientError as e:
        # An absent stack comes back as ValidationError, and is the "not deployed"
        # signal. Anything else (denied, throttled) is a real fault and must not be
        # reported as "not deployed".
        if e.response.get("Error", {}).get("Code") == "ValidationError":
            _infra = False
            return None
        raise

    outputs = {o["OutputKey"]: o["OutputValue"] for o in stacks[0].get("Outputs", [])}
    base_url = outputs.get("ApiGatewayUrl")
    secret_arn = outputs.get("MockApiKeySecretArn")
    if not base_url or not secret_arn:
        _infra = False
        return None

    # The key is exported as a secret ARN, not a value: CfnOutput cannot resolve a
    # secretsmanager dynamic reference, and it keeps the raw key out of the stack
    # outputs.
    api_key = boto3.client("secretsmanager", region_name=region).get_secret_value(
        SecretId=secret_arn
    )["SecretString"]
    _infra = (base_url.rstrip("/"), api_key)
    return _infra


def webhook_mock_validate(channel: str, uuid: str, timeout: int = 10):
    """GET the payload the mock last captured for ``uuid`` on ``channel``.

    ``channel`` is "alexa" or "gva". Every /v1 method on the mock gateway is API-key
    gated, so the key travels on the readback too — not just on the Lambda's delivery.
    Returns the raw response; 410 means the mock holds nothing for this uuid.
    """
    base_url, api_key = webhook_mock_infra()
    return requests.get(
        f"{base_url}/v1/{channel}/validate",
        params={"uuid": uuid},
        headers={"x-api-key": api_key},
        timeout=timeout,
    )
