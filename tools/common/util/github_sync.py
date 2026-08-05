# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Download a subdirectory of a GitHub repository at a given ref, cache it on disk,
and optionally expose it on sys.path — same idea as cmake/ file_sync helpers.

By default, caches live under ``<repo>/.cache/github-sync/`` (gitignored via the root ``.cache`` rule).
Set GITHUB_TOKEN in the environment for higher API rate limits.

Branch/tag refs are resolved to a commit SHA via the GitHub API once per (owner, repo, ref); the result is
stored under ``ref-resolve/`` with a cross-process file lock so parallel workers share one resolution.

Zipball download and extraction for the same cached archive are also guarded by a cross-process lock so
parallel runners (e.g. pytest-xdist) cannot corrupt a shared ``.zip`` or partially extracted tree.

Materialization of a subfolder into ``trees/<key>/`` is likewise guarded: copies are written to a sibling
``.materializing`` directory, a completion marker is written last, then the directory is swapped into
place with ``os.replace`` under a per-target lock so workers never import from a half-copied tree.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path
from typing import Optional

from filelock import FileLock, Timeout

__all__ = [
    "parse_github_repo_url",
    "resolve_ref_to_sha",
    "sync_github_folder",
    "ensure_github_folder_on_syspath",
    "get_github_archive_repo_root",
    "materialize_github_folder",
]

# GitHub rejects many requests without a User-Agent.
_DEFAULT_UA = "rmng-sdk-github-sync/1.0"


def _repo_root() -> Path:
    """Directory containing ``.git`` for this checkout, or a path derived from this file."""
    here = Path(__file__).resolve()
    for p in (here, *here.parents):
        if (p / ".git").exists():
            return p
    # tools/common/util/github_sync.py -> repo root
    return here.parents[3]


def _default_cache_dir() -> Path:
    """In-repo cache (under ``.cache/``, gitignored at the repository root)."""
    return _repo_root() / ".cache" / "github-sync"


def _md5_hex(s: str) -> str:
    return hashlib.md5(s.encode(), usedforsecurity=False).hexdigest()


_REF_CACHE_SUBDIR = "ref-resolve"

# Written last under trees/<key>/ so parallel workers only reuse a fully copied tree.
_TREE_SYNC_MARKER_NAME = "._github_sync_complete"
_MATERIALIZING_SUFFIX = ".materializing"


def _ref_lock_timeout_sec() -> float:
    raw = os.environ.get("GITHUB_SYNC_REF_LOCK_TIMEOUT_SEC", "").strip()
    if not raw:
        return 600.0
    try:
        return max(1.0, float(raw))
    except ValueError:
        return 600.0


def _ref_cache_paths(
    owner: str, repo: str, ref: str, cache_dir: Path
) -> tuple[Path, Path]:
    key = _md5_hex(f"{owner.lower()}/{repo}/{ref}")
    base = cache_dir / _REF_CACHE_SUBDIR
    cache_file = base / f"{key}.json"
    lock_file = base / f"{key}.lock"
    return cache_file, lock_file


def _read_ref_cache_file(cache_file: Path) -> Optional[str]:
    if not cache_file.is_file():
        return None
    try:
        data = json.loads(cache_file.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return None
    sha = data.get("sha")
    if not isinstance(sha, str) or len(sha) != 40:
        return None
    if not re.fullmatch(r"[0-9a-f]{40}", sha.lower()):
        return None
    return sha.lower()


def _write_ref_cache_file(
    cache_file: Path, owner: str, repo: str, ref: str, sha: str
) -> None:
    cache_file.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "owner": owner,
        "repo": repo,
        "ref": ref,
        "sha": sha,
    }
    tmp = cache_file.with_suffix(cache_file.suffix + ".tmp")
    tmp.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, cache_file)


