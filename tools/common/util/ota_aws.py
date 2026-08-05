# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

# ruff: noqa: E402 -- sys.path prelude must run before the imports below

"""
AWS OTA Manager

This class provides comprehensive OTA (Over-The-Air) testing capabilities for AWS IoT devices.
It handles infrastructure setup, job creation, execution, cancellation, and cleanup.
"""

import json
import os
import random
import sys
import boto3
import time
import hashlib
from pathlib import Path
from botocore.exceptions import ClientError, NoCredentialsError
from datetime import datetime, timezone, timedelta
import uuid
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from typing import Generator, Optional, Callable

# Belt and braces: callers (pytest via pythonpath, tools via their own prelude) already
# put tools/common on sys.path, but this module is also imported standalone.
_COMMON_ROOT = str(Path(__file__).resolve().parents[1])
if _COMMON_ROOT not in sys.path:
    sys.path.insert(0, _COMMON_ROOT)
from credentials_store import RM_CONFIG, OTA_CREDENTIALS_DIR

# Color codes for output
GREEN = "\033[92m"
BLUE = "\033[94m"
RED = "\033[91m"
YELLOW = "\033[93m"
RESET = "\033[0m"

CODE_SIGNING_CREDENTIALS_DIR = OTA_CREDENTIALS_DIR
CODE_SIGNING_CREDENTIALS_DIR.mkdir(parents=True, exist_ok=True)
CODE_SIGNER_PREFIX = "ota_test_signer_profile_"


def ota_log(message, color=GREEN):
    """Print message with OTA prefix in specified color."""
    print(f"{color}[OTA]{RESET} {message}")


def ota_error(message):
    """Print error message in red."""
    ota_log(f"ERROR: {message}", RED)


def ota_warn(message):
    """Print warning message in yellow."""
    ota_log(f"WARNING: {message}", YELLOW)


def ota_info(message):
    """Print info message in blue."""
    ota_log(f"INFO: {message}", BLUE)


_THROTTLE_ERROR_CODES = {
    "ThrottlingException",
    "Throttling",
    "ThrottledException",
    "TooManyRequestsException",
    "LimitExceededException",
    "RequestLimitExceeded",
}


def _call_with_throttle_retry(
    callable_fn, *, what="AWS call", max_retries=8, base_delay=5.0, cap=60.0
):
    """Invoke callable_fn(), retrying on AWS throttle errors with exp backoff + jitter."""
    attempt = 0
    while True:
        try:
            return callable_fn()
        except ClientError as e:
            code = e.response.get("Error", {}).get("Code", "")
            if code not in _THROTTLE_ERROR_CODES or attempt >= max_retries:
                raise
            retry_after = None
            try:
                hdr = (
                    e.response.get("ResponseMetadata", {})
                    .get("HTTPHeaders", {})
                    .get("retry-after")
                )
                if hdr is not None:
                    retry_after = float(hdr)
            except (TypeError, ValueError):
                retry_after = None
            delay = retry_after if retry_after else min(cap, base_delay * (2**attempt))
            delay += random.uniform(0, base_delay)
            ota_warn(
                f"{what} throttled ({code}); retrying in {delay:.1f}s (attempt {attempt + 1}/{max_retries})"
            )
            time.sleep(delay)
            attempt += 1


class OTAJobStatus:
    def __init__(self, status: str, details: dict, last_updated_str: str):
        self.status = status
        self.details = details
        self.last_updated_dt = self._format_timestamp(last_updated_str)

    def _format_timestamp(self, value):
        """Convert various timestamp formats to a human-friendly string"""
        if not value:
            return "Unknown"
        try:
            if isinstance(value, datetime):
                dt = value.astimezone()
            elif isinstance(value, (int, float)):
                # AWS might return seconds or milliseconds; detect by size
                seconds = value / 1000 if value > 1e12 else value
                dt = datetime.fromtimestamp(seconds, tz=timezone.utc).astimezone()
            elif isinstance(value, str):
                dt = datetime.fromisoformat(value)
                if dt.tzinfo is None:
                    dt = dt.replace(tzinfo=timezone.utc)
                dt = dt.astimezone()
            else:
                return str(value)
            return dt.strftime("%Y-%m-%d %H:%M:%S %Z%z")
        except Exception:
            return str(value)


class OTAFile:
    SIGNING_ALGORITHMS = ["ECDSA", "RSA"]
    HASH_ALGORITHMS = ["SHA256", "SHA1"]
    REQUIRED_FIELDS = ["name", "path", "file_id"]

    def __init__(self, name: str, path: Path, file_id: int, **kwargs):
        self.name = name
        self.path = path
        self.file_id = file_id
        self.needs_signing = kwargs.get("needs_signing", True)
        self.signature = kwargs.get("signature", None)
        self.hash_algorithm = kwargs.get("hash_algorithm", self.HASH_ALGORITHMS[0])
        self.signing_algorithm = kwargs.get(
            "signing_algorithm", self.SIGNING_ALGORITHMS[0]
        )

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "path": str(self.path),
            "file_id": self.file_id,
            "signing": 1 if self.needs_signing else 0,
            "signature": self.signature,
            "hash_algorithm": self.hash_algorithm,
            "signing_algorithm": self.signing_algorithm,
        }

    def validate(self):
        if self.signature is not None and not isinstance(self.signature, str):
            raise ValueError("Signature must be a base64 string or None")
        if self.signing_algorithm not in self.SIGNING_ALGORITHMS:
            raise ValueError(
                f"Invalid signing algorithm: {self.signing_algorithm}. Must be one of {self.SIGNING_ALGORITHMS}"
            )
        if self.hash_algorithm not in self.HASH_ALGORITHMS:
            raise ValueError(
                f"Invalid hash algorithm: {self.hash_algorithm}. Must be one of {self.HASH_ALGORITHMS}"
            )

    @staticmethod
    def from_json(json_data: dict) -> "OTAFile":
        for field in OTAFile.REQUIRED_FIELDS:
            if field not in json_data:
                raise ValueError(f"Missing required field '{field}' in JSON data")
        return OTAFile(**json_data)


class OTAFilesConfig:
    def __init__(self, files: list[OTAFile]):
        self.files = files

    def validate(self):
        file_ids = set()
        for file in self.files:
            file.validate()
            if file.file_id in file_ids:
                raise ValueError(f"Duplicate file ID: {file.file_id}")
            file_ids.add(file.file_id)

    @staticmethod
    def from_json(json_data: list[dict]) -> "OTAFilesConfig":
        return OTAFilesConfig([OTAFile.from_json(file) for file in json_data])

    def __iter__(self):
        return iter(self.files)

    def __len__(self):
        return len(self.files)


class DayTime:
    def __init__(self, hour: int, minute: int):
        self.hour = hour
        self.minute = minute

    def to_minutes_since_midnight(self) -> int:
        return self.hour * 60 + self.minute


