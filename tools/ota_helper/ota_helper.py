#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
AWS IoT OTA Testing Framework

This script provides comprehensive OTA (Over-The-Air) testing capabilities for AWS IoT devices.
It handles infrastructure setup, job creation, execution, cancellation, and cleanup.

Usage:
    python ota_helper.py --setup                    # Set up OTA infrastructure
    python ota_helper.py --create-ota-job --files <file_list.json> [--thing-name <name>]
    python ota_helper.py --create-custom-job --files <file_list.json> [--thing-name <name>]
    python ota_helper.py --create-rmng-ota-job --files <file_list.json> --job-config <config> [--thing-name <name>]
    python ota_helper.py --start-job --job-id <job_id>
    python ota_helper.py --track-job-execution --job-id <job_id> --thing-name <name>
    python ota_helper.py --cancel-custom-job --job-id <job_id> [--force]
    python ota_helper.py --cancel-ota-job --job-id <ota_update_id> [--force]
    python ota_helper.py --delete-job --job-id <job_id>
    python ota_helper.py --destroy                  # Clean up resources (cancels all running jobs)
"""

import os
import sys
import argparse
import json
from pathlib import Path

_COMMON_ROOT = Path(__file__).resolve().parents[1] / "common"
if str(_COMMON_ROOT) not in sys.path:
    sys.path.insert(0, str(_COMMON_ROOT))

from util.ota_aws import (  # noqa: E402
    OTAManager,
    OTAFilesConfig,
    RmngOtaInfo,
    RmngOtaDownloadWindow,
    ota_log,
    ota_error,
    ota_warn,
    ota_info,
)


def parse_args():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(
        description="AWS IoT OTA Testing Framework",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Set up OTA infrastructure
  python ota_helper.py --setup

  # Create standard FreeRTOS OTA job
  python ota_helper.py --create-ota-job \\
    --files templates/ota_file_template.json \\
    --thing-name thing_name

  # Create custom OTA job
  python ota_helper.py --create-custom-job \\
    --files templates/ota_file_template.json \\
    --job-config templates/job_config_template.json \\
    --thing-name thing_name

  # Create RMNG OTA job
  python ota_helper.py --create-rmng-ota-job \\
    --files templates/ota_file_template.json \\
    --job-config templates/job_config_template.json \\
    --thing-name thing_name

  # Start and monitor OTA job
  python ota_helper.py --start-job --job-id ota-update-12345

  # Track a job execution for a thing
  python ota_helper.py --track-job-execution --job-id ota-update-12345 --thing-name thing_name

  # Cancel a custom job
  python ota_helper.py --cancel-custom-job --job-id AFR_OTA-custom-12345

  # Cancel an OTA job
  python ota_helper.py --cancel-ota-job --job-id ota-update-12345

  # Delete a job and its S3 files
  python ota_helper.py --delete-job --job-id ota-update-12345

  # Force cancel a job
  python ota_helper.py --cancel-custom-job --job-id AFR_OTA-custom-12345 --force

  # Clean up infrastructure (cancels all jobs)
  python ota_helper.py --destroy
        """,
    )

    # Main operation modes
    parser.add_argument(
        "--setup",
        action="store_true",
        help="Set up OTA infrastructure (S3 bucket, certificates, Code Signer, IAM roles)",
    )
    parser.add_argument(
        "--create-ota-job",
        action="store_true",
        help="Create standard FreeRTOS OTA job using create-ota-update API",
    )
    parser.add_argument(
        "--create-custom-job",
        action="store_true",
        help="Create custom OTA job with custom job document",
    )
    parser.add_argument(
        "--create-rmng-ota-job",
        action="store_true",
        help="Create RMNG OTA job with RMNG-specific OTA info",
    )
    parser.add_argument(
        "--start-job", action="store_true", help="Start OTA job and monitor status"
    )
    parser.add_argument(
        "--track-job-execution",
        action="store_true",
        help="Poll and display a specific job execution for a thing",
    )
    parser.add_argument(
        "--cancel-custom-job", action="store_true", help="Cancel a custom IoT job"
    )
    parser.add_argument(
        "--cancel-ota-job", action="store_true", help="Cancel an OTA update"
    )
    parser.add_argument(
        "--delete-job",
        action="store_true",
        help="Delete a job and its associated S3 files",
    )
    parser.add_argument(
        "--destroy",
        action="store_true",
        help="Clean up OTA infrastructure (cancels all running jobs)",
    )

    # Setup parameters
    parser.add_argument(
        "--cert-type",
        type=str,
        choices=["RSA", "ECDSA"],
        default="ECDSA",
        help="Certificate type for code signing (default: ECDSA)",
    )
    parser.add_argument(
        "--platform-id",
        type=str,
        default="AmazonFreeRTOS-Default",
        help="AWS Code Signer platform ID (default: AmazonFreeRTOS-Default)",
    )

    # Job creation parameters
    parser.add_argument(
        "--files", type=str, help="JSON file describing files to include in OTA job"
    )
    parser.add_argument(
        "--job-config",
        type=str,
        help="JSON file with advanced job configurations for --create-custom-job",
    )
    parser.add_argument(
        "--thing-name",
        type=str,
        default=None,
        help="Target thing name for OTA job. Specify this or --thing-group-name.",
    )
    parser.add_argument(
        "--thing-group-name",
        type=str,
        default=None,
        help="Target thing group name for OTA job. Specify this or --thing-name.",
    )

    # Job execution parameters
    parser.add_argument(
        "--job-id", type=str, help="Job ID or OTA Update ID for job operations"
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Force operation (use with --cancel-* commands)",
    )

    return parser.parse_args()


