# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import csv
import json
import os
import sys
import tempfile
import secrets
from pathlib import Path
from typing import Dict, Any, Tuple, List, Optional
import struct
import re
import shutil

try:
    import esp_idf_nvs_partition_gen.nvs_partition_gen as nvs_partition_gen
except Exception:  # pragma: no cover - dependency may not be present in POSIX runs
    nvs_partition_gen = None  # type: ignore

# Get the components directory
components_dir = Path(__file__).resolve().parents[2] / "components"


# Length of the general-purpose "random" blob, matching CLAIM_RANDOM_NUMBER_LEN in
# components/esp_rmaker_neo/src/claim/claim.c. Both producers must agree: a consumer reading more than
# the shorter one provides would work on a claimed node and fail on a pre-flashed one. Only
# the first 4 bytes (Proof of Possession) and the last 3 (BLE device name) are used today.
RANDOM_LEN = 64


def _get_out_dir() -> Path:
    return Path.cwd() / "out"


def _parse_factory_constants() -> Dict[str, str]:
    """Parse factory key names from nvs.h using regex.

    Returns keys_by_symbol,
    maps symbols like 'CLIENT_KEY' to their string value like 'client_key'.
    """
    # Get the constants header
    header_path = (
        components_dir / "esp_rmaker_neo_common/priv_include/credentials/factory_part.h"
    )
    try:
        contents = header_path.read_text(encoding="utf-8", errors="ignore")
    except Exception as exc:
        raise InputError(f"Failed to read constants header: {header_path}: {exc}")

    keys_by_symbol: Dict[str, str] = {}
    for m in re.finditer(
        r"^\s*#define\s+RMAKER_NVS_FACTORY_KEY_([A-Z0-9_]+)\s+\"([^\"]+)\"",
        contents,
        re.MULTILINE,
    ):
        symbol = m.group(1)  # e.g., CLIENT_KEY
        value = m.group(2)  # e.g., client_key
        keys_by_symbol[symbol] = value

    required_symbols = [
        "CLIENT_KEY",
        "CLIENT_CERT",
        "MQTT_HOST",
        "CLIENT_ID",
        "RANDOM",
        "CODESIGN_CERT",
    ]
    for sym in required_symbols:
        if sym not in keys_by_symbol:
            raise InputError(
                f"Missing required factory key symbol in header: RMAKER_NVS_FACTORY_KEY_{sym}"
            )

    return keys_by_symbol


class InputError(Exception):
    pass