class RmngOtaDownloadWindow:
    def __init__(
        self,
        start_time: datetime,
        end_time: datetime,
        daily_start: DayTime,
        daily_end: DayTime,
    ):
        self.start_time = start_time
        self.end_time = end_time
        self.daily_start = daily_start
        self.daily_end = daily_end

    def to_dict(self) -> dict:
        return {
            "validity": {
                "start": int(self.start_time.timestamp()),
                "end": int(self.end_time.timestamp()),
            },
            "daily": {
                "start": self.daily_start.to_minutes_since_midnight(),
                "end": self.daily_end.to_minutes_since_midnight(),
            },
        }

    @staticmethod
    def from_json(json_data: dict) -> "RmngOtaDownloadWindow":
        """Create RmngOtaDownloadWindow from JSON data"""
        if not isinstance(json_data, dict):
            raise ValueError("RmngOtaDownloadWindow JSON data must be a dictionary")

        validity = json_data.get("validity")
        if validity is None:
            raise ValueError(
                "Missing required field 'validity' in RmngOtaDownloadWindow JSON data"
            )
        if not isinstance(validity, dict):
            raise ValueError("'validity' field must be a dictionary")

        # Check validity fields
        if "start" not in validity:
            raise ValueError("Missing required field 'start' in validity section")
        if "end" not in validity:
            raise ValueError("Missing required field 'end' in validity section")

        # Daily section is optional - default to full day (00:00 to 23:59) if not provided
        daily = json_data.get("daily")
        if daily is not None:
            if not isinstance(daily, dict):
                raise ValueError("'daily' field must be a dictionary")

            # Check daily fields if daily section is provided
            if "start" not in daily:
                raise ValueError("Missing required field 'start' in daily section")
            if "end" not in daily:
                raise ValueError("Missing required field 'end' in daily section")

            daily_start_minutes = daily["start"]
            daily_end_minutes = daily["end"]
        else:
            # Default to full day: 00:00 (0 minutes) to 23:59 (1439 minutes)
            daily_start_minutes = 0
            daily_end_minutes = 1439

        try:
            start_time = datetime.fromtimestamp(validity["start"], tz=timezone.utc)
            end_time = datetime.fromtimestamp(validity["end"], tz=timezone.utc)
            daily_start = DayTime(daily_start_minutes // 60, daily_start_minutes % 60)
            daily_end = DayTime(daily_end_minutes // 60, daily_end_minutes % 60)
        except (ValueError, TypeError) as e:
            raise ValueError(
                f"Invalid timestamp or time value in RmngOtaDownloadWindow: {e}"
            )

        return RmngOtaDownloadWindow(
            start_time=start_time,
            end_time=end_time,
            daily_start=daily_start,
            daily_end=daily_end,
        )


class RmngOtaInfo:
    def __init__(
        self,
        filetype: Optional[str] = None,
        fw_version: Optional[str] = None,
        min_fw_version: Optional[str] = None,
        metadata: Optional[dict] = None,
        download_window: Optional[RmngOtaDownloadWindow] = None,
        file_md5: Optional[str] = None,
    ):
        self.filetype = filetype
        self.fw_version = fw_version
        self.min_fw_version = min_fw_version
        self.metadata = metadata
        self.download_window = download_window
        # Usually auto-injected from the uploaded image by create_custom_job; may be set
        # explicitly to override. Enables device-side auto-resume + MD5 integrity check.
        self.file_md5 = file_md5

    def to_dict(self) -> dict:
        info = {}

        if self.filetype:
            info["filetype"] = self.filetype
        if self.fw_version:
            info["fw_version"] = self.fw_version
        if self.file_md5:
            info["file_md5"] = self.file_md5
        if self.min_fw_version:
            info["min_fw_version"] = self.min_fw_version
        if self.metadata:
            info["metadata"] = self.metadata
        if self.download_window:
            info["download_window"] = self.download_window.to_dict()
        return info


class OTAManager:
    def __init__(self):
        self.region = RM_CONFIG["StackRegion"]
        self.account_id = None

        # AWS clients
        self.s3_client = None
        self.iot_client = None
        self.iot_data_client = None
        self.signer_client = None
        self.iam_client = None
        self.sts_client = None
        self.acm_client = None

        # OTA resources
        self.ota_bucket_name = None
        self.ota_role_name = "aws-iot-device-update-role"
        self.certificate_arn = None
        self.platform_id = "AmazonFreeRTOS-Default"
        self.created_streams = []  # Track created streams for cleanup
        # Tracking for resources created by THIS process — used by cleanup_created_resources()
        # so that we never touch jobs/objects that other users on the shared AWS account own.
        self.created_jobs: list[
            dict
        ] = []  # entries: {"id": str, "is_ota_update": bool}
        self.uploaded_s3_keys: list[tuple[str, str]] = []  # (bucket, key)

        self.initialize_aws_clients()

        # Look for latest code signing profile
        self.code_signer_profile_name = self.get_latest_code_signing_profile()
        ota_info(f"Using latest code signing profile: {self.code_signer_profile_name}")

    def get_latest_code_signing_profile(self):
        """Get the latest code signing profile"""
        try:
            response = self.signer_client.list_signing_profiles(
                maxResults=25, statuses=["Active"]
            )

            # only look for Active profiles that start with the prefix
            possible_profiles = sorted(
                [
                    p["profileName"]
                    for p in response.get("profiles", [])
                    if p["profileName"].startswith(CODE_SIGNER_PREFIX)
                ],
                reverse=True,
                key=lambda x: int(x.split("_")[-1]),
            )
            if possible_profiles:
                return possible_profiles[0]
            else:
                return None
        except Exception as e:
            ota_error(f"Error getting latest code signing profile: {e}")
            return None

    def initialize_aws_clients(self):
        """Initialize AWS service clients"""
        try:
            # Initialize STS client to get account ID
            self.sts_client = boto3.client("sts", region_name=self.region)
            self.account_id = self.sts_client.get_caller_identity()["Account"]
            ota_info(f"Using AWS Account: {self.account_id}")

            # Initialize other AWS clients
            self.s3_client = boto3.client("s3", region_name=self.region)
            self.iot_client = boto3.client("iot", region_name=self.region)
            self.iot_data_client = boto3.client("iot-data", region_name=self.region)
            self.signer_client = boto3.client("signer", region_name=self.region)
            self.iam_client = boto3.client("iam", region_name=self.region)
            self.acm_client = boto3.client("acm", region_name=self.region)

            # Set OTA bucket name with account ID
            self.ota_bucket_name = f"ota-test-bucket-{self.account_id}"

            ota_info("AWS clients initialized successfully")
        except NoCredentialsError:
            ota_error(
                "AWS credentials not found. Please configure your AWS credentials."
            )
            raise RuntimeError("AWS credentials not available")
        except Exception as e:
            ota_error(f"Error initializing AWS clients: {e}")
            raise RuntimeError(f"AWS client initialization failed: {e}")

    def setup_infrastructure(
        self, cert_type="ECDSA", platform_id="AmazonFreeRTOS-Default"
    ):
        """Set up OTA infrastructure including S3 bucket, certificates, and Code Signer profile"""
        ota_info("Setting up OTA infrastructure...")
        self.platform_id = platform_id

        try:
            # Create S3 bucket
            self.create_s3_bucket()

            # Create IAM role for OTA
            self.create_ota_iam_role()

            # Create Code Signer profile
            self.create_code_signer_profile(cert_type)

            ota_log("OTA infrastructure setup completed successfully!")
            ota_info(f"Certificate ARN: {self.certificate_arn}")
            ota_info(f"Platform ID: {self.platform_id}")

        except Exception as e:
            ota_error(f"Infrastructure setup failed: {e}")
            sys.exit(1)

    def create_s3_bucket(self):
        """Create S3 bucket for OTA files"""
        try:
            # Check if bucket already exists
            try:
                self.s3_client.head_bucket(Bucket=self.ota_bucket_name)
                ota_info(f"S3 bucket {self.ota_bucket_name} already exists")
                return
            except ClientError as e:
                if e.response["Error"]["Code"] != "404":
                    raise

            # Create bucket
            if self.region == "us-east-1":
                # us-east-1 doesn't need location constraint
                self.s3_client.create_bucket(Bucket=self.ota_bucket_name)
            else:
                self.s3_client.create_bucket(
                    Bucket=self.ota_bucket_name,
                    CreateBucketConfiguration={"LocationConstraint": self.region},
                )

            # Enable versioning
            self.s3_client.put_bucket_versioning(
                Bucket=self.ota_bucket_name,
                VersioningConfiguration={"Status": "Enabled"},
            )

            # Set bucket policy for IoT access
            bucket_policy = {
                "Version": "2012-10-17",
                "Statement": [
                    {
                        "Effect": "Allow",
                        "Principal": {"Service": "iot.amazonaws.com"},
                        "Action": ["s3:GetObject", "s3:GetObjectVersion"],
                        "Resource": f"arn:aws:s3:::{self.ota_bucket_name}/*",
                    }
                ],
            }

            self.s3_client.put_bucket_policy(
                Bucket=self.ota_bucket_name, Policy=json.dumps(bucket_policy)
            )

            ota_log(f"Created S3 bucket: {self.ota_bucket_name}")

        except ClientError as e:
            if e.response["Error"]["Code"] == "BucketAlreadyOwnedByYou":
                ota_info(
                    f"S3 bucket {self.ota_bucket_name} already exists and is owned by you"
                )
            else:
                raise

    def get_code_signing_credentials_name(self, cert_type: str) -> str:
        """Get the name of the code signing certificate"""
        return f"ota-test-cert-{cert_type.lower()}"

    def get_code_signing_credentials_paths(self, cert_type: str) -> tuple[Path, Path]:
        """Get the paths to the code signing certificate and private key"""
        cert_name = self.get_code_signing_credentials_name(cert_type)
        return (
            CODE_SIGNING_CREDENTIALS_DIR / f"{cert_name}.pem",
            CODE_SIGNING_CREDENTIALS_DIR / f"{cert_name}_key.pem",
        )

    def save_code_signing_credentials_local(
        self, cert_type: str, cert_pem: str, key_pem: str
    ):
        """Save certificate and private key to local file"""
        # Save certificate and private key to build/code_signing_credentials
        self.codesign_cert_path, self.codesign_key_path = (
            self.get_code_signing_credentials_paths(cert_type)
        )
        self.codesign_cert_path.write_text(cert_pem)
        self.codesign_key_path.write_text(key_pem)

        ota_info(f"Saved certificate to: {self.codesign_cert_path}")
        ota_info(f"Saved private key to: {self.codesign_key_path}")

    def get_codesign_cert_path(self) -> Optional[Path]:
        # Try ECDSA first
        cert_path = self.get_code_signing_credentials_paths("ECDSA")[0]
        if cert_path.exists():
            return cert_path
        # Try RSA if ECDSA doesn't exist
        cert_path = self.get_code_signing_credentials_paths("RSA")[0]
        if cert_path.exists():
            return cert_path
        return None

    def generate_and_upload_certificate(self, cert_type="ECDSA"):
        """Generate self-signed certificate and upload to ACM"""
        try:
            # Check if certificate already exists
            cert_name = self.get_code_signing_credentials_name(cert_type)

            # List existing certificates
            try:
                response = self.acm_client.list_certificates()
                for cert in response.get("CertificateSummaryList", []):
                    if cert.get("DomainName") == cert_name:
                        self.certificate_arn = cert["CertificateArn"]
                        ota_info(f"Using existing certificate: {self.certificate_arn}")
                        return
            except Exception as e:
                ota_warn(f"Error checking existing certificates: {e}")

            ota_info(f"Generating {cert_type} certificate for code signing...")

            # Generate private key
            if cert_type.upper() == "RSA":
                private_key = rsa.generate_private_key(
                    public_exponent=65537, key_size=2048
                )
            else:  # ECDSA
                private_key = ec.generate_private_key(ec.SECP256R1())

            # Create certificate
            subject = issuer = x509.Name(
                [
                    x509.NameAttribute(NameOID.COUNTRY_NAME, "US"),
                    x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "California"),
                    x509.NameAttribute(NameOID.LOCALITY_NAME, "San Francisco"),
                    x509.NameAttribute(
                        NameOID.ORGANIZATION_NAME, "OTA Test Organization"
                    ),
                    x509.NameAttribute(NameOID.COMMON_NAME, cert_name),
                ]
            )

            cert = (
                x509.CertificateBuilder()
                .subject_name(subject)
                .issuer_name(issuer)
                .public_key(private_key.public_key())
                .serial_number(x509.random_serial_number())
                .not_valid_before(datetime.now(timezone.utc))
                .not_valid_after(datetime.now(timezone.utc) + timedelta(days=365))
                .add_extension(
                    x509.KeyUsage(
                        digital_signature=True,
                        key_encipherment=False,
                        key_agreement=False,
                        key_cert_sign=True,
                        crl_sign=False,
                        content_commitment=False,
                        data_encipherment=False,
                        encipher_only=False,
                        decipher_only=False,
                    ),
                    critical=True,
                )
                .sign(private_key, hashes.SHA256())
            )

            # Serialize certificate and private key
            cert_pem = cert.public_bytes(serialization.Encoding.PEM).decode("utf-8")
            key_pem = private_key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.PKCS8,
                encryption_algorithm=serialization.NoEncryption(),
            ).decode("utf-8")

            # Upload to ACM
            response = self.acm_client.import_certificate(
                Certificate=cert_pem, PrivateKey=key_pem
            )

            self.certificate_arn = response["CertificateArn"]
            ota_log(f"Generated and uploaded {cert_type} certificate to ACM")
            ota_info(f"Certificate ARN: {self.certificate_arn}")

            # save certificate and private key to local file
            self.save_code_signing_credentials_local(cert_type, cert_pem, key_pem)

        except Exception as e:
            ota_error(f"Error generating/uploading certificate: {e}")
            raise

    def create_ota_iam_role(self):
        """Create IAM role for OTA jobs"""
        try:
            # Check if role already exists
            try:
                self.iam_client.get_role(RoleName=self.ota_role_name)
                ota_info(f"IAM role {self.ota_role_name} already exists")
                return
            except ClientError as e:
                if e.response["Error"]["Code"] != "NoSuchEntity":
                    raise

            # Create trust policy
            trust_policy = {
                "Version": "2012-10-17",
                "Statement": [
                    {
                        "Effect": "Allow",
                        "Principal": {"Service": "iot.amazonaws.com"},
                        "Action": "sts:AssumeRole",
                    }
                ],
            }

            # Create role
            self.iam_client.create_role(
                RoleName=self.ota_role_name,
                AssumeRolePolicyDocument=json.dumps(trust_policy),
                Description="Role for AWS IoT OTA updates",
            )

            # Attach managed policies with retry logic
            policies = [
                "arn:aws:iam::aws:policy/service-role/AWSIoTThingsRegistration",
                "arn:aws:iam::aws:policy/service-role/AWSIoTLogging",
                "arn:aws:iam::aws:policy/AWSIoTOTAUpdate",
            ]

            # Wait a moment for role to propagate
            time.sleep(2)

            for policy_arn in policies:
                max_retries = 3
                for attempt in range(max_retries):
                    try:
                        self.iam_client.attach_role_policy(
                            RoleName=self.ota_role_name, PolicyArn=policy_arn
                        )
                        break
                    except ClientError as e:
                        if e.response["Error"]["Code"] == "NoSuchEntity":
                            if "AWSIoTOTAUpdate" in policy_arn:
                                ota_warn(
                                    "Policy AWSIoTOTAUpdate not available in this region/account, skipping"
                                )
                                break
                            elif attempt < max_retries - 1:
                                ota_info(
                                    f"Role not ready yet, retrying in 2 seconds... (attempt {attempt + 1}/{max_retries})"
                                )
                                time.sleep(2)
                            else:
                                ota_error(
                                    f"Failed to attach policy {policy_arn} after {max_retries} attempts"
                                )
                                raise
                        else:
                            raise

            # Create inline policy for additional OTA permissions
            ota_policy = {
                "Version": "2012-10-17",
                "Statement": [
                    {
                        "Effect": "Allow",
                        "Action": "iam:PassRole",
                        "Resource": f"arn:aws:iam::{self.account_id}:role/{self.ota_role_name}",
                    },
                    {
                        "Effect": "Allow",
                        "Action": [
                            "s3:GetObject",
                            "s3:GetObjectVersion",
                            "s3:PutObject",
                            "s3:GetObjectVersionAcl",
                            "s3:GetObjectAcl",
                        ],
                        "Resource": f"arn:aws:s3:::{self.ota_bucket_name}/*",
                    },
                    {
                        "Effect": "Allow",
                        "Action": ["s3:ListBucket", "s3:ListBucketVersions"],
                        "Resource": f"arn:aws:s3:::{self.ota_bucket_name}",
                    },
                    {
                        "Effect": "Allow",
                        "Action": [
                            "iot:CreateStream",
                            "iot:DeleteStream",
                            "iot:DescribeStream",
                            "iot:ListStreams",
                            "iot:CreateJob",
                            "iot:DescribeJob",
                            "iot:ListJobs",
                            "iot:CancelJob",
                            "iot:DeleteJob",
                            "iot:DescribeJobExecution",
                            "iot:ListJobExecutionsForJob",
                            "iot:ListJobExecutionsForThing",
                        ],
                        "Resource": "*",
                    },
                    {
                        "Effect": "Allow",
                        "Action": [
                            "signer:DescribeSigningJob",
                            "signer:GetSigningProfile",
                            "signer:StartSigningJob",
                        ],
                        "Resource": "*",
                    },
                ],
            }

            self.iam_client.put_role_policy(
                RoleName=self.ota_role_name,
                PolicyName="OTAEnhancedAccess",
                PolicyDocument=json.dumps(ota_policy),
            )

            ota_log(f"Created IAM role: {self.ota_role_name}")

        except ClientError as e:
            ota_error(f"Error creating IAM role: {e}")
            raise

    def create_code_signer_profile(self, cert_type="ECDSA"):
        """Create AWS Code Signer profile for firmware signing"""
        try:
            # Check if profile already exists
            if self.code_signer_profile_name:
                try:
                    self.signer_client.get_signing_profile(
                        profileName=self.code_signer_profile_name
                    )
                    ota_info(
                        f"Code Signer profile {self.code_signer_profile_name} already exists"
                    )
                    return
                except ClientError as e:
                    if e.response["Error"]["Code"] != "ResourceNotFoundException":
                        raise

            # Generate profile name
            self.code_signer_profile_name = (
                f"ota_test_signer_profile_{datetime.now().timestamp():.0f}"
            )

            if not self.certificate_arn:
                self.generate_and_upload_certificate(cert_type)

            ota_info(f"Creating Code Signer profile with platform: {self.platform_id}")

            # Create signing profile with real certificate
            self.signer_client.put_signing_profile(
                profileName=self.code_signer_profile_name,
                signingMaterial={"certificateArn": self.certificate_arn},
                platformId=self.platform_id,
                signingParameters={
                    "certname": "/ota-test-cert"  # Path on device where certificate is stored
                },
            )

            ota_log(f"Created Code Signer profile: {self.code_signer_profile_name}")
            ota_info("Using JSONDetached format for signature separation")

        except ClientError as e:
            ota_error(f"Error creating Code Signer profile: {e}")
            raise

    def generate_stream_id(self):
        """Generate unique stream ID with timestamp and UUID"""
        timestamp = int(time.time())
        stream_uuid = uuid.uuid4().hex[:8]
        return f"AFR_OTA-{timestamp}-{stream_uuid}"

    def create_stream(
        self, stream_id: str, files_info, description="OTA stream for file delivery"
    ):
        """Create AWS IoT stream for file delivery

        Args:
            stream_id (str): Stream ID
            files_info (list): List of file information dictionaries
            description (str): Stream description

        Returns:
            str: Stream ID if successful, None otherwise
        """
        try:
            # Prepare files for stream creation
            stream_files = []
            for i, file_info in enumerate(files_info):
                stream_file = {
                    "fileId": i,  # Use integer file ID for streams
                    "s3Location": {
                        "bucket": file_info["bucket"],
                        "key": file_info["key"],
                    },
                }

                # Add version if available
                if "version" in file_info and file_info["version"]:
                    stream_file["s3Location"]["version"] = file_info["version"]

                stream_files.append(stream_file)

            # Create the stream
            ota_info(f"Creating stream: {stream_id}")
            response = self.iot_client.create_stream(
                streamId=stream_id,
                description=description,
                files=stream_files,
                roleArn=f"arn:aws:iam::{self.account_id}:role/{self.ota_role_name}",
            )

            # Track created stream for cleanup
            self.created_streams.append(stream_id)

            ota_log(f"Created stream: {stream_id}")
            ota_info(f"Stream ARN: {response['streamArn']}")
            ota_info(f"Stream contains {len(stream_files)} files")

            return stream_id

        except Exception as e:
            ota_error(f"Error creating stream: {e}")
            return None

    def describe_stream(self, stream_id):
        """Get stream information

        Args:
            stream_id (str): Stream ID to describe

        Returns:
            dict: Stream information or None if error
        """
        try:
            response = self.iot_client.describe_stream(streamId=stream_id)
            return response["streamInfo"]
        except Exception as e:
            ota_error(f"Error describing stream {stream_id}: {e}")
            return None

    def delete_stream(self, stream_id):
        """Delete AWS IoT stream

        Args:
            stream_id (str): Stream ID to delete

        Returns:
            bool: True if successful, False otherwise
        """
        try:
            self.iot_client.delete_stream(streamId=stream_id)
            ota_log(f"Deleted stream: {stream_id}")

            # Remove from tracking list
            if stream_id in self.created_streams:
                self.created_streams.remove(stream_id)

            return True
        except ClientError as e:
            if e.response["Error"]["Code"] == "ResourceNotFoundException":
                ota_info(f"Stream {stream_id} doesn't exist")
                # Remove from tracking list anyway
                if stream_id in self.created_streams:
                    self.created_streams.remove(stream_id)
                return True
            else:
                ota_error(f"Error deleting stream {stream_id}: {e}")
                return False
        except Exception as e:
            ota_error(f"Error deleting stream {stream_id}: {e}")
            return False

    def list_streams(self):
        """List all streams in the account

        Returns:
            list: List of stream summaries
        """
        try:
            response = self.iot_client.list_streams()
            return response.get("streams", [])
        except Exception as e:
            ota_error(f"Error listing streams: {e}")
            return []

    def cleanup_all_streams(self):
        """Clean up all created streams"""
        ota_info("Cleaning up created streams...")

        # Clean up tracked streams
        for stream_id in self.created_streams.copy():
            self.delete_stream(stream_id)

        # Also clean up any streams that match our naming pattern
        try:
            all_streams = self.list_streams()
            for stream in all_streams:
                stream_id = stream["streamId"]
                if stream_id.startswith("AFR_OTA-"):
                    ota_info(f"Found OTA test stream: {stream_id}")
                    self.delete_stream(stream_id)
        except Exception as e:
            ota_warn(f"Error during stream cleanup: {e}")

    def process_files_for_stream(self, stream_id: str, files_config: OTAFilesConfig):
        """Process files for stream creation: upload, sign if needed, and prepare stream info

        Args:
            stream_id (str): Stream ID
            files_config (OTAFilesConfig): Files configuration
        Returns:
            tuple: (stream_files_info, processed_files_info) or (None, None) if failed
        """
        try:
            stream_files_info = []
            processed_files_info = []

            for file in files_config:
                file_path = file.path
                file_name = file.name
                file_id = file.file_id  # Now integer 0-255
                needs_signing = file.needs_signing

                if not os.path.exists(file_path):
                    ota_error(f"File not found: {file_path}")
                    continue

                ota_info(f"Processing file: {file_name}")

                # Upload original file to S3 (use integer file_id for path)
                s3_key = f"ota-files/{stream_id}/{file_id}/{file_name}"
                s3_info = self.upload_file_to_s3(file_path, s3_key)

                if not s3_info:
                    ota_error(f"Failed to upload file: {file_path}")
                    continue

                final_s3_info = s3_info
                signing_job_id = None

                # Sign file if required
                if needs_signing:
                    ota_info(f"Signing file: {file_name}")

                    signing_result = self.start_signing_job(s3_info)
                    if signing_result:
                        signing_completion = self.wait_for_signing_completion(
                            signing_result
                        )
                        if (
                            signing_completion
                            and signing_completion["status"] == "Succeeded"
                        ):
                            # The location of the signed JSON object
                            signed_location = signing_completion["signed_location"]

                            # Track signed key now (before copy) so cleanup covers it even
                            # if the subsequent copy_object fails.
                            self.uploaded_s3_keys.append(
                                (signed_location["bucket"], signed_location["key"])
                            )

                            # Mimic CreateOTAUpdate: copy the raw binary over the top of the signed object.
                            ota_info(
                                f"Copying raw file over signed file s3://{signed_location['bucket']}/{signed_location['key']}"
                            )
                            copy_response = self.s3_client.copy_object(
                                Bucket=signed_location["bucket"],
                                Key=signed_location["key"],
                                CopySource={
                                    "Bucket": s3_info["bucket"],
                                    "Key": s3_info["key"],
                                },
                            )

                            # The stream will now deliver the latest version, which is the raw binary.
                            stream_version_id = copy_response.get("VersionId")

                            # The stream will point to the signed file's location, but the newest version
                            final_s3_info = signed_location
                            final_s3_info["version"] = stream_version_id
                            final_s3_info["size"] = s3_info[
                                "size"
                            ]  # Keep original size
                            final_s3_info["hash"] = s3_info[
                                "hash"
                            ]  # Keep original hash
                            final_s3_info["md5"] = s3_info[
                                "md5"
                            ]  # Keep original MD5 (of the raw image)
                            signing_job_id = signing_completion["job_id"]

                            ota_log(
                                f"File {file_name} signed and raw file copied successfully"
                            )

                            # Delete the original file from S3
                            self.s3_client.delete_object(
                                Bucket=s3_info["bucket"], Key=s3_info["key"]
                            )
                            ota_log(
                                f"Deleted original file from S3: s3://{s3_info['bucket']}/{s3_info['key']}"
                            )
                            try:
                                self.uploaded_s3_keys.remove(
                                    (s3_info["bucket"], s3_info["key"])
                                )
                            except ValueError:
                                pass
                            # signed_location already tracked above (pre-copy)
                        else:
                            ota_error(f"Failed to sign file: {file_name}")
                            continue
                    else:
                        ota_error(f"Failed to start signing for file: {file_name}")
                        continue

                # Add to stream files info (for stream creation)
                stream_files_info.append(
                    {
                        "bucket": final_s3_info["bucket"],
                        "key": final_s3_info["key"],
                        "version": final_s3_info.get("version"),
                    }
                )

                # Add to processed files info (for job documents)
                processed_file = {
                    "original_info": file.to_dict(),
                    "s3_location": final_s3_info,
                    "signing_job_id": signing_job_id,
                    "needs_signing": needs_signing,
                    "signed_s3_location": signing_completion["signed_location"]
                    if needs_signing
                    else None,
                }
                processed_files_info.append(processed_file)

            if not stream_files_info:
                ota_error("No files were successfully processed for stream")
                return None, None

            return stream_files_info, processed_files_info

        except Exception as e:
            ota_error(f"Error processing files for stream: {e}")
            return None, None

    def calculate_file_hash(self, file_path):
        """Calculate SHA256 hash of a file"""
        sha256_hash = hashlib.sha256()
        try:
            with open(file_path, "rb") as f:
                for chunk in iter(lambda: f.read(4096), b""):
                    sha256_hash.update(chunk)
            return sha256_hash.hexdigest()
        except Exception as e:
            ota_error(f"Error calculating hash for {file_path}: {e}")
            return None

    def calculate_file_md5(self, file_path):
        """Calculate MD5 (hex) of a file.

        Emitted into the job document's rmng_ota.file_md5 so the device can auto-resume an
        interrupted download and verify image integrity end-to-end on completion.
        """
        md5_hash = hashlib.md5()
        try:
            with open(file_path, "rb") as f:
                for chunk in iter(lambda: f.read(4096), b""):
                    md5_hash.update(chunk)
            return md5_hash.hexdigest()
        except Exception as e:
            ota_error(f"Error calculating MD5 for {file_path}: {e}")
            return None

    def get_signature_from_signed_object(self, s3_location):
        """
        Get signature from a signed S3 object that has been overwritten by the raw file.
        This function finds the oldest version of the object, which is assumed to be the
        JSON file containing the signature.
        """
        try:
            bucket = s3_location["bucket"]
            key = s3_location["key"]
            ota_info(f"Getting signature from previous version of s3://{bucket}/{key}")

            # List object versions to find the one with the signature
            versions_response = self.s3_client.list_object_versions(
                Bucket=bucket, Prefix=key
            )
            versions = versions_response.get("Versions", [])

            if len(versions) < 2:
                ota_warn(
                    f"Expected at least two versions for {key} to get signature, but found {len(versions)}"
                )
                # Fallback to just getting the object if there's only one version.
                # This could happen if the copy failed or logic changes.
                if not versions:
                    raise Exception(f"No versions found for object {key}")
                target_version_id = versions[0]["VersionId"]
            else:
                # The oldest version is the signature JSON file
                target_version_id = versions[-1]["VersionId"]

            response = self.s3_client.get_object(
                Bucket=bucket, Key=key, VersionId=target_version_id
            )
            signed_object_dict = json.loads(response["Body"].read().decode("utf-8"))
            return signed_object_dict
        except Exception as e:
            ota_error(f"Error getting signature from {key}: {e}")
            return None

    def upload_file_to_s3(self, file_path, s3_key):
        """Upload a file to S3 and return version information"""
        try:
            # Upload file
            with open(file_path, "rb") as f:
                response = self.s3_client.put_object(
                    Bucket=self.ota_bucket_name,
                    Key=s3_key,
                    Body=f,
                    ServerSideEncryption="AES256",
                )

            version_id = response.get("VersionId", "1")
            file_size = os.path.getsize(file_path)
            file_hash = self.calculate_file_hash(file_path)
            file_md5 = self.calculate_file_md5(file_path)

            ota_log(
                f"Uploaded {file_path} to s3://{self.ota_bucket_name}/{s3_key} (version: {version_id})"
            )

            self.uploaded_s3_keys.append((self.ota_bucket_name, s3_key))

            return {
                "bucket": self.ota_bucket_name,
                "key": s3_key,
                "version": version_id,
                "size": file_size,
                "hash": file_hash,
                "md5": file_md5,
            }

        except Exception as e:
            ota_error(f"Error uploading file {file_path}: {e}")
            return None

    def start_signing_job(self, source_s3_location, destination_prefix="signed/"):
        """Start a signing job using AWS Code Signer

        Args:
            source_s3_location (dict): Source S3 location with bucket, key, version
            destination_prefix (str): Prefix for signed file destination

        Returns:
            str: Signing job ID if successful, None otherwise
        """
        try:
            ota_info(f"Starting signing job for {source_s3_location['key']}")

            response = self.signer_client.start_signing_job(
                source={
                    "s3": {
                        "bucketName": source_s3_location["bucket"],
                        "key": source_s3_location["key"],
                        "version": source_s3_location.get("version", ""),
                    }
                },
                destination={
                    "s3": {
                        "bucketName": source_s3_location["bucket"],
                        "prefix": destination_prefix,
                    }
                },
                profileName=self.code_signer_profile_name,
            )

            job_id = response["jobId"]
            ota_log(f"Started signing job: {job_id}")

            return job_id

        except Exception as e:
            ota_error(f"Error starting signing job: {e}")
            return None

    def wait_for_signing_completion(self, job_id, timeout=300):
        """Wait for signing job to complete

        Args:
            job_id (str): Signing job ID
            timeout (int): Timeout in seconds

        Returns:
            dict: Signing job result with signed S3 location, or None if failed
        """
        try:
            ota_info(f"Waiting for signing job {job_id} to complete...")

            start_time = time.time()
            while time.time() - start_time < timeout:
                response = self.signer_client.describe_signing_job(jobId=job_id)

                status = response["status"]
                ota_info(f"Signing job status: {status}")

                if status == "Succeeded":
                    signed_object = response["signedObject"]["s3"]
                    ota_log("Signing completed successfully")
                    ota_info(
                        f"Signed file: s3://{signed_object['bucketName']}/{signed_object['key']}"
                    )

                    return {
                        "status": "Succeeded",
                        "signed_location": {
                            "bucket": signed_object["bucketName"],
                            "key": signed_object["key"],
                        },
                        "job_id": job_id,
                    }

                elif status in ["Failed", "Canceled"]:
                    ota_error(f"Signing job failed with status: {status}")
                    return None

                # Still in progress, wait a bit
                time.sleep(5)

            ota_error(f"Signing job {job_id} timed out after {timeout} seconds")
            return None

        except Exception as e:
            ota_error(f"Error waiting for signing job {job_id}: {e}")
            return None

    def sign_file(self, s3_location, file_id):
        """Sign a file using AWS Code Signer (legacy method for compatibility)"""
        try:
            job_id = self.start_signing_job(s3_location)
            if not job_id:
                return None

            result = self.wait_for_signing_completion(job_id)
            if result and result["status"] == "Succeeded":
                return result["job_id"]
            else:
                return None

        except Exception as e:
            ota_error(f"Error signing file {file_id}: {e}")
            return None

    def save_job_info(self, job_info: dict, job_info_name: str):
        """Save job information to a file"""
        job_info_path = Path("build") / "ota-job-runs" / f"{job_info_name}.json"
        job_info_path.parent.mkdir(parents=True, exist_ok=True)
        with job_info_path.open("w") as f:
            json.dump(job_info, f, indent=2)
        ota_info(f"Job information saved to: {job_info_path}")

    def _get_job_target(self, thing_name=None, thing_group_name=None):
        """Get the target ARN and name for the job"""
        if thing_group_name:
            target_arn = f"arn:aws:iot:{self.region}:{self.account_id}:thinggroup/{thing_group_name}"
            target_name = thing_group_name
        elif thing_name:
            target_arn = (
                f"arn:aws:iot:{self.region}:{self.account_id}:thing/{thing_name}"
            )
            target_name = thing_name
        else:
            raise ValueError("Either thing_name or thing_group_name must be provided")
        return target_arn, target_name

    def _process_files_and_create_stream(
        self, files_config: OTAFilesConfig, stream_description: str
    ) -> tuple[str, list[dict]]:
        """
        Process files and create a stream.
        Returns (stream ID, processed files info)
        """
        # Validate files configuration
        files_config.validate()

        # Get stream ID
        stream_id = self.generate_stream_id()

        # Process files for stream
        stream_files_info, processed_files_info = self.process_files_for_stream(
            stream_id, files_config
        )
        if not stream_files_info or not processed_files_info:
            ota_error("Failed to process files for OTA job")
            return None, None

        # Create stream
        stream_id = self.create_stream(stream_id, stream_files_info, stream_description)
        if not stream_id:
            ota_error("Failed to create stream for OTA job")
            return None, None

        return stream_id, processed_files_info

    def create_mock_job_with_document(
        self, document: dict, thing_name=None, thing_group_name=None
    ) -> Optional[str]:
        """
        Create a mock OTA job with a given job document.
        No files are uploaded to S3, so this should not be used for actual OTA jobs.
        This is to test error handling for job documents that are not supported by the OTA agent.

        Args:
            document (dict): Job document
            thing_name (str, optional): Target thing name.
            thing_group_name (str, optional): Target thing group name.

        Returns:
            str: Job ID if successful, None otherwise
        """
        target_arn, target_name = self._get_job_target(thing_name, thing_group_name)

        ota_info(f"Creating mock OTA job for target: {target_name}")
        ota_info(f"Job document: {document}")

        try:
            # Generate unique job ID
            job_id = f"AFR_OTA-mock-{int(time.time())}-{uuid.uuid4().hex[:8]}"

            # Prepare arguments for create_job API
            create_job_args = {
                "jobId": job_id,
                "targets": [target_arn],
                "document": json.dumps(document),
                "description": f"Mock OTA job for {target_name} with custom document",
                "targetSelection": "SNAPSHOT",
                "jobExecutionsRolloutConfig": {"maximumPerMinute": 1},
            }

            response = self.iot_client.create_job(**create_job_args)

            ota_log(f"Created mock OTA job: {job_id}")
            ota_info(f"Job ARN: {response['jobArn']}")

            # Save job information
            job_info = {
                "job_id": job_id,
                "job_arn": response["jobArn"],
                "target": target_name,
                "job_document": document,
                "created_at": datetime.now(timezone.utc).isoformat(),
                "job_type": "mock_ota",
            }

            self.save_job_info(job_info, f"mock-job-{job_id}")

            self.created_jobs.append({"id": job_id, "is_ota_update": False})

            return job_id

        except Exception as e:
            ota_error(f"Error creating mock OTA job: {e}")
            return None

    def create_ota_job(
        self, files_config: OTAFilesConfig, thing_name=None, thing_group_name=None
    ):
        """Create standard FreeRTOS OTA job using create-ota-update API

        This method follows the AWS CLI workflow documented at:
        https://docs.aws.amazon.com/freertos/latest/userguide/ota-cli-workflow.html

        The workflow consists of three main steps:
        1. Digitally sign your firmware update (handled in process_files_for_stream)
        2. Create a stream of your firmware update (create_stream)
        3. Start an OTA update job (create_ota_update API)

        Args:
            files_config (OTAFilesConfig): Files configuration
            thing_name (str, optional): Target thing name.
            thing_group_name (str, optional): Target thing group name.

        Returns:
            str: OTA Update ID if successful, None otherwise
        """
        target_arn, target_name = self._get_job_target(thing_name, thing_group_name)

        ota_info(f"Creating standard FreeRTOS OTA job for target: {target_name}")
        ota_info(
            "Following AWS CLI workflow: https://docs.aws.amazon.com/freertos/latest/userguide/ota-cli-workflow.html"
        )

        try:
            # Process files and create stream
            stream_id, processed_files_info = self._process_files_and_create_stream(
                files_config, f"OTA stream for {target_name} firmware update"
            )
            if not stream_id or not processed_files_info:
                ota_error("Failed to process files and create stream for OTA job")
                return None

            # Prepare files for OTA update (with proper AFR OTA format)
            ota_files = []
            for processed_file in processed_files_info:
                original_info = processed_file["original_info"]
                signing_job_id = processed_file["signing_job_id"]

                file_id = original_info["file_id"]  # Now integer 0-255
                signature = original_info.get("signature")
                hash_algorithm = original_info.get("hash_algorithm", "SHA256")
                signing_algorithm = original_info.get("signing_algorithm", "ECDSA")

                ota_file = {
                    "fileName": original_info["name"],
                    "fileLocation": {
                        "stream": {"streamId": stream_id, "fileId": file_id}
                    },
                }

                # Handle signing based on signing field and signature availability
                if original_info.get("signing", 0) == 1:
                    if signing_job_id:
                        # AWS Code Signer was used
                        ota_file["codeSigning"] = {"awsSignerJobId": signing_job_id}
                    elif signature:
                        # User provided custom signature - AWS expects double base64 encoding
                        # The user provides a base64 signature, but AWS expects base64(base64_signature)
                        import base64

                        double_encoded_signature = base64.b64encode(
                            signature.encode("utf-8")
                        ).decode("utf-8")
                        ota_file["codeSigning"] = {
                            "customCodeSigning": {
                                "signature": {
                                    "inlineDocument": double_encoded_signature  # Double base64 encoded signature
                                },
                                "certificateChain": {
                                    "inlineDocument": "",  # Certificate chain if needed
                                    "certificateName": "/ota-test-cert",
                                },
                                "hashAlgorithm": hash_algorithm,
                                "signatureAlgorithm": signing_algorithm,
                            }
                        }
                    else:
                        ota_warn(
                            f"File {original_info['name']} marked for signing but no signature provided"
                        )
                else:
                    # For unsigned files, do not include codeSigning section. Verified against
                    # the live API: AWS emits "sig--": null (empty hash, empty algorithm) and
                    # omits "certfile" entirely. It never emits "sig-ecdsa-sha256" — the key is
                    # always sig-{hashAlgorithm}-{signatureAlgorithm}, hash first.
                    #
                    # Note this document is NOT parseable by the device: job_parser.c
                    # populateCommonFields() requires both certfile and sig-sha256-ecdsa, so an
                    # authentic AWS unsigned job is REJECTED. create_custom_job() injects
                    # placeholders for that reason.
                    ota_info(
                        f"File {original_info['name']} will be delivered unsigned "
                        "(generates sig--:null and no certfile in job document)"
                    )

                ota_files.append(ota_file)

            # Generate unique OTA update ID
            ota_update_id = f"ota-update-{int(time.time())}-{uuid.uuid4().hex[:8]}"

            # Prepare create_ota_update API arguments
            create_ota_args = {
                "otaUpdateId": ota_update_id,
                "description": f"OTA firmware update for {target_name}",
                "targets": [target_arn],
                "protocols": ["MQTT"],
                "targetSelection": "SNAPSHOT",
                "awsJobExecutionsRolloutConfig": {"maximumPerMinute": 1},
                "files": ota_files,
                "roleArn": f"arn:aws:iam::{self.account_id}:role/{self.ota_role_name}",
            }

            # Step 3: Create OTA update using create-ota-update API
            ota_info("Step 3: Creating OTA update job")
            ota_info(f"Creating OTA update: {ota_update_id}")

            response = self.iot_client.create_ota_update(**create_ota_args)

            ota_log(f"Created standard OTA job: {ota_update_id}")
            ota_info(f"OTA Update ARN: {response['otaUpdateArn']}")
            ota_info("All three AWS CLI workflow steps completed successfully")

            # AWS IoT Job ID might not be immediately available
            aws_iot_job_id = response.get("awsIotJobId", "Not yet available")
            if aws_iot_job_id != "Not yet available":
                ota_info(f"AWS IoT Job ID: {aws_iot_job_id}")
            else:
                ota_info(
                    "AWS IoT Job ID will be generated by AWS (check with get-ota-update)"
                )

            # Save OTA job information
            ota_job_info = {
                "ota_update_id": ota_update_id,
                "ota_update_arn": response["otaUpdateArn"],
                "aws_iot_job_id": aws_iot_job_id,
                "target": target_name,
                "stream_id": stream_id,
                "files": ota_files,
                "processed_files": processed_files_info,
                "create_ota_args": create_ota_args,  # Save API arguments for debugging
                "created_at": datetime.now(timezone.utc).isoformat(),
                "job_type": "standard_ota",
            }

            self.save_job_info(ota_job_info, f"ota-update-{ota_update_id}")

            # Print job document signature field behavior info
            ota_info("Job document signature field behavior:")
            ota_info("  - Signed files: 'sig-{hash}-{algorithm}' with base64 signature")
            ota_info(
                "  - Unsigned files: 'sig--':null and no 'certfile' (AWS default behavior)"
            )
            ota_info(
                "  - Custom signatures: AWS expects double base64 encoding in API call"
            )
            ota_info(
                f"  - View job document: aws iot get-job-document --job-id AFR_OTA-{ota_update_id}"
            )

            self.created_jobs.append({"id": ota_update_id, "is_ota_update": True})

            return ota_update_id

        except Exception as e:
            ota_error(f"Error creating OTA job: {e}")
            return None

    def create_custom_job(
        self,
        files_config: OTAFilesConfig,
        thing_name=None,
        thing_group_name=None,
        job_doc_extension=None,
        job_config_path=None,
    ):
        """Create custom OTA job with stream-based file delivery and FreeRTOS-compatible job document

        This method creates a job document that is compatible with the format expected by
        the FreeRTOS OTA agent. It mimics the job document structure created by the
        `create-ota-update` API, as demonstrated in the `create_ota_update.py` script.

        Args:
            files_config (OTAFilesConfig): Files configuration
            thing_name (str, optional): Target thing name.
            thing_group_name (str, optional): Target thing group name.
            job_doc_extension (dict, optional): Extension for the job document. This will be merged with the existing job document.
            - Avoid the 'afr_ota' key, as it will be overridden by the existing job document.
            job_config_path (str, optional): Path to a JSON file with advanced job configurations.

        Returns:
            str: Job ID if successful, None otherwise
        """
        target_arn, target_name = self._get_job_target(thing_name, thing_group_name)

        ota_info(f"Creating custom OTA job for target: {target_name}")

        try:
            # Load advanced job configuration if provided
            job_config = {}
            if job_config_path:
                if os.path.exists(job_config_path):
                    with open(job_config_path, "r") as f:
                        job_config = json.load(f)
                    ota_info(
                        f"Loaded advanced job configuration from {job_config_path}"
                    )
                else:
                    ota_warn(
                        f"Job config file not found: {job_config_path}, using defaults."
                    )

            # Process files and create stream
            stream_id, processed_files_info = self._process_files_and_create_stream(
                files_config, f"Custom job stream for {target_name}"
            )

            if not stream_id or not processed_files_info:
                ota_error("Failed to process files and create stream for custom job")
                return None

            # Prepare files for job document (with string file IDs)
            afr_ota_files = []
            for processed_file in processed_files_info:
                original_info = processed_file["original_info"]
                s3_location = processed_file["s3_location"]
                needs_signing = processed_file["needs_signing"]

                file_entry = {
                    "filepath": original_info.get("filepath", original_info["name"]),
                    "filesize": s3_location["size"],
                    "fileid": original_info["file_id"],
                    # Always emitted, including for unsigned files. AWS itself only emits
                    # certfile when the file is signed (it comes from the code-signing
                    # certificateChain.certificateName), but job_parser.c treats certfile as
                    # mandatory, so omitting it would get the job REJECTED. See the unsigned
                    # branch below.
                    "certfile": "/ota-test-cert",
                }

                if needs_signing:
                    signed_object_info = self.get_signature_from_signed_object(
                        processed_file["signed_s3_location"]
                    )

                    if not signed_object_info:
                        ota_error(
                            f"Could not retrieve signature for {original_info['name']}"
                        )
                        continue

                    sig_key = f"sig-{signed_object_info['signatureAlgorithm'].replace('with', '-').lower()}"
                    # # Apply the same double base64 encoding as standard OTA jobs
                    # # FreeRTOS agent expects signatures in job documents to be double base64 encoded
                    # import base64
                    # raw_signature = signed_object_info["signature"]
                    # if isinstance(raw_signature, str):
                    #     double_encoded_signature = base64.b64encode(raw_signature.encode('utf-8')).decode('utf-8')
                    # else:
                    #     # If it's already bytes, encode it
                    #     double_encoded_signature = base64.b64encode(raw_signature).decode('utf-8')

                    # file_entry[sig_key] = double_encoded_signature
                    file_entry[sig_key] = signed_object_info["signature"]
                else:
                    # NOT what AWS emits. Real create-ota-update on an unsigned file produces
                    # "sig--": null and omits certfile entirely (verified against the live API).
                    # The Jobs parser (job_parser.c populateCommonFields) requires both certfile
                    # and sig-sha256-ecdsa, so an authentic AWS unsigned document is REJECTED by
                    # the device. Inject placeholders so unsigned test jobs are parseable.
                    file_entry["sig-sha256-ecdsa"] = "N/A"

                afr_ota_files.append(file_entry)

            # Create FreeRTOS-compatible job document
            job_doc = {
                "afr_ota": {
                    "protocols": ["MQTT"],
                    "streamname": stream_id,
                    "files": afr_ota_files,
                }
            }

            # Merge job document extension if provided
            if job_doc_extension and isinstance(job_doc_extension, dict):
                if "afr_ota" in job_doc_extension:
                    ota_error(
                        "The 'afr_ota' key is not allowed in the job document extension"
                    )
                    return None
                job_doc.update(job_doc_extension)

            # Auto-inject the image MD5 into rmng_ota so the device can auto-resume an interrupted
            # download and verify integrity on completion. Only for single-file (firmware) jobs,
            # since rmng_ota.file_md5 describes one image. An explicitly-set file_md5 is preserved.
            if isinstance(job_doc.get("rmng_ota"), dict):
                if "file_md5" not in job_doc["rmng_ota"]:
                    if len(processed_files_info) == 1:
                        file_md5 = processed_files_info[0]["s3_location"].get("md5")
                        if file_md5:
                            job_doc["rmng_ota"]["file_md5"] = file_md5
                        else:
                            ota_warn(
                                "Could not determine image MD5; job will not support auto-resume"
                            )
                    else:
                        ota_warn(
                            "Multiple files in job; skipping rmng_ota.file_md5 auto-injection "
                            "(auto-resume unavailable)"
                        )

            # Generate unique job ID
            job_id = f"AFR_OTA-custom-{int(time.time())}-{uuid.uuid4().hex[:8]}"

            # Create IoT job

            # Prepare arguments for create_job API
            create_job_args = {
                "jobId": job_id,
                "targets": [target_arn],
                "document": json.dumps(job_doc),
                "description": f"Custom FreeRTOS-compatible OTA job for {target_name}",
            }

            # Set default values that can be overridden by job_config
            # These are commonly used in OTA scenarios
            defaults = {
                "targetSelection": "SNAPSHOT",
                "jobExecutionsRolloutConfig": {"maximumPerMinute": 1},
                "presignedUrlConfig": {
                    "roleArn": f"arn:aws:iam::{self.account_id}:role/{self.ota_role_name}",
                    "expiresInSec": 3600,
                },
            }

            # Merge defaults with provided config
            for key, value in defaults.items():
                if key not in job_config:
                    job_config[key] = value

            # Add allowed advanced configurations from the job_config file
            # See: https://boto3.amazonaws.com/v1/documentation/api/latest/reference/services/iot/client/create_job.html
            allowed_configs = [
                "presignedUrlConfig",
                "targetSelection",
                "jobExecutionsRolloutConfig",
                "abortConfig",
                "timeoutConfig",
                "jobExecutionsRetryConfig",
                "schedulingConfig",
            ]

            for key in allowed_configs:
                if key in job_config:
                    create_job_args[key] = job_config[key]
                    ota_info(f"Applying advanced job config for: {key}")

            response = self.iot_client.create_job(**create_job_args)

            ota_log(f"Created custom job: {job_id}")
            ota_info(f"Job ARN: {response['jobArn']}")
            ota_info(f"Using stream: {stream_id}")

            # Save job information
            job_info = {
                "job_id": job_id,
                "job_arn": response["jobArn"],
                "target": target_name,
                "stream_id": stream_id,
                "job_document": job_doc,
                "processed_files": processed_files_info,
                "created_at": datetime.now(timezone.utc).isoformat(),
                "job_type": "custom_afr_ota",
            }

            # Save advanced job configurations if they were used
            if job_config:
                job_info["advanced_config"] = job_config

            self.save_job_info(job_info, f"ota-job-{job_id}")

            self.created_jobs.append({"id": job_id, "is_ota_update": False})

            return job_id

        except Exception as e:
            ota_error(f"Error creating custom job: {e}")
            return None

    def create_rmng_ota_job(
        self,
        files_config: OTAFilesConfig,
        ota_info: RmngOtaInfo,
        thing_name=None,
        thing_group_name=None,
    ):
        """Create an RMNG OTA job"""
        return self.create_custom_job(
            files_config=files_config,
            thing_name=thing_name,
            thing_group_name=thing_group_name,
            job_doc_extension={"rmng_ota": ota_info.to_dict()},
        )

    def start_job(self, job_id):
        """Start an OTA job and monitor its status (handles both OTA updates and custom jobs)"""
        ota_info(f"Starting/monitoring job: {job_id}")

        try:
            # Determine if this is an OTA update or regular job
            is_ota_update = job_id.startswith("ota-update-")

            if is_ota_update:
                self.monitor_ota_update(job_id)
            else:
                self.monitor_regular_job(job_id)

        except Exception as e:
            ota_error(f"Error starting/monitoring job: {e}")

    def monitor_ota_update(self, ota_update_id):
        """Monitor an OTA update created by create-ota-update API"""
        try:
            ota_info(f"Monitoring OTA update: {ota_update_id}")

            # Get OTA update details
            response = self.iot_client.get_ota_update(otaUpdateId=ota_update_id)
            ota_update_info = response["otaUpdateInfo"]

            ota_info(f"OTA Update Status: {ota_update_info['otaUpdateStatus']}")
            ota_info(f"Description: {ota_update_info.get('description', 'N/A')}")

            # Get the auto-generated AWS IoT Job ID
            aws_iot_job_id = ota_update_info.get("awsIotJobId")
            if aws_iot_job_id:
                ota_info(f"Auto-generated AWS IoT Job ID: {aws_iot_job_id}")

                # Monitor the underlying IoT job
                self.monitor_regular_job(aws_iot_job_id)
            else:
                ota_warn(
                    "AWS IoT Job ID not yet available, monitoring OTA update status only"
                )

                # Monitor OTA update status
                for i in range(60):  # Monitor for up to 5 minutes
                    response = self.iot_client.get_ota_update(otaUpdateId=ota_update_id)
                    status = response["otaUpdateInfo"]["otaUpdateStatus"]

                    ota_info(f"OTA Update Status: {status}")

                    if status in [
                        "CREATE_COMPLETE",
                        "CREATE_FAILED",
                        "DELETE_COMPLETE",
                    ]:
                        ota_log(f"OTA update completed with status: {status}")
                        return

                    time.sleep(5)

                ota_warn("OTA update monitoring timeout reached")

        except Exception as e:
            ota_error(f"Error monitoring OTA update: {e}")

    def monitor_regular_job(self, job_id):
        """Monitor a regular IoT job created by create-job API"""
        try:
            # Get job details
            response = self.iot_client.describe_job(jobId=job_id)
            job = response["job"]

            ota_info(f"IoT Job Status: {job['status']}")
            ota_info(f"Job Description: {job.get('description', 'N/A')}")

            # Get job executions
            executions_response = self.iot_client.list_job_executions_for_job(
                jobId=job_id
            )
            executions = executions_response.get("executionSummaries", [])

            if not executions:
                ota_warn("No job executions found")
                return

            # Monitor job execution
            ota_info("Monitoring job execution...")

            for i in range(60):  # Monitor for up to 5 minutes
                for execution in executions:
                    thing_name = execution["thingArn"].split("/")[-1]

                    # Get detailed execution info
                    exec_response = self.iot_client.describe_job_execution(
                        jobId=job_id, thingName=thing_name
                    )

                    execution_detail = exec_response["execution"]
                    status = execution_detail["status"]

                    ota_info(f"Thing {thing_name}: {status}")

                    if "statusDetails" in execution_detail:
                        details = execution_detail["statusDetails"]
                        if "detailsMap" in details:
                            for key, value in details["detailsMap"].items():
                                ota_info(f"  {key}: {value}")

                    if status in ["SUCCEEDED", "FAILED", "REJECTED", "CANCELED"]:
                        ota_log(f"Job execution completed with status: {status}")
                        return

                time.sleep(5)  # Wait 5 seconds before next check

            ota_warn("Job monitoring timeout reached")

        except Exception as e:
            ota_error(f"Error monitoring regular job: {e}")

    def track_job_execution(
        self, job_id, thing_name, poll_interval=5, max_attempts=60
    ) -> Generator[OTAJobStatus, None, None]:
        """Poll and print a job execution for a specific thing"""
        # Translate OTA Update ID to underlying AWS IoT Job ID if needed
        aws_job_id = job_id
        if job_id.startswith("ota-update-"):
            try:
                ota_info(f"Resolving OTA update ID '{job_id}' to AWS IoT Job ID")
                ota_update_resp = self.iot_client.get_ota_update(otaUpdateId=job_id)
                aws_job_id = ota_update_resp.get("otaUpdateInfo", {}).get(
                    "awsIotJobId", job_id
                )
                ota_info(f"Using AWS IoT Job ID: {aws_job_id}")
            except ClientError as e:
                ota_warn(f"Could not resolve OTA update ID to AWS job ID: {e}")
            except Exception as e:
                ota_warn(f"Unexpected error resolving OTA update ID: {e}")

        ota_info(
            f"Tracking job execution for thing '{thing_name}' on job '{aws_job_id}'"
        )
        terminal_states = [
            "SUCCEEDED",
            "FAILED",
            "REJECTED",
            "CANCELED",
            "TIMED_OUT",
            "REMOVED",
        ]

        last_status = None
        try:
            for _ in range(max_attempts):
                response = self.iot_client.describe_job_execution(
                    jobId=aws_job_id, thingName=thing_name
                )
                execution = response.get("execution", {})

                status = execution.get("status", "UNKNOWN")
                details_map = (
                    execution.get("statusDetails", {}).get("detailsMap", {}) or {}
                )
                last_updated = execution.get("lastUpdatedAt")

                if (
                    last_status is None
                    or last_status.status != status
                    or last_status.details != details_map
                ):
                    last_status = OTAJobStatus(status, details_map, last_updated)
                    yield last_status

                if last_status.status in terminal_states:
                    ota_log(
                        f"Job execution reached terminal state: {last_status.status}"
                    )
                    return

                time.sleep(poll_interval)

            ota_warn("Job execution tracking timeout reached")

        except ClientError as e:
            if e.response["Error"]["Code"] == "ResourceNotFoundException":
                ota_error(
                    f"Job execution not found for job '{job_id}' and thing '{thing_name}'"
                )
            else:
                ota_error(f"Error tracking job execution: {e}")
        except Exception as e:
            ota_error(f"Unexpected error while tracking job execution: {e}")

    def wait_for_execution_status(
        self,
        job_id: str,
        thing_name: str,
        status_str: str,
        expected_details_check: Optional[Callable[[dict], bool]] = None,
        poll_interval: int = 5,
        max_attempts: int = 120,
    ) -> Optional[OTAJobStatus]:
        """Wait for the execution status of an OTA job to be the given status.
        Returns the job status if found, otherwise None.
        If expected_details_check is provided, it will be called with the job status details and should return True if the details are as expected, False otherwise.
        """
        for status in self.track_job_execution(
            job_id, thing_name, poll_interval, max_attempts
        ):
            if status.status == status_str:
                if expected_details_check is None or expected_details_check(
                    status.details
                ):
                    return status
        return None

    def cancel_custom_job(self, job_id, force=False):
        """Cancel a custom IoT job created by create-job API

        Args:
            job_id (str): Job ID to cancel
            force (bool): Force cancellation even if job has already completed

        Returns:
            bool: True if successful, False otherwise
        """
        try:
            ota_info(f"Canceling custom IoT job: {job_id}")

            # Check job status first
            try:
                response = self.iot_client.describe_job(jobId=job_id)
                job_status = response["job"]["status"]
                ota_info(f"Current job status: {job_status}")

                if job_status in ["COMPLETED", "CANCELED", "DELETION_IN_PROGRESS"]:
                    ota_warn(f"Job is already in {job_status} state")
                    if not force:
                        return True

            except ClientError as e:
                if e.response["Error"]["Code"] == "ResourceNotFoundException":
                    ota_error(f"Job {job_id} not found")
                    return False
                else:
                    raise

            # Cancel the job
            try:
                self.iot_client.cancel_job(
                    jobId=job_id, comment="Canceled by OTA test framework", force=force
                )
                ota_log(f"Successfully canceled custom job: {job_id}")
                return True

            except ClientError as e:
                if e.response["Error"]["Code"] == "InvalidStateTransitionException":
                    ota_error(f"Cannot cancel job {job_id} in current state")
                    if not force:
                        ota_info("Try with --force flag to force cancellation")
                    return False
                else:
                    raise

        except Exception as e:
            ota_error(f"Error canceling custom job {job_id}: {e}")
            return False

    def cancel_ota_job(self, ota_update_id, force=False):
        """Cancel an OTA update created by create-ota-update API

        Args:
            ota_update_id (str): OTA Update ID to cancel
            force (bool): Force cancellation

        Returns:
            bool: True if successful, False otherwise
        """
        try:
            ota_info(f"Canceling OTA update: {ota_update_id}")

            # Get OTA update info
            try:
                response = self.iot_client.get_ota_update(otaUpdateId=ota_update_id)
                ota_update_info = response["otaUpdateInfo"]
                ota_status = ota_update_info["otaUpdateStatus"]
                ota_info(f"Current OTA update status: {ota_status}")

                # Get the underlying IoT job ID
                aws_iot_job_id = ota_update_info.get("awsIotJobId")

                if ota_status in ["CREATE_COMPLETE", "CREATE_IN_PROGRESS"]:
                    # Cancel the underlying IoT job
                    if aws_iot_job_id:
                        ota_info(f"Canceling underlying IoT job: {aws_iot_job_id}")
                        try:
                            self.iot_client.cancel_job(
                                jobId=aws_iot_job_id,
                                comment="Canceled OTA update",
                                force=force,
                            )
                            ota_log(
                                f"Successfully canceled underlying IoT job: {aws_iot_job_id}"
                            )
                        except ClientError as job_error:
                            ota_warn(f"Error canceling underlying job: {job_error}")

                    ota_log(f"Successfully canceled OTA update: {ota_update_id}")
                    return True
                else:
                    ota_warn(f"OTA update is in {ota_status} state, cannot cancel")
                    return False

            except ClientError as e:
                if e.response["Error"]["Code"] == "ResourceNotFoundException":
                    ota_error(f"OTA update {ota_update_id} not found")
                    return False
                else:
                    raise

        except Exception as e:
            ota_error(f"Error canceling OTA update {ota_update_id}: {e}")
            return False

    def list_ota_jobs(self):
        """List all OTA-related jobs and updates

        Returns:
            dict: Dictionary with 'ota_updates' and 'custom_jobs' lists
        """
        ota_jobs = {"ota_updates": [], "custom_jobs": []}

        try:
            # List OTA updates
            try:
                response = self.iot_client.list_ota_updates()
                ota_jobs["ota_updates"] = response.get("otaUpdates", [])
            except Exception as e:
                ota_warn(f"Error listing OTA updates: {e}")

            # List all jobs and filter for custom OTA jobs
            try:
                response = self.iot_client.list_jobs()
                all_jobs = response.get("jobs", [])

                # Filter for AFR_OTA jobs (custom jobs)
                for job in all_jobs:
                    job_id = job["jobId"]
                    if job_id.startswith("AFR_OTA-"):
                        ota_jobs["custom_jobs"].append(job)

            except Exception as e:
                ota_warn(f"Error listing jobs: {e}")

        except Exception as e:
            ota_error(f"Error listing OTA jobs: {e}")

        return ota_jobs

    def cancel_all_jobs(self):
        """Cancel all OTA-related jobs and updates"""
        ota_info("Canceling all OTA jobs and updates...")

        try:
            ota_jobs = self.list_ota_jobs()

            # Cancel OTA updates
            for ota_update in ota_jobs["ota_updates"]:
                ota_update_id = ota_update["otaUpdateId"]
                status = ota_update.get("otaUpdateStatus", "UNKNOWN")
                ota_info(f"Found OTA update: {ota_update_id} (status: {status})")
                if status in ["CREATE_COMPLETE", "CREATE_IN_PROGRESS"]:
                    self.cancel_ota_job(ota_update_id, force=True)

            # Cancel custom jobs
            for job in ota_jobs["custom_jobs"]:
                job_id = job["jobId"]
                status = job.get("status", "UNKNOWN")
                ota_info(f"Found custom OTA job: {job_id} (status: {status})")
                if status not in ["COMPLETED", "CANCELED", "DELETION_IN_PROGRESS"]:
                    self.cancel_custom_job(job_id, force=True)

            total_found = len(ota_jobs["ota_updates"]) + len(ota_jobs["custom_jobs"])
            if total_found > 0:
                ota_log(f"Processed {total_found} OTA jobs/updates for cancellation")
            else:
                ota_info("No active OTA jobs or updates found to cancel")

        except Exception as e:
            ota_warn(f"Error during job cancellation: {e}")

    # Statuses from which delete_job/delete_ota_update will succeed (or no-op).
    _JOB_DELETABLE_STATUSES = {"COMPLETED", "CANCELED", "DELETION_IN_PROGRESS"}
    _OTA_UPDATE_DELETABLE_STATUSES = {
        "CREATE_COMPLETE",
        "CREATE_FAILED",
        "DELETE_COMPLETE",
        "DELETE_IN_PROGRESS",
    }

    def _wait_for_terminal_status(self, job_id, is_ota_update, timeout=60, poll=3):
        """Poll until the job/OTA update reaches a status delete_job will accept.

        cancel_job returns immediately but the job lingers in CANCELING for a few seconds
        before becoming CANCELED. Calling delete_job in CANCELING raises — wait it out.
        Returns the final status string (may still be non-terminal on timeout, in which case
        the caller falls back to force=True).
        """
        deadline = time.time() + timeout
        last_status = "UNKNOWN"
        while time.time() < deadline:
            try:
                if is_ota_update:
                    info = self.iot_client.get_ota_update(otaUpdateId=job_id)[
                        "otaUpdateInfo"
                    ]
                    last_status = info["otaUpdateStatus"]
                    if last_status in self._OTA_UPDATE_DELETABLE_STATUSES:
                        return last_status
                else:
                    last_status = self.iot_client.describe_job(jobId=job_id)["job"][
                        "status"
                    ]
                    if last_status in self._JOB_DELETABLE_STATUSES:
                        return last_status
            except ClientError as e:
                if e.response["Error"]["Code"] == "ResourceNotFoundException":
                    return "NOT_FOUND"
                raise
            time.sleep(poll)
        ota_warn(
            f"Job {job_id} still in non-terminal status {last_status} after {timeout}s; "
            "will attempt force delete"
        )
        return last_status

    def delete_job(self, job_id):
        """Delete a job and its associated S3 files

        Args:
            job_id (str): Job ID to delete (OTA update ID or regular job ID)

        Returns:
            bool: True if successful, False otherwise
        """
        try:
            ota_info(f"Deleting job: {job_id}")

            # Determine if this is an OTA update or regular job
            is_ota_update = job_id.startswith("ota-update-")

            # Wait for cancel→terminal transition; delete_job rejects CANCELING state.
            status = self._wait_for_terminal_status(job_id, is_ota_update)
            if status == "NOT_FOUND":
                ota_info(
                    f"{'OTA update' if is_ota_update else 'Job'} {job_id} not found"
                )
                self._untrack_job(job_id)
                return False

            # Resolve underlying IoT job id (only meaningful for ota-update path)
            aws_iot_job_id = None
            if is_ota_update:
                try:
                    response = self.iot_client.get_ota_update(otaUpdateId=job_id)
                    aws_iot_job_id = response["otaUpdateInfo"].get("awsIotJobId")
                except ClientError as e:
                    if e.response["Error"]["Code"] != "ResourceNotFoundException":
                        ota_warn(f"Could not fetch OTA update info: {e}")
            else:
                aws_iot_job_id = job_id

            # Load job info to get processed files for S3 cleanup
            job_info_path = None
            if is_ota_update:
                job_info_path = (
                    Path("build") / "ota-job-runs" / f"ota-update-{job_id}.json"
                )
            else:
                job_info_path = (
                    Path("build") / "ota-job-runs" / f"ota-job-{job_id}.json"
                )

            processed_files = []
            stream_id_from_info = None
            if job_info_path.exists():
                try:
                    with job_info_path.open("r") as f:
                        job_info = json.load(f)
                        processed_files = job_info.get("processed_files", [])
                        stream_id_from_info = job_info.get("stream_id")
                        ota_info(
                            f"Found {len(processed_files)} files to delete from S3"
                        )
                except Exception as e:
                    ota_warn(f"Could not load job info file {job_info_path}: {e}")
            else:
                ota_warn(f"Job info file not found: {job_info_path}")

            # Delete S3 files — purge ALL versions + delete-markers for each key.
            # The signed/ keys carry at least the original signed JSON + the raw-binary copy;
            # deleting one version leaves the other behind on a versioned bucket.
            deleted_files = 0
            for processed_file in processed_files:
                s3_location = processed_file.get("s3_location", {})
                bucket = s3_location.get("bucket")
                key = s3_location.get("key")
                if bucket and key:
                    n = self._delete_all_object_versions(bucket, key)
                    if n:
                        deleted_files += 1
                        ota_info(f"Purged {n} version(s) of s3://{bucket}/{key}")
                    try:
                        self.uploaded_s3_keys.remove((bucket, key))
                    except ValueError:
                        pass

            if deleted_files > 0:
                ota_log(f"Cleaned {deleted_files} S3 keys")

            # Delete the job (force=True so we don't get blocked by lingering CANCELING state)
            job_delete_ok = True
            if is_ota_update:
                # Delete the underlying IoT job if it exists
                if aws_iot_job_id:
                    try:
                        _call_with_throttle_retry(
                            lambda: self.iot_client.delete_job(
                                jobId=aws_iot_job_id, force=True
                            ),
                            what=f"delete_job({aws_iot_job_id})",
                        )
                        ota_log(f"Deleted underlying IoT job: {aws_iot_job_id}")
                    except ClientError as e:
                        if e.response["Error"]["Code"] == "ResourceNotFoundException":
                            ota_info(f"Underlying IoT job {aws_iot_job_id} not found")
                        else:
                            ota_warn(
                                f"Error deleting underlying IoT job {aws_iot_job_id}: {e}"
                            )

                # Delete the OTA update
                try:
                    _call_with_throttle_retry(
                        lambda: self.iot_client.delete_ota_update(otaUpdateId=job_id),
                        what=f"delete_ota_update({job_id})",
                    )
                    ota_log(f"Deleted OTA update: {job_id}")
                except ClientError as e:
                    if e.response["Error"]["Code"] == "ResourceNotFoundException":
                        ota_info(f"OTA update {job_id} not found")
                    else:
                        ota_error(f"Error deleting OTA update {job_id}: {e}")
                        job_delete_ok = False
            else:
                try:
                    _call_with_throttle_retry(
                        lambda: self.iot_client.delete_job(jobId=job_id, force=True),
                        what=f"delete_job({job_id})",
                    )
                    ota_log(f"Deleted IoT job: {job_id}")
                except ClientError as e:
                    if e.response["Error"]["Code"] == "ResourceNotFoundException":
                        ota_info(f"Job {job_id} not found")
                    else:
                        ota_error(f"Error deleting job {job_id}: {e}")
                        job_delete_ok = False

            # Delete the per-job stream (custom job path only — OTA updates manage their own).
            if not is_ota_update and stream_id_from_info:
                self.delete_stream(stream_id_from_info)

            # Delete job info file
            if job_info_path and job_info_path.exists():
                try:
                    job_info_path.unlink()
                    ota_info(f"Deleted job info file: {job_info_path}")
                except Exception as e:
                    ota_warn(f"Error deleting job info file {job_info_path}: {e}")

            if job_delete_ok:
                self._untrack_job(job_id)
                ota_log(f"Successfully deleted job: {job_id}")
            return job_delete_ok

        except Exception as e:
            ota_error(f"Error deleting job {job_id}: {e}")
            return False

    def _untrack_job(self, job_id):
        self.created_jobs = [j for j in self.created_jobs if j["id"] != job_id]

    def _delete_all_object_versions(self, bucket: str, key: str) -> int:
        """Delete every version + delete-marker for an exact S3 key.

        OTA bucket has versioning enabled (`get_signature_from_signed_object` relies on
        `list_object_versions`). A plain delete_object on a versioned bucket either drops
        a single version (if VersionId given) or just adds a delete-marker — older
        versions persist. The post-sign workflow leaves at least 2 versions on signed/
        keys (signed JSON + raw binary copy). This helper purges all of them.
        """
        deleted = 0
        try:
            paginator = self.s3_client.get_paginator("list_object_versions")
            for page in paginator.paginate(Bucket=bucket, Prefix=key):
                entries = []
                for v in page.get("Versions", []) or []:
                    if v.get("Key") == key:
                        entries.append({"Key": key, "VersionId": v["VersionId"]})
                for m in page.get("DeleteMarkers", []) or []:
                    if m.get("Key") == key:
                        entries.append({"Key": key, "VersionId": m["VersionId"]})
                # delete_objects accepts max 1000 per request
                for i in range(0, len(entries), 1000):
                    chunk = entries[i : i + 1000]
                    if not chunk:
                        continue
                    self.s3_client.delete_objects(
                        Bucket=bucket, Delete={"Objects": chunk, "Quiet": True}
                    )
                    deleted += len(chunk)
        except ClientError as e:
            ota_warn(f"list_object_versions failed for s3://{bucket}/{key}: {e}")
        except Exception as e:
            ota_warn(f"Error purging versions for s3://{bucket}/{key}: {e}")
        return deleted

    def cleanup_created_resources(self, pace_seconds: float = 6.0):
        """Best-effort cleanup of resources created by THIS process.

        Only touches jobs/streams/S3 keys explicitly tracked during this run, so it is safe
        to invoke on a shared AWS account. Paces job deletions to stay under AWS IoT's
        per-account delete-job rate limit (≈10/min); the per-call throttle retry handles bursts.
        """
        if not (self.created_jobs or self.created_streams or self.uploaded_s3_keys):
            return

        ota_info(
            f"Cleaning up tracked resources: {len(self.created_jobs)} jobs, "
            f"{len(self.created_streams)} streams, {len(self.uploaded_s3_keys)} S3 objects"
        )

        # Cancel + delete jobs
        for entry in list(self.created_jobs):
            job_id = entry["id"]
            is_ota_update = entry["is_ota_update"]
            try:
                if is_ota_update:
                    self.cancel_ota_job(job_id, force=True)
                else:
                    self.cancel_custom_job(job_id, force=True)
            except Exception as e:
                ota_warn(f"Cancel failed for {job_id} (continuing): {e}")
            try:
                self.delete_job(job_id)
            except Exception as e:
                ota_warn(f"Delete failed for {job_id}: {e}")
            time.sleep(pace_seconds)

        # Streams (delete_job above already removed per-job streams; this catches stragglers)
        for stream_id in list(self.created_streams):
            self.delete_stream(stream_id)

        # S3 objects — delete_job above already cleaned ones referenced by job_info; this
        # mops up anything orphaned (e.g., upload succeeded but job creation failed,
        # or signing succeeded but copy failed). Purge all versions on the versioned bucket.
        for bucket, key in list(self.uploaded_s3_keys):
            n = self._delete_all_object_versions(bucket, key)
            if n:
                ota_log(f"Purged {n} version(s) of tracked s3://{bucket}/{key}")
        self.uploaded_s3_keys.clear()

    def destroy_infrastructure(self):
        """Clean up OTA infrastructure"""
        ota_info("Destroying OTA infrastructure...")

        try:
            # Cancel all running jobs first
            self.cancel_all_jobs()

            # Clean up streams
            self.cleanup_all_streams()

            # Delete S3 bucket contents and bucket
            self.delete_s3_bucket()

            # Cancel Code Signer profile
            self.cancel_code_signer_profile()

            # Delete certificates
            self.delete_certificates()

            # Delete IAM role
            self.delete_ota_iam_role()

            ota_log("OTA infrastructure cleanup completed!")

        except Exception as e:
            ota_error(f"Infrastructure cleanup failed: {e}")

    def delete_s3_bucket(self):
        """Delete S3 bucket and all its contents"""
        try:
            # Check if bucket exists
            try:
                self.s3_client.head_bucket(Bucket=self.ota_bucket_name)
            except ClientError as e:
                if e.response["Error"]["Code"] == "404":
                    ota_info(f"S3 bucket {self.ota_bucket_name} doesn't exist")
                    return
                raise

            # Delete all objects and versions
            paginator = self.s3_client.get_paginator("list_object_versions")

            for page in paginator.paginate(Bucket=self.ota_bucket_name):
                objects_to_delete = []

                # Add versions
                for version in page.get("Versions", []):
                    objects_to_delete.append(
                        {"Key": version["Key"], "VersionId": version["VersionId"]}
                    )

                # Add delete markers
                for marker in page.get("DeleteMarkers", []):
                    objects_to_delete.append(
                        {"Key": marker["Key"], "VersionId": marker["VersionId"]}
                    )

                # Delete objects in batches
                if objects_to_delete:
                    self.s3_client.delete_objects(
                        Bucket=self.ota_bucket_name,
                        Delete={"Objects": objects_to_delete},
                    )

            # Delete bucket
            self.s3_client.delete_bucket(Bucket=self.ota_bucket_name)
            ota_log(f"Deleted S3 bucket: {self.ota_bucket_name}")

        except Exception as e:
            ota_error(f"Error deleting S3 bucket: {e}")

    def cancel_code_signer_profile(self):
        """Cancel Code Signer profile"""
        try:
            try:
                self.signer_client.get_signing_profile(
                    profileName=self.code_signer_profile_name
                )
                self.signer_client.cancel_signing_profile(
                    profileName=self.code_signer_profile_name
                )
                ota_info(
                    f"Code Signer profile {self.code_signer_profile_name} cancelled"
                )
            except ClientError as e:
                if e.response["Error"]["Code"] == "ResourceNotFoundException":
                    ota_info(
                        f"Code Signer profile {self.code_signer_profile_name} doesn't exist"
                    )
                else:
                    raise

        except Exception as e:
            ota_error(f"Error checking Code Signer profile: {e}")

    def delete_certificates(self):
        """Delete certificates from ACM"""
        try:
            # List certificates to find OTA test certificates
            response = self.acm_client.list_certificates()

            for cert in response.get("CertificateSummaryList", []):
                domain_name = cert.get("DomainName", "")
                if domain_name.startswith("ota-test-cert-"):
                    cert_arn = cert["CertificateArn"]
                    try:
                        self.acm_client.delete_certificate(CertificateArn=cert_arn)
                        ota_log(f"Deleted certificate: {domain_name}")
                    except ClientError as e:
                        if e.response["Error"]["Code"] == "ResourceInUseException":
                            ota_warn(
                                f"Certificate {domain_name} is in use, cannot delete"
                            )
                        else:
                            ota_warn(f"Error deleting certificate {domain_name}: {e}")

        except Exception as e:
            ota_error(f"Error deleting certificates: {e}")

    def delete_ota_iam_role(self):
        """Delete OTA IAM role"""
        try:
            try:
                # Detach managed policies
                policies = [
                    "arn:aws:iam::aws:policy/service-role/AWSIoTThingsRegistration",
                    "arn:aws:iam::aws:policy/service-role/AWSIoTLogging",
                    "arn:aws:iam::aws:policy/AWSIoTOTAUpdate",
                ]

                for policy_arn in policies:
                    try:
                        self.iam_client.detach_role_policy(
                            RoleName=self.ota_role_name, PolicyArn=policy_arn
                        )
                    except ClientError:
                        pass  # Policy might not be attached

                # Delete inline policies
                inline_policies = self.iam_client.list_role_policies(
                    RoleName=self.ota_role_name
                )
                for policy_name in inline_policies.get("PolicyNames", []):
                    try:
                        self.iam_client.delete_role_policy(
                            RoleName=self.ota_role_name, PolicyName=policy_name
                        )
                        ota_info(f"Deleted inline policy: {policy_name}")
                    except ClientError:
                        pass  # Policy might not exist

                # Delete role
                self.iam_client.delete_role(RoleName=self.ota_role_name)
                ota_log(f"Deleted IAM role: {self.ota_role_name}")

            except ClientError as e:
                if e.response["Error"]["Code"] == "NoSuchEntity":
                    ota_info(f"IAM role {self.ota_role_name} doesn't exist")
                else:
                    raise

        except Exception as e:
            ota_error(f"Error deleting IAM role: {e}")
