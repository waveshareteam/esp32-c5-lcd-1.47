#!/usr/bin/env python3
"""Validate that release archives exactly cover the configured build matrix."""

from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from pathlib import Path
from typing import Any

from validate_firmware import load_board_contract, validate_zip


DEFAULT_CONFIG = Path("config/ci.json")
SURFACES = {
    "esp-idf": Path("examples/esp-idf"),
    "arduino": Path("examples/arduino"),
}
VERSION = re.compile(r"^v?\d+\.\d+\.\d+$")
GIT_SHA = re.compile(r"^[0-9a-f]{7,64}$")
BuildKey = tuple[str, str, str, str]


def resolve_inside(repo: Path, value: str | Path) -> Path:
    candidate = Path(value)
    path = candidate.resolve() if candidate.is_absolute() else (repo / candidate).resolve()
    try:
        path.relative_to(repo)
    except ValueError as exc:
        raise ValueError("CI configuration must be inside the repository") from exc
    return path


def load_ci_config(repo: Path, value: str | Path = DEFAULT_CONFIG) -> tuple[Path, dict[str, Any]]:
    path = resolve_inside(repo, value)
    config = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(config, dict) or config.get("schema_version") != 1:
        raise ValueError(f"unsupported CI configuration schema in {path}")
    return path, config


def discover_projects(repo: Path) -> dict[str, set[str]]:
    projects: dict[str, set[str]] = {framework: set() for framework in SURFACES}
    for framework, relative_root in SURFACES.items():
        root = repo / relative_root
        if not root.is_dir():
            raise FileNotFoundError(f"example directory not found: {root}")
        for entry in root.iterdir():
            if not entry.is_dir():
                continue
            marker = entry / ("CMakeLists.txt" if framework == "esp-idf" else f"{entry.name}.ino")
            if marker.is_file():
                projects[framework].add(entry.relative_to(repo).as_posix())
        if not projects[framework]:
            raise ValueError(f"no first-party {framework} examples found")
    return projects


def configured_versions(config: dict[str, Any]) -> dict[str, list[str]]:
    idf_versions = config.get("esp_idf", {}).get("versions")
    arduino_core = config.get("arduino", {}).get("core")
    if not isinstance(idf_versions, list) or not idf_versions:
        raise ValueError("CI configuration must define at least one ESP-IDF version")
    if any(not isinstance(version, str) or not VERSION.fullmatch(version) for version in idf_versions):
        raise ValueError("ESP-IDF versions must be release tags such as v6.0.2")
    if len(idf_versions) != len(set(idf_versions)):
        raise ValueError("CI configuration contains duplicate ESP-IDF versions")
    if not isinstance(arduino_core, str) or not VERSION.fullmatch(arduino_core):
        raise ValueError("Arduino core must be a release version such as 3.3.11")
    return {"esp-idf": idf_versions, "arduino": [arduino_core]}


def expected_variants(repo: Path, config: dict[str, Any]) -> set[BuildKey]:
    board = config.get("board")
    if not isinstance(board, dict) or not isinstance(board.get("target"), str):
        raise ValueError("CI configuration must define a board target")
    target = board["target"]
    projects = discover_projects(repo)
    versions = configured_versions(config)
    return {
        (project, framework, version, target)
        for framework, framework_projects in projects.items()
        for project in framework_projects
        for version in versions[framework]
    }


def read_manifest(path: Path) -> dict[str, Any]:
    with zipfile.ZipFile(path) as archive:
        manifests = [name for name in archive.namelist() if name.endswith("/manifest.json")]
        if len(manifests) != 1:
            raise ValueError(f"{path.name} must contain exactly one manifest.json")
        manifest = json.loads(archive.read(manifests[0]).decode("utf-8"))
        if not isinstance(manifest, dict):
            raise ValueError(f"{path.name} manifest must be a JSON object")
        return manifest


def format_key(key: BuildKey) -> str:
    project, framework, version, target = key
    return f"{project} [{framework} {version} {target}]"


def validate_release(
    repo: Path,
    artifact_dir: Path,
    expected_git_sha: str,
    config_value: str | Path = DEFAULT_CONFIG,
) -> tuple[int, int]:
    expected_sha = expected_git_sha.strip().lower()
    if not GIT_SHA.fullmatch(expected_sha):
        raise ValueError("expected git SHA must contain 7 to 64 lowercase hexadecimal characters")
    if not artifact_dir.is_dir():
        raise FileNotFoundError(f"artifact directory not found: {artifact_dir}")
    archives = sorted(artifact_dir.glob("*.zip"))
    if not archives:
        raise ValueError(f"no firmware ZIP archives found in {artifact_dir}")

    config_path, config = load_ci_config(repo, config_value)
    expected = expected_variants(repo, config)
    board_contract = load_board_contract(config_path)
    covered: dict[BuildKey, str] = {}
    for archive in archives:
        validate_zip(archive, board_contract)
        manifest = read_manifest(archive)
        manifest_sha = str(manifest.get("git_sha", "")).lower()
        if manifest_sha != expected_sha:
            raise ValueError(
                f"{archive.name} git_sha {manifest_sha!r} does not match release commit {expected_sha!r}"
            )
        key: BuildKey = (
            str(manifest.get("project_path", "")),
            str(manifest.get("framework", "")),
            str(manifest.get("framework_version", "")),
            str(manifest.get("target", "")),
        )
        if key not in expected:
            raise ValueError(f"{archive.name} has unexpected build variant: {format_key(key)}")
        if key in covered:
            raise ValueError(
                f"duplicate build variant {format_key(key)} in {covered[key]} and {archive.name}"
            )
        covered[key] = archive.name

    missing = expected - covered.keys()
    if missing:
        raise ValueError(
            "missing firmware build variants: "
            + ", ".join(format_key(key) for key in sorted(missing))
        )
    if len(archives) != len(expected):
        raise ValueError(f"expected exactly {len(expected)} archives, found {len(archives)}")
    return len(archives), len(expected)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_dir")
    parser.add_argument("--repo", default=".")
    parser.add_argument("--config", default=DEFAULT_CONFIG.as_posix())
    parser.add_argument("--git-sha", required=True)
    args = parser.parse_args()
    try:
        archive_count, variant_count = validate_release(
            Path(args.repo).resolve(),
            Path(args.artifact_dir).resolve(),
            args.git_sha,
            args.config,
        )
        print(f"valid release: {archive_count} archives cover {variant_count} configured build variants")
    except (OSError, ValueError, KeyError, json.JSONDecodeError, zipfile.BadZipFile) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
