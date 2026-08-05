# Credentials Store

This credentials helper requires the following files placed in this directory
(create the sub-directories if absent — git does not track empty directories):
- Under `general`:
    - `rmng-outputs.json`, corresponding to the AWS deployment in use.
- Under `ota`:
    - `ota-test-cert-<type>[_key].pem`, AWS code signer certificate and private key. Will be managed by the [AWS OTA manager](../util/ota_aws.py).