def files_to_ota_files_config(files_path: str) -> OTAFilesConfig:
    if not os.path.exists(files_path):
        ota_error(f"Files configuration not found: {files_path}")
        sys.exit(1)

    with open(files_path, "r") as f:
        files = json.load(f)
        return OTAFilesConfig.from_json(files)


def parse_rmng_ota_info(job_config_path: str) -> RmngOtaInfo:
    """Parse RMNG OTA info from job config JSON"""
    if not os.path.exists(job_config_path):
        ota_error(f"Job config file not found: {job_config_path}")
        sys.exit(1)

    with open(job_config_path, "r") as f:
        config = json.load(f)

    # Extract RMNG-specific fields
    filetype = config.get("filetype")
    fw_version = config.get("fw_version")
    min_fw_version = config.get("min_fw_version")
    metadata = config.get("metadata")

    # Parse download window if present
    download_window = None
    if "download_window" in config:
        try:
            download_window = RmngOtaDownloadWindow.from_json(config["download_window"])
        except ValueError as e:
            ota_error(f"Invalid download_window configuration: {e}")
            sys.exit(1)

    return RmngOtaInfo(
        filetype=filetype,
        fw_version=fw_version,
        min_fw_version=min_fw_version,
        metadata=metadata,
        download_window=download_window,
    )