def _read_text_file(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except Exception as exc:
        raise InputError(f"Failed to read text file: {path}: {exc}")


def _read_binary_file(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except Exception as exc:
        raise InputError(f"Failed to read binary file: {path}: {exc}")


def _validate_and_normalize_input(
    obj: Dict[str, Any], base_dir: Path, keys: Dict[str, str]
) -> Dict[str, Any]:
    required_keys = [
        keys["CLIENT_KEY"],
        keys["CLIENT_CERT"],
        keys["MQTT_HOST"],
        keys["CLIENT_ID"],
    ]
    missing = [k for k in required_keys if k not in obj]
    if missing:
        raise InputError(f"Missing required keys in input JSON: {', '.join(missing)}")

    # Warn for codesign cert if it is not provided
    if keys["CODESIGN_CERT"] not in obj:
        print(
            "Warning: CODESIGN_CERT is not provided. This will cause the device to skip OTA firmware signature verification."
        )

    obj[keys["RANDOM"]] = secrets.token_bytes(RANDOM_LEN)

    # Normalize file paths to absolute
    def to_abs_path(value: Any) -> Path:
        p = Path(str(value))
        if not p.is_absolute():
            p = (base_dir / p).resolve()
        return p

    filepath_keys = [
        keys["CLIENT_KEY"],
        keys["CLIENT_CERT"],
        keys["CODESIGN_CERT"],
    ]
    for p in filepath_keys:
        if p not in obj:
            continue
        obj[p] = to_abs_path(obj[p])
        if not obj[p].is_file():
            raise InputError(f"File does not exist: {p}")

    # Ensure simple types for other fields
    optional_syms = ["CLIENT_USERNAME", "CLIENT_PASSWORD"]
    for k in [
        keys["MQTT_HOST"],
        keys["CLIENT_ID"],
        *(keys[s] for s in optional_syms if s in keys),
    ]:
        if k in obj and obj[k] is not None:
            obj[k] = str(obj[k])

    return obj


def _validate_matter_rmng_input(
    obj: Dict[str, Any], base_dir: Path, keys: Dict[str, str]
) -> Dict[str, Any]:
    """RainMaker factory rows merged with Matter: only mqtt_host is required in JSON."""
    mh = keys["MQTT_HOST"]
    if mh not in obj:
        raise InputError(f"Missing required keys in input JSON: {mh}")

    if keys["CODESIGN_CERT"] not in obj:
        print(
            "Warning: CODESIGN_CERT is not provided. This will cause the device to skip OTA firmware signature verification."
        )

    obj[keys["RANDOM"]] = secrets.token_bytes(RANDOM_LEN)

    def to_abs_path(value: Any) -> Path:
        p = Path(str(value))
        if not p.is_absolute():
            p = (base_dir / p).resolve()
        return p

    if keys["CODESIGN_CERT"] in obj and obj[keys["CODESIGN_CERT"]]:
        obj[keys["CODESIGN_CERT"]] = to_abs_path(obj[keys["CODESIGN_CERT"]])
        if not obj[keys["CODESIGN_CERT"]].is_file():
            raise InputError(f"File does not exist: {keys['CODESIGN_CERT']}")

    obj[mh] = str(obj[mh])
    optional_syms = ["CLIENT_USERNAME", "CLIENT_PASSWORD"]
    for k in [*(keys[s] for s in optional_syms if s in keys)]:
        if k in obj and obj[k] is not None:
            obj[k] = str(obj[k])

    return obj


def _matter_mfg_tool_main():
    try:
        from sources.cli import main as matter_mfg_main
    except ImportError as exc:
        raise InputError(
            "The esp-matter-mfg-tool package is required. Install with: "
            "python3 -m pip install esp-matter-mfg-tool"
        ) from exc
    return matter_mfg_main


def _run_matter_mfg_tool(argv: List[str]) -> None:
    main_fn = _matter_mfg_tool_main()
    # esp-matter-mfg-tool keeps module-level UUIDs / SECURE_CERT_INFO; setup_out_dirs only
    # appends. A second in-process run (e.g. factory_autoreg -n > 1) leaves UUIDs[0] as the
    # previous device while OUT_DIR points at a new outdir — internal/ is never created for
    # that stale UUID and DAC generation fails with FileNotFoundError.
    import sources.mfg_tool as _mfg_tool

    _mfg_tool.UUIDs.clear()
    _mfg_tool.SECURE_CERT_INFO.clear()
    saved = sys.argv
    try:
        sys.argv = argv
        main_fn(standalone_mode=False)
    finally:
        sys.argv = saved


def _dac_pem_common_name(pem_path: Path) -> str:
    try:
        from cryptography import x509
        from cryptography.x509.oid import NameOID
    except ImportError as exc:
        raise InputError(
            "The cryptography package is required for Matter factory generation. "
            "Install with: python3 -m pip install cryptography"
        ) from exc

    cert = x509.load_pem_x509_certificate(pem_path.read_bytes())
    cns = cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
    if not cns:
        raise InputError(f"No subject CN in DAC certificate: {pem_path}")
    return cns[0].value


def _read_qr_payload_from_codes_csv(codes_csv: Path) -> str:
    with codes_csv.open(encoding="utf-8") as f:
        qrcode_i = -1
        for line in f:
            cols = line.strip().split(",")
            if "qrcode" in cols:
                qrcode_i = cols.index("qrcode")
            elif qrcode_i != -1:
                return cols[qrcode_i]
    raise InputError(f"No QR payload found in {codes_csv}")


def _read_matter_partition_csv_rows(
    matter_partition_csv: Path,
) -> List[Tuple[str, str, str, str]]:
    matter_rows: List[Tuple[str, str, str, str]] = []
    with matter_partition_csv.open(newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        try:
            header = next(reader)
        except StopIteration as exc:
            raise InputError(
                f"Empty Matter partition.csv: {matter_partition_csv}"
            ) from exc
        if len(header) < 4 or [h.strip() for h in header[:4]] != [
            "key",
            "type",
            "encoding",
            "value",
        ]:
            raise InputError(
                f"Unexpected Matter partition.csv header in {matter_partition_csv}: {header}"
            )
        for row in reader:
            if not row or all(not c.strip() for c in row):
                continue
            while len(row) < 4:
                row.append("")
            matter_rows.append((row[0], row[1], row[2], row[3]))
    return matter_rows


def _resolve_matter_test_credentials(
    vendor_id: int, product_id: int
) -> Tuple[Path, Path, Path]:
    esp_matter = os.environ.get("ESP_MATTER_PATH")
    if not esp_matter:
        raise InputError(
            "ESP_MATTER_PATH must be set for Matter factory NVS generation"
        )

    matter_sdk = Path(
        os.environ.get(
            "MATTER_SDK_PATH", Path(esp_matter) / "connectedhomeip" / "connectedhomeip"
        )
    )
    if not matter_sdk.is_dir():
        raise InputError(f"Matter SDK path is not a directory: {matter_sdk}")

    creds = matter_sdk / "credentials" / "test"
    vid4 = vendor_id & 0xFFFF
    pid4 = product_id & 0xFFFF
    pai_stem = f"Chip-Test-PAI-{vid4:04X}-{pid4:04X}"
    pai_key = creds / "attestation" / f"{pai_stem}-Key.pem"
    pai_cert = creds / "attestation" / f"{pai_stem}-Cert.pem"
    cd_der = (
        creds / "certification-declaration" / f"Chip-Test-CD-{vid4:04X}-{pid4:04X}.der"
    )
    for p in (pai_key, pai_cert, cd_der):
        if not p.is_file():
            raise InputError(f"Expected Matter test credential file missing: {p}")
    return pai_key, pai_cert, cd_der


def get_matter_idf_credentials(
    ns: str,
    data: Dict[str, Any],
    base_dir: Path,
    out_path: Path,
    *,
    vendor_id: int = 0xFFF2,
    product_id: int = 0x8001,
    work_root: Optional[Path] = None,
) -> Tuple[Path, Dict[str, str]]:
    """
    Run ``esp-matter-mfg-tool``, merge chip-factory NVS rows with RainMaker factory namespace,
    and emit a single factory partition binary.

    ``data`` is validated for Matter merge: only ``mqtt_host`` is required (no client key/cert in JSON).
    DAC PEM paths are taken from the manufacturing tool output and written as RainMaker ``client_key`` /
    ``client_cert`` NVS entries; ``node_id`` is set to the DAC subject CN.

    Returns ``(out_path, info)`` where ``info`` has keys ``qr_payload``, ``dac_key``, ``dac_cert``,
    ``thing_name`` (all strings).
    """
    keys = _parse_factory_constants()
    rmng_base = _validate_matter_rmng_input(dict(data), base_dir, keys)

    pai_key, pai_cert, cd_der = _resolve_matter_test_credentials(vendor_id, product_id)
    mfg_root = (
        work_root if work_root is not None else out_path.parent / "matter_mfg_work"
    )
    mfg_root.mkdir(parents=True, exist_ok=True)
    mfg_out = mfg_root / "mfg_out"
    mfg_out.mkdir(parents=True, exist_ok=True)

    argv = [
        "esp-matter-mfg-tool",
        "-n",
        "1",
        "-v",
        hex(vendor_id),
        "-p",
        hex(product_id),
        "--vendor-name",
        "RMNG",
        "--product-name",
        "matter-sim",
        "--hw-ver",
        "1",
        "--hw-ver-str",
        "1",
        "--pai",
        "-k",
        str(pai_key),
        "-c",
        str(pai_cert),
        "-cd",
        str(cd_der),
        "--outdir",
        str(mfg_out),
    ]
    _run_matter_mfg_tool(argv)

    vid_pid = f"{vendor_id:x}_{product_id:x}"
    out_base = mfg_out / vid_pid
    if not out_base.is_dir():
        raise InputError(f"Matter mfg output directory not found: {out_base}")

    uuids = [d for d in out_base.iterdir() if d.is_dir() and d.name != "staging"]
    if len(uuids) != 1:
        raise InputError(
            f"Expected exactly one device UUID directory under {out_base}, found: {[d.name for d in uuids]}"
        )
    device_dir = uuids[0]
    part_csv = device_dir / "internal" / "partition.csv"
    dac_key_path = device_dir / "internal" / "DAC_key.pem"
    dac_pem_path = device_dir / "internal" / "DAC_cert.pem"
    codes_csv = device_dir / f"{device_dir.name}-onb_codes.csv"
    if not part_csv.is_file():
        raise InputError(f"Missing Matter partition.csv at {part_csv}")
    if not dac_key_path.is_file():
        raise InputError(f"Missing DAC key at {dac_key_path}")
    if not dac_pem_path.is_file():
        raise InputError(f"Missing DAC PEM at {dac_pem_path}")
    if not codes_csv.is_file():
        raise InputError(f"Missing codes CSV at {codes_csv}")

    thing_name = _dac_pem_common_name(dac_pem_path)
    qr_payload = _read_qr_payload_from_codes_csv(codes_csv)
    dac_key_pem = dac_key_path.read_text(encoding="utf-8")
    dac_cert_pem = dac_pem_path.read_text(encoding="utf-8")

    rmng_inp = dict(rmng_base)
    # RainMaker client_id matches DAC subject CN (the device identity).
    rmng_inp[keys["CLIENT_ID"]] = thing_name

    matter_rows = _read_matter_partition_csv_rows(part_csv)
    rmng_rows = _build_idf_csv_rows(rmng_inp, ns, keys)
    combined = matter_rows + rmng_rows
    out_path.parent.mkdir(parents=True, exist_ok=True)
    _generate_idf_binary(combined, out_path)

    meta: Dict[str, str] = {
        "qr_payload": qr_payload,
        "dac_key": dac_key_pem,
        "dac_cert": dac_cert_pem,
        "thing_name": thing_name,
    }
    return out_path, meta


def _build_idf_csv_rows(
    inp: Dict[str, Any], ns: str, keys: Dict[str, str]
) -> List[Tuple[str, str, str, str]]:
    rows: List[Tuple[str, str, str, str]] = []
    # Header
    # First namespace row
    rows.append((ns, "namespace", "", ""))

    # PEMs as file/string so IDF NVS stores as string entries
    if keys["CLIENT_KEY"] in inp and inp[keys["CLIENT_KEY"]]:
        rows.append(
            (keys["CLIENT_KEY"], "file", "binary", str(inp[keys["CLIENT_KEY"]]))
        )
    if keys["CLIENT_CERT"] in inp and inp[keys["CLIENT_CERT"]]:
        rows.append(
            (keys["CLIENT_CERT"], "file", "binary", str(inp[keys["CLIENT_CERT"]]))
        )

    # Data fields
    if keys["MQTT_HOST"] in inp and inp[keys["MQTT_HOST"]]:
        rows.append((keys["MQTT_HOST"], "data", "binary", inp[keys["MQTT_HOST"]]))
    if keys["CLIENT_ID"] in inp and inp[keys["CLIENT_ID"]]:
        rows.append((keys["CLIENT_ID"], "data", "binary", inp[keys["CLIENT_ID"]]))

    # Random bytes as hex string
    if keys["RANDOM"] in inp and inp[keys["RANDOM"]]:
        rows.append((keys["RANDOM"], "data", "hex2bin", inp[keys["RANDOM"]].hex()))

    if (
        "CLIENT_USERNAME" in keys
        and keys["CLIENT_USERNAME"] in inp
        and inp[keys["CLIENT_USERNAME"]]
    ):
        rows.append(
            (keys["CLIENT_USERNAME"], "data", "binary", inp[keys["CLIENT_USERNAME"]])
        )
    if (
        "CLIENT_PASSWORD" in keys
        and keys["CLIENT_PASSWORD"] in inp
        and inp[keys["CLIENT_PASSWORD"]]
    ):
        rows.append(
            (keys["CLIENT_PASSWORD"], "data", "binary", inp[keys["CLIENT_PASSWORD"]])
        )
    if keys["CODESIGN_CERT"] in inp and inp[keys["CODESIGN_CERT"]]:
        rows.append(
            (keys["CODESIGN_CERT"], "file", "binary", str(inp[keys["CODESIGN_CERT"]]))
        )
    return rows


def _write_csv(path: Path, rows: List[Tuple[str, str, str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        f.write("key,type,encoding,value\n")
        for key, typ, enc, val in rows:
            # Ensure no commas inside fields; for file paths it's fine
            f.write(f"{key},{typ},{enc},{val}\n")


def _generate_idf_binary(
    csv_rows: List[Tuple[str, str, str, str]], out_bin: Path
) -> None:
    if nvs_partition_gen is None:
        raise InputError(
            "esp_idf_nvs_partition_gen is not installed. Install with: pip install esp-idf-nvs-partition-gen"
        )

    out_bin.parent.mkdir(parents=True, exist_ok=True)

    # Write CSV to a temp file in the same directory (to keep relative paths stable if any)
    with tempfile.TemporaryDirectory() as tmpdir:
        csv_path = Path(tmpdir) / "factory.csv"
        _write_csv(csv_path, csv_rows)

        # Auto-size the partition: start small and grow until it fits
        size = 0x3000
        size_inc = 0x1000
        max_size = 0x10000  # 64KB upper bound guard
        last_err: Exception | None = None
        while size <= max_size:
            try:
                # Build an argparse-like args object as expected by the library
                size_str = f"0x{size:x}"  # library does int(args.size, 0)
                args = argparse.Namespace(
                    input=str(csv_path),
                    output=os.path.basename(str(out_bin)),
                    size=size_str,
                    version=2,
                    outdir=str(out_bin.parent),
                )
                nvs_partition_gen.generate(args)

                print(f"->  Final size: 0x{size:x}")
                return
            except Exception as exc:  # pragma: no cover
                last_err = exc
                # Retry only if this looks like a size/fit error
                msg = str(exc).lower()
                if not ("fit" in msg or "too small" in msg or "does not fit" in msg):
                    raise InputError(f"Failed to generate IDF NVS binary: {exc}")
                size += size_inc
        raise InputError(f"Failed to generate IDF NVS binary: {last_err}")


def _parse_osal_storage_types() -> Dict[str, int]:
    """Parse NVS common type enum values from osal_storage.h using regex.

    Returns a dictionary mapping string type names to their enum values.
    """
    header_path = components_dir / "osal/storage/include/osal_storage.h"

    try:
        contents = header_path.read_text(encoding="utf-8", errors="ignore")
    except Exception as exc:
        raise InputError(f"Failed to read osal_storage.h header: {header_path}: {exc}")

    # Map string type names to their expected enum names
    type_name_to_enum = {
        "binary": "OSAL_STORAGE_TYPE_BINARY",
        "u16": "OSAL_STORAGE_TYPE_U16",
        "i32": "OSAL_STORAGE_TYPE_I32",
    }

    result: Dict[str, int] = {}

    for type_name, enum_name in type_name_to_enum.items():
        # Look for pattern: OSAL_STORAGE_TYPE_BINARY = 0,
        pattern = rf"{re.escape(enum_name)}\s*=\s*(\d+)"
        m = re.search(pattern, contents)
        if not m:
            raise InputError(
                f"Failed to find enum value for {enum_name} in {header_path}"
            )
        result[type_name] = int(m.group(1))

    return result


def _encode_posix_record(key: str, value_bytes: bytes, data_type: str) -> bytes:
    key_bytes = key.encode("utf-8")

    # Get type enum values dynamically from header file
    type_map = _parse_osal_storage_types()

    if data_type not in type_map:
        raise InputError(f"Unknown data type: {data_type}")

    type_byte = type_map[data_type]
    return (
        struct.pack("<HIB", len(key_bytes), len(value_bytes), type_byte)
        + key_bytes
        + value_bytes
    )


def _generate_posix_binary(
    inp: Dict[str, Any], out_dir: Path, part_label: str, ns: str, keys: Dict[str, str]
) -> Path:
    # Path format mirrors nvs_posix_impl.c: nvs_persistent/<partition>-<namespace>.bin
    base_dir = out_dir / "nvs_persistent"
    base_dir.mkdir(parents=True, exist_ok=True)
    out_path = base_dir / f"{part_label}-{ns}.bin"

    records: List[bytes] = []

    # Order mirrors the CSV order
    # Files as raw bytes (PEM content) - type: binary
    records.append(
        _encode_posix_record(
            keys["CLIENT_KEY"],
            _read_binary_file(Path(inp[keys["CLIENT_KEY"]])),
            "binary",
        )
    )
    records.append(
        _encode_posix_record(
            keys["CLIENT_CERT"],
            _read_binary_file(Path(inp[keys["CLIENT_CERT"]])),
            "binary",
        )
    )
    if keys["CODESIGN_CERT"] in inp and inp[keys["CODESIGN_CERT"]]:
        records.append(
            _encode_posix_record(
                keys["CODESIGN_CERT"],
                _read_binary_file(Path(inp[keys["CODESIGN_CERT"]])),
                "binary",
            )
        )

    # Data fields as bytes - type: binary
    records.append(
        _encode_posix_record(
            keys["MQTT_HOST"], inp[keys["MQTT_HOST"]].encode("utf-8"), "binary"
        )
    )
    records.append(
        _encode_posix_record(
            keys["CLIENT_ID"], inp[keys["CLIENT_ID"]].encode("utf-8"), "binary"
        )
    )

    # Random bytes as raw binary data
    records.append(_encode_posix_record(keys["RANDOM"], inp[keys["RANDOM"]], "binary"))

    if (
        "CLIENT_USERNAME" in keys
        and keys["CLIENT_USERNAME"] in inp
        and inp[keys["CLIENT_USERNAME"]]
    ):
        records.append(
            _encode_posix_record(
                keys["CLIENT_USERNAME"],
                inp[keys["CLIENT_USERNAME"]].encode("utf-8"),
                "binary",
            )
        )
    if (
        "CLIENT_PASSWORD" in keys
        and keys["CLIENT_PASSWORD"] in inp
        and inp[keys["CLIENT_PASSWORD"]]
    ):
        records.append(
            _encode_posix_record(
                keys["CLIENT_PASSWORD"],
                inp[keys["CLIENT_PASSWORD"]].encode("utf-8"),
                "binary",
            )
        )

    out_path.write_bytes(b"".join(records))
    return out_path


def get_idf_bin(ns: str, data: dict[str, Any], base_dir: Path, out_path: Path) -> Path:
    keys = _parse_factory_constants()
    data = _validate_and_normalize_input(data, base_dir, keys)
    csv_rows = _build_idf_csv_rows(data, ns, keys)
    _generate_idf_binary(csv_rows, out_path)
    return out_path


def get_posix_bin(
    part_label: str, ns: str, data: dict[str, Any], base_dir: Path, out_dir: Path
) -> Path:
    keys = _parse_factory_constants()
    data = _validate_and_normalize_input(data, base_dir, keys)

    out_path = _generate_posix_binary(data, out_dir, part_label, ns, keys)
    return out_path


def run(part_label: str, ns: str, json_input_path: Path) -> Tuple[Path, Path]:
    base_dir = json_input_path.parent
    try:
        data = json.loads(json_input_path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise InputError(f"Failed to read/parse JSON input: {json_input_path}: {exc}")

    keys = _parse_factory_constants()
    data = _validate_and_normalize_input(data, base_dir, keys)

    # Prepare outputs: out/<json_basename>/...
    run_label = json_input_path.stem
    out_root = _get_out_dir() / run_label
    out_idf_dir = out_root / "esp-idf"
    out_posix_dir = out_root / "posix"

    # ESP-IDF: build CSV rows and generate binary
    idf_bin_path = None
    if nvs_partition_gen is not None:
        csv_rows = _build_idf_csv_rows(data, ns, keys)
        idf_bin_path = out_idf_dir / f"{part_label}.bin"
        _generate_idf_binary(csv_rows, idf_bin_path)
    else:
        print(
            "Warning: ESP-IDF NVS partition generation is not supported. Install esp-idf-nvs-partition-gen to enable."
        )

    # POSIX: write records file
    posix_bin_path = _generate_posix_binary(data, out_posix_dir, part_label, ns, keys)

    return idf_bin_path, posix_bin_path


def run_matter(
    part_label: str,
    ns: str,
    json_input_path: Path,
    vendor_id: int = 0xFFF2,
    product_id: int = 0x8001,
) -> Tuple[Optional[Path], Path]:
    """
    Matter factory flow: ``esp-matter-mfg-tool`` + merged RainMaker NVS. Writes QR payload to
    ``out/<json_stem>/esp-idf/qr_payload.txt``.
    """
    base_dir = json_input_path.parent
    try:
        data = json.loads(json_input_path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise InputError(
            f"Failed to read/parse JSON input: {json_input_path}: {exc}"
        ) from exc

    run_label = json_input_path.stem
    out_root = _get_out_dir() / run_label
    out_idf_dir = out_root / "esp-idf"
    out_idf_dir.mkdir(parents=True, exist_ok=True)
    out_bin = out_idf_dir / f"{part_label}.bin"
    work_root = out_idf_dir / "matter_mfg_work"

    _, meta = get_matter_idf_credentials(
        ns,
        data,
        base_dir,
        out_bin,
        vendor_id=vendor_id,
        product_id=product_id,
        work_root=work_root,
    )
    shutil.rmtree(work_root, ignore_errors=True)

    # Write DAC key and cert to the output directory
    dac_key_path = out_root / "dac_key.pem"
    dac_key_path.write_text(meta["dac_key"], encoding="utf-8")
    dac_cert_path = out_root / "dac_cert.pem"
    dac_cert_path.write_text(meta["dac_cert"], encoding="utf-8")

    # Write QR link to the output directory
    qr_payload = meta["qr_payload"]
    qr_link = (
        f"https://project-chip.github.io/connectedhomeip/qrcode.html?data={qr_payload}"
    )
    qr_path = out_root / "qr_link.txt"
    qr_path.write_text(qr_link, encoding="utf-8")
    out_root = out_root.rename(_get_out_dir() / (run_label + "_" + meta["thing_name"]))
    out_bin = out_root / "esp-idf" / f"{part_label}.bin"
    return out_bin, qr_link


def clear_out_dir() -> None:
    out_dir = _get_out_dir()
    if out_dir.exists():
        shutil.rmtree(out_dir)


def _parse_hex_int(s: str) -> int:
    return int(s, 0)


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Generate factory NVS images for ESP-IDF and POSIX."
    )
    parser.add_argument(
        "--matter",
        action="store_true",
        help="Run esp-matter-mfg-tool and merge RainMaker factory NVS (JSON needs mqtt_host only)",
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
    parser.add_argument("part_label", type=str, help="Partition label")
    parser.add_argument("ns", type=str, help="Namespace")
    parser.add_argument("json_input", type=str, help="Path to JSON input file")
    args = parser.parse_args(argv)

    json_input_path = Path(args.json_input).resolve()
    if not json_input_path.is_file():
        print(f"Input JSON does not exist: {json_input_path}", file=sys.stderr)
        return 2

    try:
        if args.matter:
            idf_bin, qr_link = run_matter(
                args.part_label,
                args.ns,
                json_input_path,
                vendor_id=args.vendor_id,
                product_id=args.product_id,
            )
            print("Generated:")
            print(f"  ESP-IDF: {idf_bin}")
            print(f"  QR:      {qr_link}")
            print("  POSIX:   (skipped for --matter)")
        else:
            idf_bin, posix_bin = run(args.part_label, args.ns, json_input_path)
            print("Generated:")
            print(f"  ESP-IDF: {idf_bin}")
            print(f"  POSIX:   {posix_bin}")
    except InputError as exc:
        print(str(exc), file=sys.stderr)
        return 3

    return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main(sys.argv[1:]))
