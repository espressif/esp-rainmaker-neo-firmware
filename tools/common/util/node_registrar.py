# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

# ruff: noqa: E402 -- sys.path prelude must run before the imports below

"""
Superadmin identity for direct invokes of the admin node-registration Lambda.

rmng-admin-node-reg resolves its caller from the API-Gateway
CognitoAuthenticationProvider string and enforces a superadmin gate, so an
identity-less direct invoke is answered 401. Ported from the backend itest suite
(rmng-main test/itest/conftest.py node_registrar_identity), which cannot be
imported here: that module reads rmng-outputs.json relative to the CWD and
hard-indexes stack outputs this repo's credentials_store does not carry.
"""

import sys
import tempfile
from pathlib import Path

import boto3
from filelock import FileLock

_TOOLS_COMMON = str(Path(__file__).resolve().parents[1])
if _TOOLS_COMMON not in sys.path:
    sys.path.insert(0, _TOOLS_COMMON)
from credentials_store import RM_CONFIG
from rmng_backend import User

# Pinned so nodes registered by earlier runs keep resolving to the same caller.
_EMAIL = "node-registrar-sdk@example.com"
_USER_ID = "node-registrar-sdk"

# Provisioning is serialized across processes: every xdist worker imports this module
# separately, so they would otherwise race to create and stamp the same account and the
# losers see a Cognito throttle, which create_super_admin_via_cognito reports only as None.
_LOCK_PATH = Path(tempfile.gettempdir()) / "rm-neo-node-registrar-sdk.lock"
_LOCK_TIMEOUT_SEC = 180

_provider = None


def _stamped_username(cognito, user_pool_id):
    """Cognito Username of the registrar account, or None if absent or not yet stamped as a
    superadmin. Both halves matter: the account is created first and stamped in a second
    call, and one seen between the two is not usable as a caller yet."""
    try:
        account = cognito.admin_get_user(UserPoolId=user_pool_id, Username=_EMAIL)
    except cognito.exceptions.UserNotFoundException:
        return None
    attributes = {a["Name"]: a["Value"] for a in account.get("UserAttributes", [])}
    if (
        attributes.get("custom:super_admin") != "true"
        or attributes.get("custom:user_id") != _USER_ID
    ):
        return None
    return account["Username"]


def node_registrar_identity() -> str:
    """CognitoAuthenticationProvider string of a real superadmin, for direct-invoke node
    registration. Provisions the fixed registrar account on first use — idempotent across
    runs and across xdist workers — and caches the string."""
    global _provider
    if _provider:
        return _provider

    region = RM_CONFIG["StackRegion"]
    user_pool_id = RM_CONFIG["AdminUserPoolId"]
    cognito = boto3.client("cognito-idp", region_name=region)

    with FileLock(str(_LOCK_PATH), timeout=_LOCK_TIMEOUT_SEC):
        username = _stamped_username(cognito, user_pool_id)
        if username is None:
            # password=False: this identity is only ever a provider string for a direct
            # invoke, so it must not have a password that could be used to sign in.
            User(
                username=_EMAIL,
                password="",
                region=region,
                identity_pool_id=RM_CONFIG["IdentityPoolId"],
                api_gateway_url=RM_CONFIG["ApiGatewayUrl"],
                user_api_gateway_url=RM_CONFIG["UserApiGatewayUrl"],
                iot_endpoint=RM_CONFIG["IoTEndpointUrl"],
                admin_user_pool_id=user_pool_id,
                admin_client_id=RM_CONFIG["AdminUserPoolClientId"],
            ).create_super_admin_via_cognito(
                email=_EMAIL, password=False, user_id=_USER_ID
            )
            # Trust the pool, not the return value: provisioning reports failure as None
            # after logging the Cognito error, and a run on another host may have finished
            # the account even when our own calls were throttled.
            username = _stamped_username(cognito, user_pool_id)
            if username is None:
                raise RuntimeError(
                    f"Failed to provision the node-registrar superadmin {_EMAIL} in pool "
                    f"{user_pool_id}; see the [User] log line above for the Cognito error"
                )

    pool = f"cognito-idp.{region}.amazonaws.com/{user_pool_id}"
    _provider = f"{pool},{pool}:CognitoSignIn:{username}"
    return _provider
