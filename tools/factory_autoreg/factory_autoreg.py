#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Generate node identities, register via the admin API, and emit factory NVS binaries.

Default stack config: tools/common/credentials_store/general/rmng-outputs.json
(--config overrides with a filesystem path or a client outputs URL).

Use -n N to create N nodes in one run (single sign-in). Artifacts go under
``<output_dir>/<StackAccountId>/<thing_type>/<thing_name>/`` where ``thing_type`` is
``rmng-only`` (default mode) or ``matter`` (``--matter``); StackAccountId comes from config.
With N > 1, ``batch_summary.json`` is written under the same ``<thing_type>`` directory.

Neither RainMaker-only nor Matter mode requires ``RMNG_BACKEND_DIR`` or the backend tree on ``PYTHONPATH``.

RainMaker-only mode uses ``util.node_crypto`` (cryptography) for key/cert generation. Matter mode (``--matter``)
uses esp-matter-mfg-tool + ``get_matter_idf_credentials``; the DAC subject CN is the thing name.

Dependencies: boto3, requests, cryptography, shortuuid; esp-idf-nvs-partition-gen; for --matter:
esp-matter-mfg-tool, ESP_MATTER_PATH, MATTER SDK test credentials.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import tempfile
import shortuuid
from pathlib import Path

import boto3
import requests
from botocore.auth import SigV4Auth
from botocore.awsrequest import AWSRequest
from botocore.exceptions import ClientError

# factory_nvs_gen lives next to this tool
_FACTORY_NVS_GEN = Path(__file__).resolve().parent.parent / "factory_nvs_gen"
if str(_FACTORY_NVS_GEN) not in sys.path:
    sys.path.insert(0, str(_FACTORY_NVS_GEN))

_COMMON_ROOT = Path(__file__).resolve().parents[1] / "common"
if str(_COMMON_ROOT) not in sys.path:
    sys.path.insert(0, str(_COMMON_ROOT))

from factory_nvs_gen import (  # noqa: E402
    InputError,
    get_idf_bin,
    get_matter_idf_credentials,
    get_posix_bin,
    _resolve_matter_test_credentials,
)

from util.node_crypto import generate_key_and_cert, split_combined_cert_pem  # noqa: E402

from credentials_store import build_rm_config, _is_url  # noqa: E402

_DEFAULT_RMNG_OUTPUTS = (
    _COMMON_ROOT / "credentials_store" / "general" / "rmng-outputs.json"
)

# Allowed capabilities
ALLOWED_CAPABILITIES = {"s3", "kvs", "bridge"}
ALLOWED_CAPABILITIES_STRING = ", ".join(sorted(ALLOWED_CAPABILITIES))


def _generate_random_id() -> str:
    return shortuuid.uuid()


def _thing_name_safe_for_path(cn: str) -> str:
    s = re.sub(r"[/\\:\x00-\x1f]+", "_", cn).strip()
    return s or f"matter-dac-{_generate_random_id()}"


# --- Copied from register_node.py (path not guaranteed in tree) -----------------


def load_config(config_source) -> dict:
    rm = build_rm_config(config_source)
    return {
        "api_gateway_url": (rm["ApiGatewayUrl"] or "").rstrip("/"),
        "region": rm["StackRegion"] or "us-east-1",
        "iot_endpoint_url": (rm["IoTEndpointUrl"] or "").strip(),
        "stack_account_id": rm["StackAccountId"],
        "admin_client_id": rm["AdminUserPoolClientId"] or "",
    }


