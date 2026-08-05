# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""Test-style EC/RSA node key and X.509 cert generation for IoT clients.

Logic aligned with RainMaker Neo backend ``test.test_device`` helpers so factory tooling
does not need the backend tree on ``PYTHONPATH``.
"""

from __future__ import annotations

import datetime

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.x509.oid import NameOID


def _generate_key_and_cert(
    thing_name, key_type="ec", node_key=None, node_validity_days=365
):
    """Internal implementation that supports passing an existing node_key and custom validity."""
    if key_type == "ec":
        ca_key = ec.generate_private_key(ec.SECP256R1())
    elif key_type == "rsa":
        ca_key = rsa.generate_private_key(
            public_exponent=65537,
            key_size=2048,
        )
    else:
        raise ValueError("Invalid key type. Use 'ec' or 'rsa'.")

    ca_subject = issuer = x509.Name(
        [
            x509.NameAttribute(NameOID.COUNTRY_NAME, "US"),
            x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "California"),
            x509.NameAttribute(NameOID.LOCALITY_NAME, "San Francisco"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Test CA"),
            x509.NameAttribute(NameOID.COMMON_NAME, "Test CA"),
        ]
    )
    ca_cert = (
        x509.CertificateBuilder()
        .subject_name(ca_subject)
        .issuer_name(issuer)
        .public_key(ca_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(datetime.datetime.now(datetime.timezone.utc))
        .not_valid_after(
            datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=3650)
        )
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .sign(ca_key, hashes.SHA256())
    )

    if node_key is None:
        if key_type == "ec":
            node_key = ec.generate_private_key(ec.SECP256R1())
        elif key_type == "rsa":
            node_key = rsa.generate_private_key(
                public_exponent=65537,
                key_size=2048,
            )

    node_subject = x509.Name(
        [
            x509.NameAttribute(NameOID.COUNTRY_NAME, "US"),
            x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "California"),
            x509.NameAttribute(NameOID.LOCALITY_NAME, "San Francisco"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Test Organization"),
            x509.NameAttribute(NameOID.COMMON_NAME, thing_name),
        ]
    )
    node_cert = (
        x509.CertificateBuilder()
        .subject_name(node_subject)
        .issuer_name(ca_subject)
        .public_key(node_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(datetime.datetime.now(datetime.timezone.utc))
        .not_valid_after(
            datetime.datetime.now(datetime.timezone.utc)
            + datetime.timedelta(days=node_validity_days)
        )
        .add_extension(
            x509.SubjectAlternativeName([x509.DNSName(thing_name)]),
            critical=False,
        )
        .sign(ca_key, hashes.SHA256())
    )

    node_key_pem = node_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=(
            serialization.PrivateFormat.TraditionalOpenSSL
            if key_type == "ec"
            else serialization.PrivateFormat.PKCS8
        ),
        encryption_algorithm=serialization.NoEncryption(),
    ).decode("utf-8")

    node_cert_pem = node_cert.public_bytes(serialization.Encoding.PEM).decode("utf-8")
    ca_cert_pem = ca_cert.public_bytes(serialization.Encoding.PEM).decode("utf-8")
    combined_cert_pem = node_cert_pem + ca_cert_pem

    return node_key_pem, combined_cert_pem


def generate_key_and_cert(thing_name, key_type="ec"):
    return _generate_key_and_cert(thing_name, key_type=key_type)


def split_combined_cert_pem(combined_cert_pem):
    node_cert = ""
    node_ca_cert = ""
    certs = combined_cert_pem.split("-----BEGIN CERTIFICATE-----", 2)
    if len(certs) >= 2:
        node_cert = "-----BEGIN CERTIFICATE-----\n" + certs[1].strip()
    else:
        print("Node cert not found")

    if len(certs) == 3:
        node_ca_cert = "-----BEGIN CERTIFICATE-----\n" + certs[2].strip()
    else:
        print("CA not found")

    return node_cert, node_ca_cert