def _fetch_commit_sha_from_ls_remote(owner: str, repo: str, ref: str) -> Optional[str]:
    """
    Resolve ``ref`` to a commit SHA via ``git ls-remote`` (no API rate limit).

    Returns None only when git itself is unusable (not installed, or the command failed, e.g.
    no network), so the caller can fall back to the GitHub API. If git ran successfully but the
    ref does not exist, raises RuntimeError -- retrying through the API would not help.

    Annotated tags are peeled via their ``^{}`` line so the result is the commit SHA the API
    would return, not the tag object SHA.
    """
    git = shutil.which("git")
    if git is None:
        return None
    try:
        proc = subprocess.run(
            [
                git,
                "ls-remote",
                f"https://github.com/{owner}/{repo}.git",
                f"refs/heads/{ref}",
                # Trailing "*" so git also emits the peeled "refs/tags/<ref>^{}" line for
                # annotated tags; an exact pattern suppresses it. Extra glob hits (e.g.
                # "<ref>-rc1") are ignored by the exact-name lookup below.
                f"refs/tags/{ref}*",
            ],
            capture_output=True,
            text=True,
            timeout=120,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if proc.returncode != 0:
        return None

    found: dict[str, str] = {}
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) != 2:
            continue
        sha, name = parts[0].lower(), parts[1]
        if re.fullmatch(r"[0-9a-f]{40}", sha):
            found[name] = sha

    # Prefer a branch tip over a tag of the same name; for annotated tags prefer the peeled
    # ("^{}") commit over the tag object.
    for name in (
        f"refs/heads/{ref}",
        f"refs/tags/{ref}^{{}}",
        f"refs/tags/{ref}",
    ):
        if name in found:
            return found[name]

    raise RuntimeError(
        f"git ls-remote found no refs/heads/{ref} or refs/tags/{ref} in {owner}/{repo}"
    )