def signin(
    region: str, admin_client_id: str, username: str, password: str
) -> tuple[str, str]:
    """Sign in as super admin against the admin Cognito pool. Returns (id_token, access_token).

    There is no platform admin-auth API: the backend removed /v1/admin/auth/* and admins
    now talk to their user pool directly (rmng-main src_esp_user/misc/specs/admin_auth.md).
    InitiateAuth is unauthenticated, so this needs no AWS credentials of its own.

    Both tokens are needed: /v1/user/credentials verifies them as a pair.
    """
    if not admin_client_id:
        print("ERROR: EspAdminUserPoolClientId missing from the espuser-base config")
        sys.exit(1)

    cognito = boto3.client("cognito-idp", region_name=region)
    try:
        resp = cognito.initiate_auth(
            ClientId=admin_client_id,
            AuthFlow="USER_PASSWORD_AUTH",
            AuthParameters={"USERNAME": username, "PASSWORD": password},
        )
    except ClientError as exc:
        # Every modelled Cognito exception subclasses ClientError, so catch the family
        # rather than naming members: an unlisted one (throttling, an unconfirmed
        # account, a password reset in progress) should not surface as a traceback.
        code = exc.response.get("Error", {}).get("Code", "Unknown")
        print(f"ERROR: Admin sign-in failed ({code}): {exc}")
        if code == "InvalidParameterException":
            print(
                "       Most likely the app client does not permit this flow. Enable "
                "ALLOW_USER_PASSWORD_AUTH on the admin user pool client."
            )
        elif code == "ResourceNotFoundException":
            print(
                f"       No such app client in {region}. Check EspAdminUserPoolClientId "
                "and StackRegion in the config."
            )
        sys.exit(1)

    # Deploy-time admins are created with a TemporaryPassword, which leaves the account in
    # FORCE_CHANGE_PASSWORD: Cognito answers with a challenge and no tokens at all.
    if "AuthenticationResult" not in resp:
        challenge = resp.get("ChallengeName", "unknown")
        print(
            f"ERROR: Admin sign-in returned the '{challenge}' challenge instead of tokens."
        )
        if challenge == "NEW_PASSWORD_REQUIRED":
            print(
                "       This account still has its bootstrap temporary password. Set a "
                "permanent one (admin console, or `aws cognito-idp "
                "admin-set-user-password --permanent`) and retry."
            )
        sys.exit(1)

    result = resp["AuthenticationResult"]
    id_token = result.get("IdToken")
    access_token = result.get("AccessToken")
    if not id_token or not access_token:
        print("ERROR: Cognito authentication result is missing IdToken or AccessToken.")
        sys.exit(1)

    print("Signed in successfully.")
    return id_token, access_token


def get_aws_credentials(api_url: str, id_token: str, access_token: str) -> dict:
    """Exchange the sign-in tokens for temporary AWS credentials.

    The access token goes in the header (the method declares API Gateway authorization
    scopes, so the authorizer validates that and rejects an ID token), while the Identity
    Pool exchange behind the endpoint takes an OIDC ID token and nothing else. The server
    verifies the two came from one sign-in, so both have to be sent, by different routes.
    """
    url = f"{api_url}/v1/user/credentials"
    headers = {
        "Authorization": f"Bearer {access_token}",
        "Content-Type": "application/json",
    }

    resp = requests.post(
        url, headers=headers, data=json.dumps({"id_token": id_token}), timeout=60
    )
    if resp.status_code != 200:
        print(f"ERROR: Failed to get AWS credentials ({resp.status_code}): {resp.text}")
        sys.exit(1)

    creds = resp.json()
    for field in ("access_key_id", "secret_access_key", "session_token"):
        if field not in creds:
            print(f"ERROR: Missing '{field}' in credentials response.")
            sys.exit(1)

    print("Obtained AWS credentials.")
    return creds


def register_node(
    api_url: str,
    region: str,
    creds: dict,
    cert_pem: str,
    ca_cert_pem: str | None = None,
    thing_groups: list[str] | None = None,
    tags: list[str] | None = None,
    capabilities: list[str] | None = None,
):
    url = f"{api_url}/v1/admin/nodes"

    body: dict = {"cert": cert_pem}
    if ca_cert_pem:
        body["ca_cert"] = ca_cert_pem
    if thing_groups:
        body["admin_group_names"] = thing_groups
    if tags:
        body["tags"] = tags
    if capabilities:
        body["capabilities"] = capabilities

    json_body = json.dumps(body)

    session = boto3.Session(
        aws_access_key_id=creds["access_key_id"],
        aws_secret_access_key=creds["secret_access_key"],
        aws_session_token=creds["session_token"],
        region_name=region,
    )

    aws_request = AWSRequest(method="POST", url=url, data=json_body)
    SigV4Auth(session.get_credentials(), "execute-api", region).add_auth(aws_request)
    prepared = aws_request.prepare()

    return requests.post(
        prepared.url,
        headers=dict(prepared.headers),
        data=json_body,
        timeout=120,
    )


