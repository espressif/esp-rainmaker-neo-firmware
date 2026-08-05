# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Utility cloud functions.
"""

import boto3

from botocore.exceptions import ClientError, NoCredentialsError

blue = "\033[94m"
reset = "\033[0m"


def cloud_log(message):
    """
    Print message with Cloud prefix in blue color.
    """
    print(f"{blue}[Cloud]{reset} {message}")


def destroy_node(
    region, iot_endpoint, node_thing_name, group_id=None, subgroup_ids=[]
) -> bool:
    """
    Destroy a node with the cloud.
    """
    # Create boto3 client
    iot_client = boto3.client("iot", region_name=region)
    iot_data_client = boto3.client(
        "iot-data", region_name=region, endpoint_url=f"https://{iot_endpoint}"
    )

    try:
        try:
            # Delete the indexed shadow
            iot_data_client.delete_thing_shadow(
                thingName=node_thing_name, shadowName="iparams"
            )
            cloud_log(f"Deleted indexed shadow for thing '{node_thing_name}'")
        except iot_data_client.exceptions.ResourceNotFoundException:
            pass  # Shadow doesn't exist, which is fine

        # Delete any group-based shadows
        if group_id:
            shadow_name = f"params-{group_id}"
            if subgroup_ids:
                for subgroup_id in sorted(subgroup_ids):
                    shadow_name += f"-{subgroup_id}"

            try:
                # Delete the group-based shadow
                iot_data_client.delete_thing_shadow(
                    thingName=node_thing_name, shadowName=shadow_name
                )
                cloud_log(f"Deleted group-based shadow for thing '{node_thing_name}'")
            except iot_data_client.exceptions.ResourceNotFoundException:
                pass  # Shadow doesn't exist, which is fine

    except ClientError as e:
        cloud_log(f"Error destroying shadows: {str(e)}")

    try:
        # Get the principals (certificates) attached to the thing
        principals = iot_client.list_thing_principals(thingName=node_thing_name)[
            "principals"
        ]

        for principal in principals:
            # Detach the principal from the thing
            iot_client.detach_thing_principal(
                thingName=node_thing_name, principal=principal
            )
            cloud_log(f"Detached principal {principal} from thing '{node_thing_name}'")

            # List and detach policies from the certificate
            policies = iot_client.list_attached_policies(target=principal)["policies"]
            for policy in policies:
                iot_client.detach_policy(
                    policyName=policy["policyName"], target=principal
                )
                cloud_log(
                    f"Detached policy {policy['policyName']} from principal {principal}"
                )

            # Update the certificate status to INACTIVE
            certificate_id = principal.split("/")[-1]
            iot_client.update_certificate(
                certificateId=certificate_id, newStatus="INACTIVE"
            )
            cloud_log(f"Set certificate {certificate_id} to INACTIVE")

            # Delete the certificate
            iot_client.delete_certificate(
                certificateId=certificate_id, forceDelete=True
            )
            cloud_log(f"Deleted certificate {certificate_id}")

        # Delete the thing
        iot_client.delete_thing(thingName=node_thing_name)
        cloud_log(f"Deleted thing '{node_thing_name}'")
    except NoCredentialsError as e:
        cloud_log(f"No credentials found: {e}. Please check your AWS credentials.")
        return False
    except ClientError as ex:
        cloud_log(f"An error occurred: {ex}")
        return False

    return True