def _fetch_commit_sha_from_github_api(
    owner: str, repo: str, ref: str, token: Optional[str]
) -> str:
    api_ref = urllib.parse.quote(ref, safe="")
    api_url = f"https://api.github.com/repos/{owner}/{repo}/commits/{api_ref}"
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": _DEFAULT_UA,
    }
    tok = token if token is not None else os.environ.get("GITHUB_TOKEN", "").strip()
    if tok:
        headers["Authorization"] = f"Bearer {tok}"

    req = urllib.request.Request(api_url, headers=headers, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            body = resp.read().decode()
    except urllib.error.HTTPError as e:
        raise RuntimeError(
            f"GitHub API failed for {owner}/{repo}@{ref} ({api_url}): HTTP {e.code} {e.reason}"
        ) from e
    except urllib.error.URLError as e:
        raise RuntimeError(
            f"GitHub API failed for {owner}/{repo}@{ref} ({api_url}): {e.reason}"
        ) from e

    try:
        data = json.loads(body)
    except json.JSONDecodeError as e:
        raise RuntimeError(
            f"GitHub API returned non-JSON for {owner}/{repo}@{ref}"
        ) from e

    sha = data.get("sha")
    if (
        not isinstance(sha, str)
        or len(sha) != 40
        or not re.fullmatch(r"[0-9a-f]{40}", sha.lower())
    ):
        raise RuntimeError(
            f"Could not parse 40-char commit sha from GitHub API for {owner}/{repo}@{ref}"
        )
    return sha.lower()


def parse_github_repo_url(url: str) -> tuple[str, str]:
    """
    Parse owner and repo from a github.com repository URL.

    Supports https://github.com/owner/repo, https://github.com/owner/repo.git,
    and git@github.com:owner/repo.git.
    """
    u = url.strip()
    if u.startswith("git@"):
        # git@github.com:owner/repo.git
        m = re.match(r"git@github\.com:([^/]+)/([^/]+?)(?:\.git)?$", u, re.I)
        if not m:
            raise ValueError(
                f"Unsupported git SSH URL (expected git@github.com:owner/repo): {url!r}"
            )
        return m.group(1).lower(), _normalize_repo_name(m.group(2))
    p = urllib.parse.urlparse(u)
    if p.netloc.lower() not in ("github.com", "www.github.com"):
        raise ValueError(f"Only github.com repositories are supported: {url!r}")
    parts = [x for x in p.path.strip("/").split("/") if x]
    if len(parts) < 2:
        raise ValueError(f"Could not parse owner/repo from URL: {url!r}")
    return parts[0].lower(), _normalize_repo_name(parts[1])


def _normalize_repo_name(name: str) -> str:
    return name[:-4] if name.lower().endswith(".git") else name


def resolve_ref_to_sha(
    owner: str,
    repo: str,
    ref: str,
    *,
    token: Optional[str] = None,
    cache_dir: Optional[Path] = None,
) -> str:
    """
    Resolve a branch name, tag, or ref to a full 40-character commit SHA.

    If ``ref`` is already 40 hex characters, it is normalized to lowercase and returned
    without calling the API.

    Otherwise ``git ls-remote`` resolves the ref (no rate limit), falling back to the GitHub API
    if git is unavailable. The lookup runs at most once per (owner, repo, ref) on this machine:
    results are stored under ``cache_dir/ref-resolve/`` (default:
    ``<repo>/.cache/github-sync/ref-resolve/``) and guarded by a cross-process file lock so
    parallel test workers do not stampede the remote.
    """
    ref = ref.strip()
    if len(ref) == 40 and re.fullmatch(r"[0-9a-fA-F]{40}", ref):
        return ref.lower()

    if cache_dir is None:
        cache_dir = _default_cache_dir()

    cache_file, lock_path = _ref_cache_paths(owner, repo, ref, cache_dir)

    cached = _read_ref_cache_file(cache_file)
    if cached is not None:
        return cached

    lock_timeout = _ref_lock_timeout_sec()
    lock = FileLock(str(lock_path), timeout=lock_timeout)
    try:
        with lock:
            cached = _read_ref_cache_file(cache_file)
            if cached is not None:
                return cached
            sha = _fetch_commit_sha_from_ls_remote(owner, repo, ref)
            if sha is None:
                sha = _fetch_commit_sha_from_github_api(owner, repo, ref, token)
            _write_ref_cache_file(cache_file, owner, repo, ref, sha)
            return sha
    except Timeout as e:
        raise RuntimeError(
            f"Timed out waiting for ref-resolve lock ({lock_path}) after {lock_timeout:.0f}s; "
            f"another process may be resolving {owner}/{repo}@{ref}. "
            f"Increase GITHUB_SYNC_REF_LOCK_TIMEOUT_SEC if needed."
        ) from e


def _normalize_folder_path(folder_path: str) -> str:
    s = folder_path.strip().strip("/")
    if not s:
        raise ValueError("folder_path must not be empty")
    return s


def _github_request_headers(token: Optional[str]) -> dict[str, str]:
    headers = {"Accept": "application/vnd.github+json", "User-Agent": _DEFAULT_UA}
    tok = token if token is not None else os.environ.get("GITHUB_TOKEN", "").strip()
    if tok:
        headers["Authorization"] = f"Bearer {tok}"
    return headers


def _download_file(url: str, dest: Path, headers: dict[str, str]) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(url, headers=headers, method="GET")
    with urllib.request.urlopen(req, timeout=600) as resp:
        data = resp.read()
    dest.write_bytes(data)


def _download_file_atomic(url: str, dest: Path, headers: dict[str, str]) -> None:
    """Write ``dest`` via a temp file + ``os.replace`` so readers never see a partial zip."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_name(dest.name + ".part")
    try:
        _download_file(url, tmp, headers)
        os.replace(tmp, dest)
    except BaseException:
        if tmp.is_file():
            tmp.unlink(missing_ok=True)
        raise


def _copy_directory_contents(src: Path, dst: Path) -> None:
    """Copy contents of src into dst (like CMake copy_directory for a folder)."""
    if not src.is_dir():
        raise FileNotFoundError(f"Not a directory: {src}")
    dst.mkdir(parents=True, exist_ok=True)
    for child in src.iterdir():
        target = dst / child.name
        if child.is_dir():
            shutil.copytree(child, target, dirs_exist_ok=True)
        else:
            shutil.copy2(child, target)


def _target_materialize_paths(target_dir: Path) -> tuple[Path, Path]:
    """Return (lock file, staging directory) for atomic tree materialization next to ``target_dir``."""
    parent = target_dir.parent
    name = target_dir.name
    lock_path = parent / f"{name}.lock"
    staging = parent / f"{name}{_MATERIALIZING_SUFFIX}"
    return lock_path, staging


def _read_tree_sync_marker(target_dir: Path) -> Optional[str]:
    marker = target_dir / _TREE_SYNC_MARKER_NAME
    if not marker.is_file():
        return None
    try:
        data = json.loads(marker.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return None
    sha = data.get("sha")
    if not isinstance(sha, str) or len(sha) != 40:
        return None
    if not re.fullmatch(r"[0-9a-f]{40}", sha.lower()):
        return None
    return sha.lower()


def _write_tree_sync_marker(target_dir: Path, resolved_sha: str) -> None:
    payload = {"sha": resolved_sha.lower()}
    tmp = target_dir / (_TREE_SYNC_MARKER_NAME + ".tmp")
    tmp.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, target_dir / _TREE_SYNC_MARKER_NAME)


def _probe_extract_inner_root(
    extract_dir: Path, folder_path: Optional[str]
) -> Optional[Path]:
    """If ``extract_dir`` already looks like a finished extraction, return the inner repo root."""
    if not extract_dir.is_dir():
        return None
    children = [p for p in extract_dir.iterdir() if p.name not in (".", "..")]
    if len(children) != 1 or not children[0].is_dir():
        return None
    inner_root = children[0]
    if folder_path is None:
        return inner_root
    if (inner_root / folder_path).is_dir():
        return inner_root
    return None


def _ensure_github_archive_inner_root(
    owner: str,
    repo: str,
    ref: str,
    resolved_sha: str,
    *,
    token: Optional[str],
    cache_dir: Path,
    folder_path: Optional[str] = None,
    log_suffix: str = "",
) -> Path:
    """
    Download the source zip if needed and extract it. Return the single top-level directory
    inside the archive (e.g. ``esp-idf-v6.0.2``).

    If ``folder_path`` is set, reuse an existing extract only when ``inner_root/folder_path``
    exists (same rule as :func:`sync_github_folder`). If ``folder_path`` is ``None``, reuse
    whenever a single top-level directory is already present.

    Uses a cross-process lock so parallel pytest workers do not race on the same cache zip /
    extract directory (which otherwise yields corrupt zips and inconsistent xdist collection).
    """
    archive_key = _md5_hex(f"{owner}/{repo}/{resolved_sha}")
    archive_file = cache_dir / f".github_folder_{archive_key}.zip"
    extract_dir = cache_dir / f".github_folder_extract_{archive_key}"
    headers = _github_request_headers(token)
    lock_path = cache_dir / f".github_archive_{archive_key}.lock"
    lock_timeout = _ref_lock_timeout_sec()
    lock = FileLock(str(lock_path), timeout=lock_timeout)

    label = (
        f"{folder_path}{log_suffix}"
        if folder_path is not None
        else f"(repo root){log_suffix}"
    )

    try:
        with lock:
            # Finished tree + zip from a previous run, or another worker completed while we waited.
            warm2 = _probe_extract_inner_root(extract_dir, folder_path)
            if warm2 is not None and archive_file.is_file():
                print(
                    f"Reusing cached archive+extract for {owner}/{repo}@{resolved_sha} ({label})",
                    file=sys.stderr,
                )
                return warm2

            if archive_file.is_file():
                print(
                    f"Reusing cached archive {archive_file} for {owner}/{repo}@{resolved_sha} ({label})",
                    file=sys.stderr,
                )
            else:
                archive_url = f"https://github.com/{owner}/{repo}/archive/{ref}.zip"
                print(
                    f"Downloading {owner}/{repo}@{ref} archive (GitHub folder: {label})...",
                    file=sys.stderr,
                )
                _download_file_atomic(archive_url, archive_file, headers)

            skip_extract = False
            inner_root: Optional[Path] = None
            if extract_dir.is_dir():
                children = [
                    p for p in extract_dir.iterdir() if p.name not in (".", "..")
                ]
                if len(children) == 1 and children[0].is_dir():
                    inner_root = children[0]
                    if folder_path is None:
                        skip_extract = True
                    elif (inner_root / folder_path).is_dir():
                        skip_extract = True

            if not skip_extract:
                if extract_dir.exists():
                    shutil.rmtree(extract_dir)
                extract_dir.mkdir(parents=True, exist_ok=True)
                print(f"Extracting archive to {extract_dir}...", file=sys.stderr)
                with zipfile.ZipFile(archive_file, "r") as zf:
                    zf.extractall(extract_dir)

                children = [
                    p for p in extract_dir.iterdir() if p.name not in (".", "..")
                ]
                if len(children) != 1 or not children[0].is_dir():
                    raise RuntimeError(
                        f"Expected a single top-level directory in GitHub archive, found {len(children)}"
                    )
                inner_root = children[0]

            assert inner_root is not None
            return inner_root
    except Timeout as e:
        raise RuntimeError(
            f"Timed out waiting for GitHub archive lock ({lock_path}) after {lock_timeout:.0f}s; "
            f"another process may be downloading/extracting {owner}/{repo}@{resolved_sha}. "
            f"Increase GITHUB_SYNC_REF_LOCK_TIMEOUT_SEC if needed."
        ) from e


def get_github_archive_repo_root(
    repo_url: str,
    ref: str,
    *,
    token: Optional[str] = None,
    cache_dir: Optional[Path] = None,
    resolved_sha: Optional[str] = None,
) -> Path:
    """
    Return the extracted archive's top-level directory (e.g. for ``IDF_PATH``).

    Third-party scripts often load ``$IDF_PATH/components/...``; this path matches that layout
    without a full git clone.
    """
    owner, repo = parse_github_repo_url(repo_url)
    if cache_dir is None:
        cache_dir = _default_cache_dir()
    if resolved_sha is None:
        resolved_sha = resolve_ref_to_sha(
            owner, repo, ref, token=token, cache_dir=cache_dir
        )
    else:
        resolved_sha = resolved_sha.lower()
        if len(resolved_sha) != 40 or not re.fullmatch(r"[0-9a-f]{40}", resolved_sha):
            raise ValueError(
                "resolved_sha must be a 40-character lowercase hex commit SHA"
            )

    ref_lower = ref.strip().lower()
    if ref_lower != resolved_sha:
        print(f"{owner}/{repo}@{ref} -> {resolved_sha} (cache key)", file=sys.stderr)

    return _ensure_github_archive_inner_root(
        owner,
        repo,
        ref,
        resolved_sha,
        token=token,
        cache_dir=cache_dir,
        folder_path=None,
        log_suffix="",
    ).resolve()


def materialize_github_folder(
    repo_url: str,
    ref: str,
    folder_path: str,
    *,
    token: Optional[str] = None,
    cache_dir: Optional[Path] = None,
) -> Path:
    """
    Materialize a repository subfolder into ``trees/<key>/`` (same layout as
    :func:`ensure_github_folder_on_syspath`) without modifying ``sys.path``.
    """
    owner, repo = parse_github_repo_url(repo_url)
    norm = _normalize_folder_path(folder_path)
    if cache_dir is None:
        cache_dir = _default_cache_dir()
    resolved_sha = resolve_ref_to_sha(
        owner, repo, ref, token=token, cache_dir=cache_dir
    )
    tree_key = _md5_hex(f"{owner}/{repo}/{resolved_sha}/{norm}")

    target = (cache_dir / "trees" / tree_key).resolve()
    sync_github_folder(
        owner,
        repo,
        ref,
        folder_path,
        target,
        token=token,
        cache_dir=cache_dir,
        resolved_sha=resolved_sha,
    )
    return target


def sync_github_folder(
    owner: str,
    repo: str,
    ref: str,
    folder_path: str,
    target_dir: Path,
    *,
    token: Optional[str] = None,
    cache_dir: Optional[Path] = None,
    force_copy: bool = False,
    resolved_sha: Optional[str] = None,
) -> Path:
    """
    Download ``folder_path`` inside ``owner/repo`` at ``ref`` into ``target_dir``.

    Caches the zipball and extraction under ``cache_dir`` (default: ``<repo>/.cache/github-sync``).

    ``ref`` may be a branch, tag, or full 40-char SHA; branches/tags are resolved via the API
    so cache keys use the resolved commit.

    Returns ``target_dir`` (created with the folder *contents*, not an extra subdirectory
    named after the last path segment).
    """
    folder_path = _normalize_folder_path(folder_path)
    if cache_dir is None:
        cache_dir = _default_cache_dir()
    if resolved_sha is None:
        resolved_sha = resolve_ref_to_sha(
            owner, repo, ref, token=token, cache_dir=cache_dir
        )
    else:
        resolved_sha = resolved_sha.lower()
        if len(resolved_sha) != 40 or not re.fullmatch(r"[0-9a-f]{40}", resolved_sha):
            raise ValueError(
                "resolved_sha must be a 40-character lowercase hex commit SHA"
            )

    ref_lower = ref.strip().lower()
    if ref_lower != resolved_sha:
        print(f"{owner}/{repo}@{ref} -> {resolved_sha} (cache key)", file=sys.stderr)

    inner_root = _ensure_github_archive_inner_root(
        owner,
        repo,
        ref,
        resolved_sha,
        token=token,
        cache_dir=cache_dir,
        folder_path=folder_path,
        log_suffix="",
    )
    source_folder = inner_root / folder_path
    if not source_folder.is_dir():
        raise FileNotFoundError(
            f"Folder not in archive: {folder_path} (resolved {source_folder})"
        )

    lock_path, staging_dir = _target_materialize_paths(target_dir.resolve())
    lock_timeout = _ref_lock_timeout_sec()
    mat_lock = FileLock(str(lock_path), timeout=lock_timeout)
    try:
        with mat_lock:
            if (
                not force_copy
                and target_dir.is_dir()
                and _read_tree_sync_marker(target_dir) == resolved_sha.lower()
            ):
                print(f"Reusing existing target {target_dir}", file=sys.stderr)
            else:
                if force_copy and target_dir.exists():
                    shutil.rmtree(target_dir)
                if staging_dir.exists():
                    shutil.rmtree(staging_dir)
                staging_dir.mkdir(parents=True, exist_ok=True)
                print(
                    f"Copying {folder_path} into {target_dir} (staging {staging_dir.name})...",
                    file=sys.stderr,
                )
                _copy_directory_contents(source_folder, staging_dir)
                _write_tree_sync_marker(staging_dir, resolved_sha)
                if target_dir.exists():
                    shutil.rmtree(target_dir)
                os.replace(staging_dir, target_dir)
    except Timeout as e:
        raise RuntimeError(
            f"Timed out waiting for tree materialize lock ({lock_path}) after {lock_timeout:.0f}s; "
            f"another process may be copying into {target_dir}. "
            f"Increase GITHUB_SYNC_REF_LOCK_TIMEOUT_SEC if needed."
        ) from e

    print(f"Synced {owner}/{repo}@{ref}:{folder_path} -> {target_dir}", file=sys.stderr)
    return target_dir.resolve()


def ensure_github_folder_on_syspath(
    repo_url: str,
    ref: str,
    folder_path: str,
    *,
    cache_dir: Optional[Path] = None,
    token: Optional[str] = None,
    insert_at: int = 0,
    skip_if_present: bool = True,
) -> Path:
    """
    Ensure a path inside a GitHub repo is available locally, then prepend it to ``sys.path``.

    The materialized directory is cached under ``cache_dir`` using a key derived from
    owner, repo, resolved commit, and ``folder_path``.

    If ``skip_if_present`` is True and the path is already on ``sys.path``, returns that
    path without downloading.
    """
    owner, repo = parse_github_repo_url(repo_url)
    norm = _normalize_folder_path(folder_path)
    if cache_dir is None:
        cache_dir = _default_cache_dir()
    resolved_sha = resolve_ref_to_sha(
        owner, repo, ref, token=token, cache_dir=cache_dir
    )
    tree_key = _md5_hex(f"{owner}/{repo}/{resolved_sha}/{norm}")

    target = (cache_dir / "trees" / tree_key).resolve()

    if skip_if_present:
        for p in sys.path:
            try:
                if Path(p).resolve() == target:
                    return target
            except OSError:
                continue

    sync_github_folder(
        owner,
        repo,
        ref,
        folder_path,
        target,
        token=token,
        cache_dir=cache_dir,
        resolved_sha=resolved_sha,
    )

    sp = str(target)
    if sp not in sys.path:
        sys.path.insert(insert_at, sp)
    return target
