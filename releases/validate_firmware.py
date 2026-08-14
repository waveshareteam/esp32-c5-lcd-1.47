#!/usr/bin/env python3
"""Validate packaged firmware archives without extracting them."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import stat
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Any, Callable


REQUIRED_FILES = {"README.md", "SHA256SUMS", "flash.sh", "flash.bat", "flash_args.txt", "manifest.json"}
DEFAULT_CONFIG = Path("config/ci.json")
ESP_IMAGE_CHIP_ID_OFFSET = 12
ESP_IMAGE_CHIP_IDS = {
    "esp32c5": 23,
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def safe_member(name: str) -> PurePosixPath:
    if not isinstance(name, str) or not name or "\x00" in name:
        raise ValueError(f"invalid archive member: {name!r}")
    if "\\" in name:
        raise ValueError(f"archive member uses a backslash: {name}")
    raw_parts = name.split("/")
    if name.startswith("/") or any(part in ("", ".", "..") for part in raw_parts):
        raise ValueError(f"unsafe or non-canonical archive member: {name}")
    path = PurePosixPath(name)
    if path.is_absolute() or path.as_posix() != name:
        raise ValueError(f"unsafe or non-canonical archive member: {name}")
    return path


def parse_offset(value: str | int) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise ValueError("firmware offset must be an integer or numeric string")
    try:
        return value if isinstance(value, int) else int(value, 0)
    except ValueError as exc:
        raise ValueError(f"invalid firmware offset: {value!r}") from exc


def load_board_contract(config_value: str | Path = DEFAULT_CONFIG) -> dict[str, str]:
    config_path = Path(config_value)
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if config.get("schema_version") != 1:
        raise ValueError(f"unsupported CI configuration schema in {config_path}")
    board = config.get("board")
    if not isinstance(board, dict):
        raise ValueError("CI configuration must define board metadata")
    contract: dict[str, str] = {}
    for config_field, manifest_field in (
        ("name", "board"),
        ("module", "hardware_variant"),
        ("target", "target"),
        ("bootloader_offset", "image_header_offset"),
    ):
        value = board.get(config_field)
        if not isinstance(value, str) or not value:
            raise ValueError(f"CI board field {config_field!r} must be a non-empty string")
        contract[manifest_field] = (
            f"0x{parse_offset(value):x}" if config_field == "bootloader_offset" else value
        )
    return contract


def validate_timestamp(value: Any) -> None:
    if not isinstance(value, str) or not value.endswith("Z"):
        raise ValueError("timestamp_utc must be an ISO 8601 UTC timestamp ending in Z")
    try:
        parsed = datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError as exc:
        raise ValueError("timestamp_utc is not a valid ISO 8601 timestamp") from exc
    if parsed.tzinfo is None or parsed.utcoffset() != timezone.utc.utcoffset(parsed):
        raise ValueError("timestamp_utc must use UTC")


def quote_shell(value: str) -> str:
    return "'" + value.replace("'", "'\"'\"'") + "'"


def quote_batch(value: str) -> str:
    return '"' + value.replace('"', '\\"') + '"'


def option_value(tokens: list[str], option: str) -> str:
    if tokens.count(option) != 1:
        raise ValueError(f"flash_command must contain exactly one {option}")
    index = tokens.index(option)
    if index + 1 >= len(tokens):
        raise ValueError(f"flash_command is missing the value for {option}")
    return tokens[index + 1]


def expected_flash_helpers(manifest: dict[str, Any]) -> dict[str, bytes]:
    command = manifest["flash_command"]
    if not isinstance(command, str) or not command:
        raise ValueError("flash_command must be a non-empty string")
    try:
        tokens = shlex.split(command, posix=True)
    except ValueError as exc:
        raise ValueError("flash_command is not valid shell-style text") from exc
    if " ".join(tokens) != command:
        raise ValueError("flash_command must use canonical single-space formatting")
    if tokens[:3] not in (["python", "-m", "esptool"], ["python3", "-m", "esptool"]):
        raise ValueError("flash_command must invoke python or python3 -m esptool")
    if option_value(tokens, "--chip") != manifest["target"]:
        raise ValueError("flash_command chip does not match the manifest target")
    if option_value(tokens, "--port") != "<PORT>":
        raise ValueError("flash_command port must be the <PORT> placeholder")
    if option_value(tokens, "--baud") != str(manifest["baud"]):
        raise ValueError("flash_command baud does not match the manifest")
    if tokens.count("write-flash") != 1:
        raise ValueError("flash_command must contain exactly one write-flash command")
    if tokens[-2:] != ["0x0", manifest["combined_bin"]]:
        raise ValueError("flash_command must write the combined image at offset 0x0")

    if tokens[0] == "python3":
        shell_tokens = ["$PORT" if token == "<PORT>" else token for token in tokens]
        shell_command = " ".join(
            '"$PORT"' if token == "$PORT" else quote_shell(token)
            for token in shell_tokens
        )
        batch_tokens = ["py", "-3", *tokens[1:]]
        batch_tokens = ["%PORT%" if token == "<PORT>" else token for token in batch_tokens]
        batch_command = " ".join(
            '"%PORT%"' if token == "%PORT%" else quote_batch(token)
            for token in batch_tokens
        )
        shell = f'''#!/usr/bin/env sh
set -eu
PORT="${{1:-}}"
if [ -z "$PORT" ]; then
    echo "Usage: $0 /dev/ttyUSB0"
    exit 2
fi
cd "$(dirname "$0")"
{shell_command}
'''
        batch = f'''@echo off
set "PORT=%~1"
if "%PORT%"=="" (
  echo Usage: flash.bat COMx
  exit /b 2
)
cd /d "%~dp0"
{batch_command}
'''
        return {
            "flash.sh": shell.encode("utf-8"),
            "flash.bat": batch.encode("utf-8"),
            "flash_args.txt": (command + "\n").encode("utf-8"),
        }

    shell_tokens = ["$PYTHON", *tokens[1:]]
    shell_tokens = ["$PORT" if token == "<PORT>" else token for token in shell_tokens]
    shell_command = " ".join(
        '"$PYTHON"'
        if token == "$PYTHON"
        else '"$PORT"'
        if token == "$PORT"
        else quote_shell(token)
        for token in shell_tokens
    )
    python_batch_tokens = ["%PORT%" if token == "<PORT>" else token for token in tokens]
    py_batch_tokens = ["py", "-3", *tokens[1:]]
    py_batch_tokens = ["%PORT%" if token == "<PORT>" else token for token in py_batch_tokens]
    python_batch_command = " ".join(
        '"%PORT%"' if token == "%PORT%" else quote_batch(token)
        for token in python_batch_tokens
    )
    py_batch_command = " ".join(
        '"%PORT%"' if token == "%PORT%" else quote_batch(token)
        for token in py_batch_tokens
    )
    shell = f'''#!/usr/bin/env sh
set -eu
PORT="${{1:-}}"
if [ -z "$PORT" ]; then
    echo "Usage: $0 /dev/ttyUSB0"
    exit 2
fi
cd "$(dirname "$0")"
if command -v python >/dev/null 2>&1 && python -c 'import esptool' >/dev/null 2>&1; then
    PYTHON=python
elif command -v python3 >/dev/null 2>&1 && python3 -c 'import esptool' >/dev/null 2>&1; then
    PYTHON=python3
else
    echo "Error: esptool is not installed for Python 3." >&2
    echo "Install it with: python -m pip install esptool" >&2
    echo "Or use: python3 -m pip install esptool" >&2
    exit 127
fi
{shell_command}
'''
    batch = f'''@echo off
setlocal
set "PORT=%~1"
if "%PORT%"=="" (
  echo Usage: flash.bat COMx
  exit /b 2
)
cd /d "%~dp0"
where python >nul 2>&1
if not errorlevel 1 (
  "python" "-c" "import esptool" >nul 2>&1
  if not errorlevel 1 goto use_python
)
where py >nul 2>&1
if not errorlevel 1 (
  "py" "-3" "-c" "import esptool" >nul 2>&1
  if not errorlevel 1 goto use_py
)
echo Error: esptool is not installed for Python 3. 1>&2
echo Install it with: python -m pip install esptool 1>&2
echo Or use: py -3 -m pip install esptool 1>&2
exit /b 127

:use_python
{python_batch_command}
exit /b %ERRORLEVEL%

:use_py
{py_batch_command}
exit /b %ERRORLEVEL%
'''
    return {
        "flash.sh": shell.encode("utf-8"),
        "flash.bat": batch.encode("utf-8"),
        "flash_args.txt": (command + "\n").encode("utf-8"),
    }


def validate_manifest(
    manifest: dict[str, Any],
    read_file: Callable[[str], bytes],
    available: set[str],
    board_contract: dict[str, str] | None = None,
) -> None:
    if manifest.get("schema_version") != 1:
        raise ValueError("unsupported or missing manifest schema_version")
    for field in (
        "name",
        "board",
        "hardware_variant",
        "framework",
        "framework_version",
        "target",
        "project_path",
        "git_sha",
        "timestamp_utc",
        "baud",
        "files",
        "flash_command",
        "combined_bin",
        "image_header_offset",
        "segments",
    ):
        if field not in manifest:
            raise ValueError(f"manifest is missing {field!r}")
    if manifest["framework"] not in ("esp-idf", "arduino"):
        raise ValueError(f"unsupported source-build framework: {manifest['framework']}")
    for field in ("name", "board", "hardware_variant", "framework_version", "target"):
        if not isinstance(manifest[field], str) or not manifest[field]:
            raise ValueError(f"manifest field {field!r} must be a non-empty string")
    if not re.fullmatch(r"esp32[a-z0-9]+", manifest["target"]):
        raise ValueError(f"invalid firmware target: {manifest['target']}")
    if board_contract:
        for field, expected in board_contract.items():
            if manifest.get(field) != expected:
                raise ValueError(
                    f"manifest {field} {manifest.get(field)!r} does not match configured value {expected!r}"
                )
    project_path = str(manifest["project_path"])
    safe_member(project_path)
    git_sha = str(manifest["git_sha"])
    if git_sha and not re.fullmatch(r"[0-9a-f]{7,64}", git_sha):
        raise ValueError("git_sha must be empty or a hexadecimal commit identifier")
    validate_timestamp(manifest["timestamp_utc"])
    if not str(manifest["baud"]).isdigit() or int(manifest["baud"]) <= 0:
        raise ValueError("baud must be a positive integer")
    runtime = manifest.get("runtime_resources", {})
    if not isinstance(runtime, dict):
        raise ValueError("runtime_resources must be an object")
    if runtime.get("sdcard_included") is not False:
        raise ValueError("manifest must explicitly state that SD-card resources are excluded")
    if runtime.get("source_path") is not None:
        raise ValueError("manifest must not reference repository-managed runtime resources")

    records = manifest["files"]
    segments = manifest["segments"]
    if not isinstance(records, list) or not isinstance(segments, list):
        raise ValueError("files and segments must be lists")
    if not records:
        raise ValueError("manifest has no firmware records")
    by_path: dict[str, dict[str, Any]] = {}
    for record in records:
        if not isinstance(record, dict):
            raise ValueError("firmware records must be objects")
        for field in ("offset", "file", "source", "size", "sha256"):
            if field not in record:
                raise ValueError(f"firmware record is missing {field!r}")
        relative = str(record["file"])
        safe_member(relative)
        if parse_offset(record["offset"]) < 0:
            raise ValueError(f"negative firmware offset for {relative}")
        if not isinstance(record["source"], str) or not record["source"]:
            raise ValueError(f"firmware source must be a non-empty string for {relative}")
        if relative not in available:
            raise ValueError(f"manifest references a missing file: {relative}")
        data = read_file(relative)
        if len(data) != int(record["size"]):
            raise ValueError(f"size mismatch for {relative}")
        if sha256_bytes(data) != str(record["sha256"]).lower():
            raise ValueError(f"SHA-256 mismatch for {relative}")
        if relative in by_path:
            raise ValueError(f"duplicate firmware record for {relative}")
        by_path[relative] = record

    combined = str(manifest["combined_bin"])
    if combined not in by_path:
        raise ValueError("combined_bin is not listed in manifest files")
    if parse_offset(by_path[combined]["offset"]) != 0:
        raise ValueError("combined firmware must be flashed at offset 0x0")
    combined_data = read_file(combined)
    header_offset = parse_offset(manifest["image_header_offset"])
    if header_offset < 0 or header_offset >= len(combined_data):
        raise ValueError("image_header_offset is outside the combined firmware")
    actual_header_offset = next(
        (index for index, value in enumerate(combined_data) if value != 0xFF), None
    )
    if actual_header_offset is None:
        raise ValueError("combined firmware contains only erased-flash padding")
    if actual_header_offset != header_offset:
        raise ValueError(
            f"image_header_offset mismatch: expected {header_offset:#x}, got {actual_header_offset:#x}"
        )
    if combined_data[header_offset] != 0xE9:
        raise ValueError("combined firmware image header is not Espressif magic 0xe9")
    chip_id_start = header_offset + ESP_IMAGE_CHIP_ID_OFFSET
    chip_id_end = chip_id_start + 2
    if chip_id_end > len(combined_data):
        raise ValueError("combined firmware is too short to contain an Espressif chip ID")
    expected_chip_id = ESP_IMAGE_CHIP_IDS.get(manifest["target"])
    if expected_chip_id is None:
        raise ValueError(f"unsupported firmware target: {manifest['target']!r}")
    actual_chip_id = int.from_bytes(combined_data[chip_id_start:chip_id_end], "little")
    if actual_chip_id != expected_chip_id:
        raise ValueError(
            f"firmware chip ID mismatch for {manifest['target']}: "
            f"expected {expected_chip_id}, got {actual_chip_id}"
        )

    segment_paths: set[str] = set()
    position = 0
    for record in sorted(segments, key=lambda item: parse_offset(item["offset"])):
        if not isinstance(record, dict):
            raise ValueError("firmware segments must be objects")
        for field in ("offset", "file", "source", "size", "sha256"):
            if field not in record:
                raise ValueError(f"firmware segment is missing {field!r}")
        relative = str(record["file"])
        if relative in segment_paths:
            raise ValueError(f"duplicate firmware segment for {relative}")
        segment_paths.add(relative)
        file_record = by_path.get(relative)
        if file_record is None:
            raise ValueError(f"segment is not listed in files: {relative}")
        if parse_offset(file_record["offset"]) != parse_offset(record["offset"]):
            raise ValueError(f"segment offset differs from files for {relative}")
        if int(file_record["size"]) != int(record["size"]):
            raise ValueError(f"segment size differs from files for {relative}")
        if str(file_record["sha256"]).lower() != str(record["sha256"]).lower():
            raise ValueError(f"segment SHA-256 differs from files for {relative}")
        if file_record["source"] != record["source"]:
            raise ValueError(f"segment source differs from files for {relative}")
        offset = parse_offset(record["offset"])
        if offset < position:
            raise ValueError(f"overlapping firmware segment: {record['file']}")
        if combined_data[position:offset] != b"\xff" * (offset - position):
            raise ValueError(f"combined firmware padding before {relative} is not erased flash")
        segment_data = read_file(relative)
        end = offset + len(segment_data)
        if combined_data[offset:end] != segment_data:
            raise ValueError(f"combined firmware does not contain segment {relative} at {offset:#x}")
        position = end
    if not segments:
        raise ValueError("manifest has no source firmware segments")
    if segment_paths != set(by_path) - {combined}:
        raise ValueError("files must contain exactly the combined image and source segments")
    if len(combined_data) != position:
        raise ValueError(
            f"combined firmware length {len(combined_data)} does not match segment layout {position}"
        )

    binary_members = {name for name in available if name.lower().endswith(".bin")}
    if binary_members != set(by_path):
        untracked = sorted(binary_members - set(by_path))
        missing = sorted(set(by_path) - binary_members)
        raise ValueError(f"binary manifest mismatch; untracked={untracked}, non-binary={missing}")

    checksum_lines = read_file("SHA256SUMS").decode("utf-8").splitlines()
    checksums: dict[str, str] = {}
    for line in checksum_lines:
        if not line:
            continue
        try:
            digest, relative = line.split("  ", 1)
        except ValueError as exc:
            raise ValueError("invalid SHA256SUMS line") from exc
        if relative in checksums:
            raise ValueError(f"duplicate SHA256SUMS entry: {relative}")
        checksums[relative] = digest.lower()
    expected_checksums = {path: str(record["sha256"]).lower() for path, record in by_path.items()}
    if checksums != expected_checksums:
        raise ValueError("SHA256SUMS does not match the manifest")

    expected_members = REQUIRED_FILES | set(by_path)
    if available != expected_members:
        unexpected = sorted(available - expected_members)
        missing = sorted(expected_members - available)
        raise ValueError(
            f"package file inventory mismatch; unexpected={unexpected}, missing={missing}"
        )
    for relative, expected in expected_flash_helpers(manifest).items():
        if read_file(relative) != expected:
            raise ValueError(f"{relative} does not match the manifest flash_command")


def validate_zip(path: Path, board_contract: dict[str, str] | None = None) -> None:
    if not path.is_file():
        raise FileNotFoundError(path)
    with zipfile.ZipFile(path) as archive:
        infos = archive.infolist()
        if any(info.is_dir() for info in infos):
            raise ValueError("archive must not contain explicit directory entries")
        names = [info.filename for info in infos]
        if len(names) != len(set(names)):
            raise ValueError("archive contains duplicate member names")
        safe_paths = [safe_member(name) for name in names]
        if any(len(member.parts) < 2 for member in safe_paths):
            raise ValueError("every archive member must be inside the package directory")
        if len(safe_paths) != len(set(safe_paths)):
            raise ValueError("archive contains duplicate canonical member paths")
        for info in infos:
            file_type = stat.S_IFMT((info.external_attr >> 16) & 0xFFFF)
            if file_type not in (0, stat.S_IFREG):
                raise ValueError(f"archive member is not a regular file: {info.filename}")
            if info.flag_bits & 0x1:
                raise ValueError(f"archive member must not be encrypted: {info.filename}")
        roots = {member.parts[0] for member in safe_paths}
        if len(roots) != 1:
            raise ValueError("archive must contain exactly one top-level package directory")
        root = next(iter(roots))
        relative_names = {PurePosixPath(*member.parts[1:]).as_posix() for member in safe_paths}
        missing = REQUIRED_FILES - relative_names
        if missing:
            raise ValueError("archive is missing required files: " + ", ".join(sorted(missing)))
        for relative in relative_names:
            parts = [part.lower() for part in PurePosixPath(relative).parts]
            if "sdcard" in parts:
                raise ValueError(f"archive contains an SD-card runtime resource: {relative}")

        def read_file(relative: str) -> bytes:
            return archive.read(f"{root}/{relative}")

        manifest = json.loads(read_file("manifest.json").decode("utf-8"))
        if manifest.get("name") != root:
            raise ValueError("manifest name does not match the ZIP top-level directory")
        if path.stem != root:
            raise ValueError("ZIP filename does not match the package name")
        validate_manifest(manifest, read_file, relative_names, board_contract)


def validate_directory(path: Path, board_contract: dict[str, str] | None = None) -> None:
    if not path.is_dir():
        raise FileNotFoundError(path)
    available = {
        item.relative_to(path).as_posix() for item in path.rglob("*") if item.is_file()
    }
    missing = REQUIRED_FILES - available
    if missing:
        raise ValueError("package is missing required files: " + ", ".join(sorted(missing)))
    if any("sdcard" in [part.lower() for part in PurePosixPath(name).parts] for name in available):
        raise ValueError("package contains SD-card runtime resources")

    def read_file(relative: str) -> bytes:
        return (path / relative).read_bytes()

    manifest = json.loads(read_file("manifest.json").decode("utf-8"))
    if manifest.get("name") != path.name:
        raise ValueError("manifest name does not match the package directory")
    validate_manifest(manifest, read_file, available, board_contract)


def validate_path(path: Path, board_contract: dict[str, str] | None = None) -> None:
    if path.suffix.lower() == ".zip":
        validate_zip(path, board_contract)
    else:
        validate_directory(path, board_contract)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packages", nargs="+", help="Firmware ZIP archives or unpacked package directories.")
    parser.add_argument("--config", default=DEFAULT_CONFIG.as_posix())
    args = parser.parse_args()
    try:
        board_contract = load_board_contract(args.config)
        for value in args.packages:
            path = Path(value)
            validate_path(path, board_contract)
            digest = sha256_bytes(path.read_bytes()) if path.is_file() else "directory"
            print(f"valid: {path.as_posix()} ({digest})")
    except (OSError, ValueError, KeyError, json.JSONDecodeError, zipfile.BadZipFile) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
