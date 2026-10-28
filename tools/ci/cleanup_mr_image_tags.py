#!/usr/bin/env python
#
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0
"""Delete the mr-<iid> tags of one project registry image whose MR is no longer open.

Runs in CI with the job token; set GITLAB_TOKEN and CI_* variables to run it locally.
"""

import argparse
import base64
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

MR_TAG = re.compile(r"^mr-(\d+)$")


def request(url, method="GET", headers=None):
    req = urllib.request.Request(url, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req) as resp:
            return resp.status, resp.headers, resp.read()
    except urllib.error.HTTPError as e:
        return e.code, e.headers, e.read()


def registry_token(registry, repository, user, password):
    status, headers, _ = request(f"https://{registry}/v2/")
    challenge = headers.get("WWW-Authenticate", "")
    params = dict(re.findall(r'(\w+)="([^"]*)"', challenge))
    if status != 401 or "realm" not in params:
        sys.exit(f"ERROR: unexpected registry challenge ({status}): {challenge!r}")
    query = urllib.parse.urlencode(
        {
            "service": params.get("service", ""),
            "scope": f"repository:{repository}:pull,delete",
        }
    )
    basic = base64.b64encode(f"{user}:{password}".encode()).decode()
    status, _, body = request(
        f"{params['realm']}?{query}", headers={"Authorization": f"Basic {basic}"}
    )
    if status != 200:
        sys.exit(f"ERROR: registry token request failed ({status}): {body[:200]!r}")
    return json.loads(body)["token"]


def list_tags(registry, repository, auth):
    tags, url = [], f"https://{registry}/v2/{repository}/tags/list?n=1000"
    while url:
        status, headers, body = request(url, headers=auth)
        if status == 404:
            return tags
        if status != 200:
            sys.exit(f"ERROR: tag list failed ({status}): {body[:200]!r}")
        tags += json.loads(body).get("tags") or []
        link = re.match(r"<([^>]+)>;\s*rel=\"next\"", headers.get("Link", ""))
        url = (
            urllib.parse.urljoin(f"https://{registry}", link.group(1)) if link else None
        )
    return tags


def mr_state(api, project_id, iid, auth):
    status, _, body = request(
        f"{api}/projects/{project_id}/merge_requests/{iid}", headers=auth
    )
    if status == 404:
        return "missing"
    if status != 200:
        sys.exit(f"ERROR: MR !{iid} lookup failed ({status}): {body[:200]!r}")
    return json.loads(body)["state"]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "image", help="image name under the project registry, e.g. posix_test_env"
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="print the tags, delete nothing"
    )
    args = parser.parse_args()

    env = os.environ
    registry = env["CI_REGISTRY"]
    repository = f"{env['CI_REGISTRY_IMAGE'].removeprefix(registry + '/')}/{args.image}"
    if "CI_JOB_TOKEN" in env:
        api_auth = {"JOB-TOKEN": env["CI_JOB_TOKEN"]}
        user, password = env["CI_REGISTRY_USER"], env["CI_REGISTRY_PASSWORD"]
    else:
        api_auth = {"PRIVATE-TOKEN": env["GITLAB_TOKEN"]}
        user, password = env["GITLAB_USER_LOGIN"], env["GITLAB_TOKEN"]

    auth = {
        "Authorization": f"Bearer {registry_token(registry, repository, user, password)}"
    }
    failed = 0
    for tag in sorted(list_tags(registry, repository, auth)):
        match = MR_TAG.match(tag)
        if not match:
            continue
        state = mr_state(
            env["CI_API_V4_URL"], env["CI_PROJECT_ID"], match.group(1), api_auth
        )
        if state == "opened":
            print(f"keep   {repository}:{tag} (MR open)")
            continue
        if args.dry_run:
            print(f"would delete {repository}:{tag} (MR {state})")
            continue
        status, _, body = request(
            f"https://{registry}/v2/{repository}/manifests/{tag}",
            method="DELETE",
            headers=auth,
        )
        if status in (200, 202):
            print(f"delete {repository}:{tag} (MR {state})")
        else:
            print(
                f"ERROR: delete {repository}:{tag} failed ({status}): {body[:200]!r}",
                file=sys.stderr,
            )
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