# ---------------------------------------------------------------------------------

THING_TYPE_RMNG_ONLY = "rmng-only"
THING_TYPE_MATTER = "matter"


def _thing_out_dir(
    output_base: Path,
    stack_account_id: str,
    thing_type: str,
    thing_key: str,
) -> Path:
    """``<output_base>/<StackAccountId>/<thing_type>/<thing_key>/`` — thing_key is uuid or path-safe DAC CN."""
    return (output_base / stack_account_id / thing_type / thing_key).resolve()


def _generate_one_thing(
    *,
    thing_name: str,
    out_dir: Path,
    api_url: str,
    region: str,
    mqtt_host: str,
    aws_creds: dict,
    key_type: str,
    part_label: str,
    namespace: str,
    thing_groups: list[str] | None,
    tags: list[str] | None,
    capabilities: list[str] | None,
    idf_warned: list[bool],
) -> int:
    """Register one node and write factory artifacts under out_dir. Returns process exit code."""
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Thing name (CN): {thing_name}")
    print(f"Output directory: {out_dir}")

    node_key_pem, combined_cert_pem = generate_key_and_cert(
        thing_name, key_type=key_type
    )
    node_cert_pem, node_ca_pem = split_combined_cert_pem(combined_cert_pem)

    key_path = out_dir / "client.key"
    crt_path = out_dir / "client.crt"
    key_path.write_text(node_key_pem, encoding="utf-8")
    crt_path.write_text(combined_cert_pem.strip() + "\n", encoding="utf-8")
    print(f"  Wrote {key_path.name} and {crt_path.name} (combined cert chain).")

    print("  Registering node certificate...")
    resp = register_node(
        api_url=api_url,
        region=region,
        creds=aws_creds,
        cert_pem=node_cert_pem.strip(),
        ca_cert_pem=node_ca_pem.strip() if node_ca_pem else None,
        thing_groups=thing_groups,
        tags=tags,
        capabilities=capabilities,
    )

    if resp.status_code not in (200, 201):
        print(f"ERROR: Registration failed ({resp.status_code}): {resp.text}")
        return 3

    result = resp.json()
    node_id = result.get("node_id")
    if not node_id:
        print(f"ERROR: No node_id in response: {result}")
        return 3

    print(f"  Node registered. node_id={node_id} status={result.get('status')}")

    nvs_data = {
        "client_key": str(key_path),
        "client_cert": str(crt_path),
        "mqtt_host": mqtt_host,
        "node_id": node_id,
    }
    factory_json = out_dir / "factory_nvs_input.json"
    factory_json.write_text(json.dumps(nvs_data, indent=4) + "\n", encoding="utf-8")

    idf_dir = out_dir / "esp-idf"
    posix_dir = out_dir / "posix"
    idf_dir.mkdir(parents=True, exist_ok=True)
    posix_dir.mkdir(parents=True, exist_ok=True)

    idf_bin = idf_dir / f"{part_label}.bin"

    try:
        get_idf_bin(namespace, dict(nvs_data), out_dir, idf_bin)
    except InputError as exc:
        err = str(exc).lower()
        if "esp_idf_nvs_partition_gen" in err or "nvs_partition_gen" in err:
            if not idf_warned[0]:
                print(
                    "Warning: ESP-IDF factory .bin not generated; install esp-idf-nvs-partition-gen.",
                    file=sys.stderr,
                )
                idf_warned[0] = True
        else:
            print(f"ERROR: factory_nvs_gen (IDF): {exc}", file=sys.stderr)
            return 4

    try:
        get_posix_bin(part_label, namespace, dict(nvs_data), out_dir, posix_dir)
    except InputError as exc:
        print(f"ERROR: factory_nvs_gen (POSIX): {exc}", file=sys.stderr)
        return 4

    print("  Factory NVS:")
    if idf_bin.is_file():
        print(f"    ESP-IDF: {idf_bin}")
    else:
        print("    ESP-IDF: (not generated — install esp-idf-nvs-partition-gen)")
    posix_nvs = posix_dir / "nvs_persistent"
    if posix_nvs.is_dir():
        for p in sorted(posix_nvs.glob("*.bin")):
            print(f"    POSIX:   {p}")

    meta = {
        "thing_name": thing_name,
        "node_id": node_id,
        "output_dir": str(out_dir),
        "registration": result,
    }
    (out_dir / "registration.json").write_text(
        json.dumps(meta, indent=2) + "\n", encoding="utf-8"
    )

    return 0