def main():
    """Main function"""
    args = parse_args()

    # Validate arguments
    if not any(
        [
            args.setup,
            args.create_ota_job,
            args.create_custom_job,
            args.create_rmng_ota_job,
            args.start_job,
            args.track_job_execution,
            args.cancel_custom_job,
            args.cancel_ota_job,
            args.delete_job,
            args.destroy,
        ]
    ):
        ota_error(
            "Must specify one of: --setup, --create-ota-job, --create-custom-job, --create-rmng-ota-job, --start-job, "
            "--track-job-execution, --cancel-custom-job, --cancel-ota-job, --delete-job, --destroy"
        )
        sys.exit(1)

    if args.create_ota_job or args.create_custom_job or args.create_rmng_ota_job:
        if not args.files:
            ota_error(
                "--create-ota-job or --create-custom-job requires --files argument"
            )
            sys.exit(1)
        if not os.path.exists(args.files):
            ota_error(f"Files configuration not found: {args.files}")
            sys.exit(1)
        if not args.thing_name and not args.thing_group_name:
            ota_error(
                "Must specify either --thing-name or --thing-group-name for job creation"
            )
            sys.exit(1)
        if args.thing_name and args.thing_group_name:
            ota_error(
                "Cannot specify both --thing-name and --thing-group-name. Please choose one target."
            )
            sys.exit(1)

    if args.start_job and not args.job_id:
        ota_error("--start-job requires --job-id argument")
        sys.exit(1)

    if args.track_job_execution:
        if not args.job_id:
            ota_error("--track-job-execution requires --job-id argument")
            sys.exit(1)
        if not args.thing_name:
            ota_error("--track-job-execution requires --thing-name argument")
            sys.exit(1)

    if args.cancel_custom_job and not args.job_id:
        ota_error("--cancel-custom-job requires --job-id argument")
        sys.exit(1)

    if args.cancel_ota_job and not args.job_id:
        ota_error("--cancel-ota-job requires --job-id argument")
        sys.exit(1)

    if args.delete_job and not args.job_id:
        ota_error("--delete-job requires --job-id argument")
        sys.exit(1)

    # Initialize OTA manager
    try:
        ota_manager = OTAManager()
    except Exception as e:
        ota_error(f"Failed to initialize OTA manager: {e}")
        sys.exit(1)

    # Execute requested operation
    try:
        if args.setup:
            ota_manager.setup_infrastructure(args.cert_type, args.platform_id)

        elif args.create_ota_job:
            ota_update_id = ota_manager.create_ota_job(
                files_to_ota_files_config(args.files),
                thing_name=args.thing_name,
                thing_group_name=args.thing_group_name,
            )
            if ota_update_id:
                ota_log(
                    f"Standard OTA job created successfully! OTA Update ID: {ota_update_id}"
                )
                ota_info(
                    f"To start the job, run: python ota_helper.py --start-job --job-id {ota_update_id}"
                )
                thing_hint = args.thing_name if args.thing_name else "<thing_name>"
                ota_info(
                    f"To track execution, run: python ota_helper.py --track-job-execution --job-id {ota_update_id} --thing-name {thing_hint}"
                )
            else:
                ota_error("Failed to create OTA job")
                sys.exit(1)

        elif args.create_custom_job:
            job_id = ota_manager.create_custom_job(
                files_to_ota_files_config(args.files),
                thing_name=args.thing_name,
                thing_group_name=args.thing_group_name,
                job_config_path=args.job_config,
            )
            if job_id:
                ota_log(f"Custom job created successfully! Job ID: {job_id}")
                ota_info(
                    f"To start the job, run: python ota_helper.py --start-job --job-id {job_id}"
                )
                thing_hint = args.thing_name if args.thing_name else "<thing_name>"
                ota_info(
                    f"To track execution, run: python ota_helper.py --track-job-execution --job-id {job_id} --thing-name {thing_hint}"
                )
            else:
                ota_error("Failed to create custom job")
                sys.exit(1)

        elif args.create_rmng_ota_job:
            if not args.job_config:
                ota_error("--create-rmng-ota-job requires --job-config argument")
                sys.exit(1)
            rmng_ota_info = parse_rmng_ota_info(args.job_config)
            job_id = ota_manager.create_rmng_ota_job(
                files_to_ota_files_config(args.files),
                rmng_ota_info,
                thing_name=args.thing_name,
                thing_group_name=args.thing_group_name,
            )
            if job_id:
                ota_log(f"RMNG OTA job created successfully! Job ID: {job_id}")
                ota_info(
                    f"To start the job, run: python ota_helper.py --start-job --job-id {job_id}"
                )
                thing_hint = args.thing_name if args.thing_name else "<thing_name>"
                ota_info(
                    f"To track execution, run: python ota_helper.py --track-job-execution --job-id {job_id} --thing-name {thing_hint}"
                )
            else:
                ota_error("Failed to create RMNG OTA job")
                sys.exit(1)

        elif args.start_job:
            ota_manager.start_job(args.job_id)

        elif args.track_job_execution:
            for status in ota_manager.track_job_execution(args.job_id, args.thing_name):
                ota_info(f"Status: {status.status}")
                details_str = "\n".join(
                    [f"  - {key}: {value}" for key, value in status.details.items()]
                )
                ota_info(f"Details:\n{details_str}")
                ota_info(f"Last Updated: {status.last_updated_dt}")

        elif args.cancel_custom_job:
            success = ota_manager.cancel_custom_job(args.job_id, force=args.force)
            if success:
                ota_log(f"Successfully canceled custom job: {args.job_id}")
            else:
                ota_error(f"Failed to cancel custom job: {args.job_id}")
                sys.exit(1)

        elif args.cancel_ota_job:
            success = ota_manager.cancel_ota_job(args.job_id, force=args.force)
            if success:
                ota_log(f"Successfully canceled OTA update: {args.job_id}")
            else:
                ota_error(f"Failed to cancel OTA update: {args.job_id}")
                sys.exit(1)

        elif args.delete_job:
            success = ota_manager.delete_job(args.job_id)
            if success:
                ota_log(f"Successfully deleted job: {args.job_id}")
            else:
                ota_error(f"Failed to delete job: {args.job_id}")
                sys.exit(1)

        elif args.destroy:
            ota_manager.destroy_infrastructure()

    except KeyboardInterrupt:
        ota_warn("Operation interrupted by user")
        sys.exit(1)
    except Exception as e:
        ota_error(f"Operation failed: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
