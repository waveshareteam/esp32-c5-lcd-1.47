#!/usr/bin/env python3
"""Validate a draft GitHub Release against locally built firmware archives."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


PENDING_EXIT_CODE = 75
SHA256 = re.compile(r"^sha256:[0-9a-f]{64}$")


class PendingReleaseAssets(ValueError):
    """Raised while GitHub is still indexing uploaded release assets."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def local_assets(artifact_dir: Path) -> dict[str, tuple[int, str]]:
    if not artifact_dir.is_dir():
        raise FileNotFoundError(f"artifact directory not found: {artifact_dir}")
    archives = sorted(artifact_dir.glob("*.zip"))
    if not archives:
        raise ValueError(f"no firmware ZIP archives found in {artifact_dir}")
    return {archive.name: (archive.stat().st_size, sha256(archive)) for archive in archives}


def validate_downloaded_assets(
    expected: dict[str, tuple[int, str]], downloaded_dir: Path
) -> None:
    downloaded = local_assets(downloaded_dir)
    expected_names = set(expected)
    downloaded_names = set(downloaded)
    if expected_names != downloaded_names:
        missing = sorted(expected_names - downloaded_names)
        unexpected = sorted(downloaded_names - expected_names)
        raise ValueError(
            "downloaded GitHub Release asset names differ: "
            f"missing={json.dumps(missing)}, unexpected={json.dumps(unexpected)}"
        )
    mismatches = {
        name: {"local": expected[name], "downloaded": downloaded[name]}
        for name in sorted(expected)
        if expected[name] != downloaded[name]
    }
    if mismatches:
        raise ValueError(
            "downloaded GitHub Release asset sizes or digests differ: "
            f"{json.dumps(mismatches)}"
        )


def validate_release(
    release: Any,
    artifact_dir: Path,
    tag: str,
    downloaded_dir: Path | None = None,
) -> int:
    if not isinstance(release, dict):
        raise ValueError("GitHub Release response must be a JSON object")
    if release.get("draft") is not True:
        raise ValueError("Release must remain a draft until verification completes")
    if release.get("tag_name") != tag:
        raise ValueError("Release tag metadata does not match the workflow tag")
    if release.get("name") != tag:
        raise ValueError("Release title does not match the workflow tag")
    if release.get("prerelease") is not False:
        raise ValueError("stable release workflow must not publish a prerelease")
    if not str(release.get("body") or "").strip():
        raise ValueError("generated release notes must not be empty")

    expected = local_assets(artifact_dir)
    assets = release.get("assets")
    if not isinstance(assets, list):
        raise ValueError("GitHub Release response must contain an asset list")

    remote: dict[str, tuple[int, Any]] = {}
    for asset in assets:
        if not isinstance(asset, dict):
            raise ValueError("GitHub Release assets must be JSON objects")
        name = asset.get("name")
        if not isinstance(name, str) or not name:
            raise ValueError("GitHub Release assets must have non-empty names")
        if name in remote:
            raise ValueError(f"duplicate GitHub Release asset: {name}")
        size = asset.get("size")
        if not isinstance(size, int) or isinstance(size, bool) or size < 0:
            raise ValueError(f"GitHub Release asset has invalid size: {name}")
        remote[name] = (size, asset.get("digest"))

    expected_names = set(expected)
    remote_names = set(remote)
    missing = sorted(expected_names - remote_names)
    unexpected = sorted(remote_names - expected_names)
    if unexpected:
        raise ValueError(
            "GitHub Release has unexpected assets: " + json.dumps(unexpected)
        )

    wrong_sizes = {
        name: {"local": expected[name][0], "remote": remote[name][0]}
        for name in sorted(remote)
        if expected[name][0] != remote[name][0]
    }
    if wrong_sizes:
        raise ValueError(f"GitHub Release asset sizes differ: {json.dumps(wrong_sizes)}")

    malformed = {
        name: digest
        for name, (_, digest) in sorted(remote.items())
        if digest is not None
        and (not isinstance(digest, str) or not SHA256.fullmatch(digest))
    }
    if malformed:
        raise ValueError(f"GitHub Release asset digests are malformed: {json.dumps(malformed)}")

    wrong_digests = {
        name: {"local": expected[name][1], "remote": remote[name][1]}
        for name in sorted(remote)
        if remote[name][1] is not None and expected[name][1] != remote[name][1]
    }
    if wrong_digests:
        raise ValueError(f"GitHub Release asset digests differ: {json.dumps(wrong_digests)}")

    pending = sorted(name for name, (_, digest) in remote.items() if digest is None)
    if missing or pending:
        if downloaded_dir is None:
            details = []
            if missing:
                details.append("assets not listed yet: " + ", ".join(missing))
            if pending:
                details.append("SHA-256 digests pending: " + ", ".join(pending))
            raise PendingReleaseAssets(
                "GitHub is still indexing the draft Release (" + "; ".join(details) + ")"
            )
        validate_downloaded_assets(expected, downloaded_dir)
    return len(expected)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release_json")
    parser.add_argument("artifact_dir")
    parser.add_argument("--tag", required=True)
    parser.add_argument("--downloaded-assets")
    args = parser.parse_args()

    try:
        release = json.loads(Path(args.release_json).read_text(encoding="utf-8"))
        downloaded_dir = Path(args.downloaded_assets) if args.downloaded_assets else None
        count = validate_release(release, Path(args.artifact_dir), args.tag, downloaded_dir)
    except PendingReleaseAssets as exc:
        print(f"pending: {exc}", file=sys.stderr)
        return PENDING_EXIT_CODE
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"valid draft GitHub Release: {count} firmware assets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