def _parse_hex_int(s: str) -> int:
    return int(s, 0)


def _generate_one_matter(
    *,
    output_base: Path,
    stack_account_id: str,
    api_url: str,
    region: str,
    mqtt_host: str,
    aws_creds: dict,
    part_label: str,
    namespace: str,
    thing_groups: list[str] | None,
    tags: list[str] | None,
    capabilities: list[str] | None,
    vendor_id: int,
    product_id: int,
    codesign_cert: Path | None,
    idf_warned: list[bool],
) -> tuple[int, Path]:
    """Single esp-matter-mfg-tool run; DAC CN is thing name; ESP-IDF merged .bin only."""
    work = Path(tempfile.mkdtemp(prefix="factory_autoreg_matter_"))
    work_root = None
    try:
        esp_dir = work / "esp-idf"
        esp_dir.mkdir(parents=True)
        bin_path = esp_dir / f"{part_label}.bin"
        work_root = esp_dir / "matter_mfg_work"

        factory_data: dict = {"mqtt_host": mqtt_host}
        if codesign_cert is not None:
            factory_data["codesign_cert"] = str(codesign_cert.resolve())

        try:
            _, meta = get_matter_idf_credentials(
                namespace,
                factory_data,
                work,
                bin_path,
                vendor_id=vendor_id,
                product_id=product_id,
                work_root=work_root,
            )
        except InputError as exc:
            err = str(exc).lower()
            if "esp_idf_nvs_partition_gen" in err or "nvs_partition_gen" in err:
                if not idf_warned[0]:
                    print(
                        "Warning: ESP-IDF Matter factory .bin not generated; "
                        "install esp-idf-nvs-partition-gen.",
                        file=sys.stderr,
                    )
                    idf_warned[0] = True
                return 4, work
            print(f"ERROR: Matter factory: {exc}", file=sys.stderr)
            return 4, work

        thing_name = meta["thing_name"]
        dac_cert_pem = meta["dac_cert"]
        dac_key_pem = meta["dac_key"]
        qr_payload = meta["qr_payload"]

        thing_key = _thing_name_safe_for_path(thing_name)
        out_dir = _thing_out_dir(
            output_base, stack_account_id, THING_TYPE_MATTER, thing_key
        )

        _, pai_cert_path, _ = _resolve_matter_test_credentials(vendor_id, product_id)
        pai_pem = pai_cert_path.read_text(encoding="utf-8")

        out_dir.mkdir(parents=True, exist_ok=True)
        print(f"Thing name (DAC CN): {thing_name}")
        print(f"Output directory: {out_dir}")

        dest_esp = out_dir / "esp-idf"
        dest_esp.mkdir(parents=True, exist_ok=True)
        dest_bin = dest_esp / bin_path.name
        shutil.copy2(bin_path, dest_bin)

        (out_dir / "dac_key.pem").write_text(dac_key_pem, encoding="utf-8")
        (out_dir / "dac_cert.pem").write_text(dac_cert_pem, encoding="utf-8")
        qr_link = (
            "https://project-chip.github.io/connectedhomeip/qrcode.html?data="
            + qr_payload
        )
        (out_dir / "qr_link.txt").write_text(qr_link + "\n", encoding="utf-8")
        print("  Wrote dac_key.pem, dac_cert.pem, qr_link.txt")

        print("  Registering node (DAC + PAI CA)...")
        resp = register_node(
            api_url=api_url,
            region=region,
            creds=aws_creds,
            cert_pem=dac_cert_pem.strip(),
            ca_cert_pem=pai_pem.strip(),
            thing_groups=thing_groups,
            tags=tags,
            capabilities=capabilities,
        )

        if resp.status_code not in (200, 201):
            print(f"ERROR: Registration failed ({resp.status_code}): {resp.text}")
            return 3, out_dir

        result = resp.json()
        node_id = result.get("node_id")
        if not node_id:
            print(f"ERROR: No node_id in response: {result}")
            return 3, out_dir

        print(f"  Node registered. node_id={node_id} status={result.get('status')}")

        factory_payload = {
            "mqtt_host": mqtt_host,
            "node_id": thing_name,
            "dac_key": str(out_dir / "dac_key.pem"),
            "dac_cert": str(out_dir / "dac_cert.pem"),
            "matter_factory_bin": str(dest_bin),
            "qr_payload": qr_payload,
            "cloud_node_id": node_id,
        }
        if codesign_cert is not None:
            factory_payload["codesign_cert"] = str(codesign_cert.resolve())

        (out_dir / "factory_nvs_input.json").write_text(
            json.dumps(factory_payload, indent=4) + "\n", encoding="utf-8"
        )

        print("  Factory NVS (Matter + RainMaker merge):")
        if dest_bin.is_file():
            print(f"    ESP-IDF: {dest_bin}")
        print("    POSIX:   (skipped for --matter, same as factory_nvs_gen --matter)")

        reg_meta = {
            "thing_name": thing_name,
            "node_id": node_id,
            "output_dir": str(out_dir),
            "registration": result,
            "matter": True,
            "qr_link": qr_link,
        }
        (out_dir / "registration.json").write_text(
            json.dumps(reg_meta, indent=2) + "\n", encoding="utf-8"
        )

        return 0, out_dir
    finally:
        if work_root is not None:
            shutil.rmtree(work_root, ignore_errors=True)
        shutil.rmtree(work, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Auto-generate credentials, register a node, and build factory NVS (IDF + POSIX).",
    )
    parser.add_argument("username", help="Super admin username")
    parser.add_argument("password", help="Super admin password")
    parser.add_argument(
        "--matter",
        action="store_true",
        help=(
            "Matter factory: esp-matter-mfg-tool + merged chip/RainMaker NVS (ESP-IDF .bin only). "
            "Thing name is the DAC subject CN. Requires ESP_MATTER_PATH and esp-matter-mfg-tool."
        ),
    )
    parser.add_argument(
        "--vendor-id",
        type=_parse_hex_int,
        default=0xFFF2,
        help="Matter vendor id for mfg tool (default 0xFFF2)",
    )
    parser.add_argument(
        "--product-id",
        type=_parse_hex_int,
        default=0x8001,
        help="Matter product id for mfg tool (default 0x8001)",
    )
    parser.add_argument(
        "--codesign-cert",
        type=Path,
        default=None,
        help="Optional codesign certificate path for RainMaker factory namespace (Matter mode)",
    )
    parser.add_argument(
        "--config",
        default=str(_DEFAULT_RMNG_OUTPUTS),
        help=(
            "Path to rmng-outputs.json or client outputs URL"
            f"(default: {_DEFAULT_RMNG_OUTPUTS})"
        ),
    )
    parser.add_argument(
        "-n",
        "--count",
        type=int,
        default=1,
        metavar="N",
        help=(
            "Number of nodes to generate (default: 1). Each gets "
            "<output-dir>/<StackAccountId>/<rmng-only|matter>/<thing>/ when N > 1."
        ),
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help=(
            "Output root directory (default: tools/factory_autoreg/outputs). "
            "Each run writes to <output-dir>/<StackAccountId>/rmng-only|matter/<thing_name>/ "
            "(StackAccountId from rmng-base in config)."
        ),
    )
    parser.add_argument(
        "--key-type",
        choices=("ec", "rsa"),
        default="ec",
        help="Node key algorithm for non-Matter mode only (default: ec)",
    )
    parser.add_argument(
        "--part-label",
        default="fctry",
        help="Factory partition label (default: fctry)",
    )
    parser.add_argument(
        "--namespace",
        default="rmaker_creds",
        help="Factory NVS namespace (default: rmaker_creds)",
    )
    parser.add_argument(
        "--thing-groups",
        nargs="+",
        default=None,
        help="Optional IoT thing group names (admin_group_names)",
    )
    parser.add_argument(
        "--tags",
        nargs="+",
        default=None,
        help="Optional tags as key:value (e.g. env:prod)",
    )
    parser.add_argument(
        "--capabilities",
        nargs="+",
        default=None,
        help=f"Optional capabilities (accepted: {ALLOWED_CAPABILITIES_STRING})",
    )
    args = parser.parse_args()

    if args.count < 1:
        print("ERROR: -n / --count must be at least 1")
        return 2

    capabilities: list[str] | None = None
    if args.capabilities:
        capabilities = [c.strip() for c in args.capabilities if c.strip()]
        bad = [c for c in capabilities if c not in ALLOWED_CAPABILITIES]
        if bad:
            print(
                f"ERROR: invalid --capabilities: {bad}. Allowed: {ALLOWED_CAPABILITIES_STRING}"
            )
            return 2

    if _is_url(args.config):
        config_source = args.config
    else:
        config_source = Path(args.config).resolve()
        if not config_source.is_file():
            print(f"ERROR: Config file not found: {config_source}")
            return 2

    config = load_config(config_source)
    api_url = config["api_gateway_url"]
    region = config["region"]
    mqtt_host = config["iot_endpoint_url"]
    stack_account_id = config["stack_account_id"]
    admin_client_id = config["admin_client_id"]

    if not api_url:
        print("ERROR: ApiGatewayUrl required in config")
        return 2
    if not mqtt_host:
        print("ERROR: IoTEndpointUrl missing in rmng-base section of config")
        return 2
    if not stack_account_id:
        print("ERROR: StackAccountId missing in rmng-base section of config")
        return 2

    print("------------------ Stack config: ---------------------------")
    print(f"Config source:     {config_source}")
    print(f"Stack Account ID:  {stack_account_id}")
    print(f"Stack Region:      {region}")
    print(f"API Gateway URL:   {api_url}")
    print(f"IoT Endpoint URL:  {mqtt_host}")
    print("-----------------------------------------------------------\n")

    repo_tool_root = Path(__file__).resolve().parent
    if args.output_dir is not None:
        output_base = args.output_dir.resolve()
    else:
        output_base = (repo_tool_root / "outputs").resolve()

    thing_type = THING_TYPE_MATTER if args.matter else THING_TYPE_RMNG_ONLY

    print("Signing in (once for this batch)...")
    id_token, access_token = signin(
        region, admin_client_id, args.username, args.password
    )
    aws_creds = get_aws_credentials(api_url, id_token, access_token)

    idf_warned = [False]
    batch_summary: list[dict] = []

    for index in range(args.count):
        if args.matter:
            if args.count > 1:
                print(f"\n--- [{index + 1}/{args.count}] ---")

            rc, out_dir = _generate_one_matter(
                output_base=output_base,
                stack_account_id=stack_account_id,
                api_url=api_url,
                region=region,
                mqtt_host=mqtt_host,
                aws_creds=aws_creds,
                part_label=args.part_label,
                namespace=args.namespace,
                thing_groups=args.thing_groups,
                tags=args.tags,
                capabilities=capabilities,
                vendor_id=args.vendor_id,
                product_id=args.product_id,
                codesign_cert=args.codesign_cert,
                idf_warned=idf_warned,
            )
        else:
            thing_name = _generate_random_id()
            thing_key = thing_name
            out_dir = _thing_out_dir(
                output_base, stack_account_id, thing_type, thing_key
            )

            if args.count > 1:
                print(f"\n--- [{index + 1}/{args.count}] ---")

            rc = _generate_one_thing(
                thing_name=thing_name,
                out_dir=out_dir,
                api_url=api_url,
                region=region,
                mqtt_host=mqtt_host,
                aws_creds=aws_creds,
                key_type=args.key_type,
                part_label=args.part_label,
                namespace=args.namespace,
                thing_groups=args.thing_groups,
                tags=args.tags,
                capabilities=capabilities,
                idf_warned=idf_warned,
            )

        if rc != 0:
            return rc
        reg = json.loads((out_dir / "registration.json").read_text(encoding="utf-8"))
        entry: dict = {
            "thing_name": reg["thing_name"],
            "node_id": reg["node_id"],
            "output_dir": reg["output_dir"],
        }
        if reg.get("matter") and reg.get("qr_link"):
            entry["qr_link"] = reg["qr_link"]
        batch_summary.append(entry)

    if args.count > 1:
        batch_path = output_base / stack_account_id / thing_type / "batch_summary.json"
        batch_path.parent.mkdir(parents=True, exist_ok=True)
        batch_path.write_text(
            json.dumps(batch_summary, indent=2) + "\n", encoding="utf-8"
        )
        print(f"\nWrote batch summary: {batch_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
