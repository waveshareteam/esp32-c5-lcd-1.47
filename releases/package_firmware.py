#!/usr/bin/env python3
"""Create flashable firmware ZIP archives from maintained source builds."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import sys
import tempfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


DEFAULT_BAUD = "460800"
DEFAULT_CONFIG = Path("config/ci.json")
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
SCHEMA_VERSION = 1
ESP_IMAGE_CHIP_ID_OFFSET = 12
ESP_IMAGE_CHIP_IDS = {
    "esp32c5": 23,
}


def slugify(value: str) -> str:
    value = value.strip().replace("\\", "/")
    value = re.sub(r"[^A-Za-z0-9._-]+", "-", value)
    value = re.sub(r"-+", "-", value).strip("-")
    return "firmware" if value in ("", ".", "..") else value


def parse_offset(value: str | int) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise ValueError("firmware offset must be an integer or numeric string")
    try:
        return value if isinstance(value, int) else int(value, 0)
    except ValueError as exc:
        raise ValueError(f"invalid firmware offset: {value!r}") from exc


def normalized_offset(value: str | int) -> str:
    return f"0x{parse_offset(value):x}"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_from(root: Path, value: str) -> Path:
    path = Path(value)
    return path.resolve() if path.is_absolute() else (root / path).resolve()


def safe_project_path(project: Path, repo: Path) -> str:
    try:
        return project.resolve().relative_to(repo.resolve()).as_posix()
    except ValueError as exc:
        raise ValueError("project must be inside the repository") from exc


def load_board_config(repo: Path, config_value: str) -> dict[str, str]:
    config_path = resolve_from(repo, config_value)
    try:
        config_path.relative_to(repo)
    except ValueError as exc:
        raise ValueError("CI configuration must be inside the repository") from exc
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if config.get("schema_version") != 1:
        raise ValueError(f"unsupported CI configuration schema in {config_path}")
    board = config.get("board")
    if not isinstance(board, dict):
        raise ValueError("CI configuration must define board metadata")
    result: dict[str, str] = {}
    for field in ("name", "module", "target", "bootloader_offset"):
        value = board.get(field)
        if not isinstance(value, str) or not value:
            raise ValueError(f"CI board field {field!r} must be a non-empty string")
        result[field] = value
    return result


def normalized_git_sha(value: str) -> str:
    sha = value.strip().lower()
    if sha and not re.fullmatch(r"[0-9a-f]{7,64}", sha):
        raise ValueError("--git-sha must be a 7 to 64 character hexadecimal commit identifier")
    return sha


def artifact_name_with_sha(value: str, git_sha: str) -> str:
    name = slugify(value)
    if not git_sha:
        return name
    suffix = git_sha[:7]
    return name if name.lower().endswith(f"-{suffix}") else slugify(f"{name}-{suffix}")


def timestamp_utc() -> str:
    source_date_epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if source_date_epoch is None:
        timestamp = datetime.now(timezone.utc)
    else:
        try:
            epoch = int(source_date_epoch)
        except ValueError as exc:
            raise ValueError("SOURCE_DATE_EPOCH must be an integer") from exc
        if epoch < 0:
            raise ValueError("SOURCE_DATE_EPOCH must not be negative")
        timestamp = datetime.fromtimestamp(epoch, timezone.utc)
    return timestamp.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def quote_shell(value: str) -> str:
    return "'" + value.replace("'", "'\"'\"'") + "'"


def quote_batch(value: str) -> str:
    return '"' + value.replace('"', '\\"') + '"'


def write_text(path: Path, content: str, executable: bool = False) -> None:
    path.write_text(content, encoding="utf-8", newline="\n")
    mode = 0o755 if executable else 0o644
    path.chmod(mode)


def file_record(path: Path, package_dir: Path, offset: str | int, source: str) -> dict[str, Any]:
    return {
        "offset": normalized_offset(offset),
        "file": path.relative_to(package_dir).as_posix(),
        "source": source.replace("\\", "/"),
        "size": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def copy_segment(
    source: Path,
    firmware_dir: Path,
    package_dir: Path,
    offset: str | int,
    source_label: str,
) -> dict[str, Any]:
    if not source.is_file():
        raise FileNotFoundError(f"missing firmware file: {source}")
    destination_name = slugify(f"{normalized_offset(offset)}_{source.name}")
    destination = firmware_dir / destination_name
    shutil.copyfile(source, destination)
    destination.chmod(0o644)
    return file_record(destination, package_dir, offset, source_label)


def write_padding(output, size: int) -> None:
    chunk = b"\xff" * 65536
    while size:
        count = min(size, len(chunk))
        output.write(chunk[:count])
        size -= count


def create_combined_bin(
    package_dir: Path,
    firmware_dir: Path,
    artifact_name: str,
    entries: list[dict[str, Any]],
) -> dict[str, Any]:
    if not entries:
        raise ValueError("no firmware entries available to combine")
    combined_path = firmware_dir / slugify(f"{artifact_name}.combined.bin")
    position = 0
    with combined_path.open("wb") as output:
        for entry in sorted(entries, key=lambda item: parse_offset(item["offset"])):
            offset = parse_offset(entry["offset"])
            source = package_dir / entry["file"]
            if offset < position:
                raise ValueError(
                    f"firmware segment {entry['file']} at {entry['offset']} overlaps a previous segment"
                )
            write_padding(output, offset - position)
            with source.open("rb") as input_file:
                shutil.copyfileobj(input_file, output)
            position = offset + source.stat().st_size
    combined_path.chmod(0o644)
    return file_record(combined_path, package_dir, "0x0", "combined firmware image")


def image_header_offset(path: Path) -> int:
    offset = 0
    with path.open("rb") as image:
        for chunk in iter(lambda: image.read(65536), b""):
            for value in chunk:
                if value == 0xFF:
                    offset += 1
                    continue
                if value != 0xE9:
                    raise ValueError(
                        f"combined firmware first non-padding byte is 0x{value:02x}, expected 0xe9"
                    )
                return offset
    raise ValueError("combined firmware contains only erased-flash padding")


def image_chip_id(path: Path, header_offset: int) -> int:
    with path.open("rb") as image:
        image.seek(header_offset + ESP_IMAGE_CHIP_ID_OFFSET)
        value = image.read(2)
    if len(value) != 2:
        raise ValueError("combined firmware is too short to contain an Espressif chip ID")
    return int.from_bytes(value, "little")


def esp_idf_segments(build_dir: Path, firmware_dir: Path, package_dir: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    flasher_args_path = build_dir / "flasher_args.json"
    if not flasher_args_path.is_file():
        raise FileNotFoundError(f"missing ESP-IDF flasher args: {flasher_args_path}")
    data = json.loads(flasher_args_path.read_text(encoding="utf-8"))
    flash_files = data.get("flash_files")
    if not isinstance(flash_files, dict) or not flash_files:
        raise ValueError(f"no flash_files found in {flasher_args_path}")

    entries: list[dict[str, Any]] = []
    for offset, relative in sorted(flash_files.items(), key=lambda item: parse_offset(item[0])):
        source = Path(str(relative))
        if not source.is_absolute():
            source = build_dir / source
        source_value = Path(str(relative))
        source_label = source_value.name if source_value.is_absolute() else source_value.as_posix()
        entries.append(copy_segment(source, firmware_dir, package_dir, offset, source_label))
    return entries, data


def arduino_segments(
    build_dir: Path,
    firmware_dir: Path,
    package_dir: Path,
    bootloader_offset: str,
) -> list[dict[str, Any]]:
    binaries = sorted(build_dir.rglob("*.bin"), key=lambda path: path.as_posix().lower())
    if not binaries:
        raise FileNotFoundError(f"no Arduino .bin files found in {build_dir}")

    merged = [path for path in binaries if path.name.endswith(".merged.bin")]
    if len(merged) > 1:
        raise ValueError(f"expected at most one Arduino merged binary, found {len(merged)}")
    if merged:
        return [copy_segment(merged[0], firmware_dir, package_dir, "0x0", merged[0].name)]

    selected: list[tuple[str, Path]] = []
    bootloaders: list[Path] = []
    partition_tables: list[Path] = []
    application_candidates: list[Path] = []
    for path in binaries:
        name = path.name
        if name.endswith(".bootloader.bin"):
            bootloaders.append(path)
        elif name.endswith(".partitions.bin"):
            partition_tables.append(path)
        elif name == "boot_app0.bin" or name.endswith(".boot_app0.bin"):
            selected.append(("0xe000", path))
        elif not any(token in name for token in (".bootloader.", ".partitions.", ".merged.")):
            application_candidates.append(path)
    if len(application_candidates) != 1:
        raise ValueError(
            f"expected one Arduino application binary, found {len(application_candidates)} in {build_dir}"
        )
    if len(bootloaders) != 1:
        raise ValueError(
            f"expected one Arduino bootloader binary, found {len(bootloaders)} in {build_dir}"
        )
    if len(partition_tables) != 1:
        raise ValueError(
            f"expected one Arduino partition-table binary, found {len(partition_tables)} in {build_dir}"
        )
    selected.extend(
        (
            (bootloader_offset, bootloaders[0]),
            ("0x8000", partition_tables[0]),
        )
    )
    selected.append(("0x10000", application_candidates[0]))
    return [
        copy_segment(path, firmware_dir, package_dir, offset, path.name)
        for offset, path in sorted(selected, key=lambda item: parse_offset(item[0]))
    ]


def shell_command(parts: Iterable[str]) -> str:
    return " ".join('"$PORT"' if part == "$PORT" else quote_shell(part) for part in parts)


def batch_command(parts: Iterable[str]) -> str:
    return " ".join('"%PORT%"' if part == "$PORT" else quote_batch(part) for part in parts)


def esptool_command(chip: str, before: str, after: str, write_args: list[str], image: str) -> list[str]:
    before = before.replace("_", "-")
    after = after.replace("_", "-")
    return [
        "python3",
        "-m",
        "esptool",
        "--chip",
        chip,
        "--port",
        "$PORT",
        "--baud",
        DEFAULT_BAUD,
        "--before",
        before,
        "--after",
        after,
        "write-flash",
        *write_args,
        "0x0",
        image,
    ]


def write_flash_helpers(package_dir: Path, command: list[str], artifact_name: str) -> None:
    batch_parts = ["py", "-3", *command[1:]]
    shell = f'''#!/usr/bin/env sh
set -eu
PORT="${{1:-}}"
if [ -z "$PORT" ]; then
    echo "Usage: $0 /dev/ttyUSB0"
    exit 2
fi
cd "$(dirname "$0")"
{shell_command(command)}
'''
    batch = f'''@echo off
set "PORT=%~1"
if "%PORT%"=="" (
  echo Usage: flash.bat COMx
  exit /b 2
)
cd /d "%~dp0"
{batch_command(batch_parts)}
'''
    args_text = " ".join("<PORT>" if item == "$PORT" else item for item in command) + "\n"
    write_text(package_dir / "flash.sh", shell, executable=True)
    write_text(package_dir / "flash.bat", batch)
    write_text(package_dir / "flash_args.txt", args_text)
    write_text(
        package_dir / "README.md",
        f'''# {artifact_name}

Install `esptool` with `python3 -m pip install esptool`, then flash the complete
image at offset `0x0` on Linux or macOS:

```bash
./flash.sh /dev/ttyUSB0
```

On Windows, install with `py -3 -m pip install esptool`, then use
`flash.bat COMx`.

The archive contains only firmware produced by this source build. External
storage content, credentials, and device-specific runtime data are not included.
''',
    )


def write_checksums(package_dir: Path, records: list[dict[str, Any]]) -> None:
    unique = {record["file"]: record["sha256"] for record in records}
    content = "".join(f"{digest}  {path}\n" for path, digest in sorted(unique.items()))
    write_text(package_dir / "SHA256SUMS", content)


def create_zip(package_dir: Path, zip_path: Path) -> None:
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    if zip_path.exists():
        raise FileExistsError(f"refusing to replace staging archive: {zip_path}")
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(package_dir.rglob("*"), key=lambda item: item.as_posix()):
            if not path.is_file():
                continue
            archive_name = path.relative_to(package_dir.parent).as_posix()
            info = zipfile.ZipInfo(archive_name, date_time=ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            permissions = 0o755 if path.name == "flash.sh" else 0o644
            info.external_attr = (stat.S_IFREG | permissions) << 16
            archive.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def package(args: argparse.Namespace) -> Path:
    repo = Path(args.repo).resolve()
    board_config = load_board_config(repo, args.config)
    output_dir = resolve_from(repo, args.output_dir)
    flasher_args: dict[str, Any] = {}
    if not args.project or not args.build_dir:
        raise ValueError("--project and --build-dir are required for framework build outputs")
    project = resolve_from(repo, args.project)
    build_dir = resolve_from(repo, args.build_dir)
    project_path = safe_project_path(project, repo)
    if not project.is_dir():
        raise FileNotFoundError(f"project directory not found: {project}")
    if not build_dir.is_dir():
        raise FileNotFoundError(f"build directory not found: {build_dir}")

    target = args.target or board_config["target"]
    if target != board_config["target"]:
        raise ValueError(
            f"firmware target {target!r} does not match configured board target "
            f"{board_config['target']!r}"
        )
    framework_version = args.framework_version or ""
    git_sha = normalized_git_sha(args.git_sha)
    default_name = f"{project.name}-{args.framework}-{framework_version or 'unversioned'}-{target}"
    artifact_name = artifact_name_with_sha(args.name or default_name, git_sha)
    output_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix=".firmware-package-", dir=output_dir) as staging_value:
        staging_dir = Path(staging_value)
        package_dir = staging_dir / artifact_name
        firmware_dir = package_dir / "bin"
        firmware_dir.mkdir(parents=True)

        if args.framework == "esp-idf":
            segments, flasher_args = esp_idf_segments(build_dir, firmware_dir, package_dir)
            extra = flasher_args.get("extra_esptool_args", {})
            build_target = str(extra.get("chip") or target)
            if build_target != target:
                raise ValueError(
                    f"ESP-IDF build target {build_target!r} does not match configured target {target!r}"
                )
            before = str(extra.get("before") or "default-reset")
            after = str(extra.get("after") or "hard-reset")
            write_args = [str(value) for value in flasher_args.get("write_flash_args", [])]
        else:
            segments = arduino_segments(
                build_dir,
                firmware_dir,
                package_dir,
                board_config["bootloader_offset"],
            )
            before, after, write_args = "default-reset", "hard-reset", []
        combined = create_combined_bin(package_dir, firmware_dir, artifact_name, segments)
        header_offset = image_header_offset(package_dir / combined["file"])
        configured_header_offset = parse_offset(board_config["bootloader_offset"])
        if header_offset != configured_header_offset:
            raise ValueError(
                f"combined firmware image header is at {header_offset:#x}, expected "
                f"configured bootloader offset {configured_header_offset:#x}"
            )
        expected_chip_id = ESP_IMAGE_CHIP_IDS.get(target)
        if expected_chip_id is None:
            raise ValueError(f"unsupported firmware target: {target!r}")
        actual_chip_id = image_chip_id(package_dir / combined["file"], header_offset)
        if actual_chip_id != expected_chip_id:
            raise ValueError(
                f"firmware chip ID mismatch for {target}: "
                f"expected {expected_chip_id}, got {actual_chip_id}"
            )

        command = esptool_command(target, before, after, write_args, combined["file"])
        files = [combined, *segments]
        manifest = {
            "schema_version": SCHEMA_VERSION,
            "name": artifact_name,
            "board": board_config["name"],
            "hardware_variant": board_config["module"],
            "framework": args.framework,
            "framework_version": framework_version,
            "target": target,
            "project_path": project_path,
            "git_sha": git_sha,
            "timestamp_utc": timestamp_utc(),
            "baud": DEFAULT_BAUD,
            "combined_bin": combined["file"],
            "image_header_offset": normalized_offset(header_offset),
            "files": files,
            "segments": segments,
            "runtime_resources": {
                "sdcard_included": False,
                "source_path": None,
            },
            "flash_command": " ".join("<PORT>" if item == "$PORT" else item for item in command),
        }
        write_text(package_dir / "manifest.json", json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        write_checksums(package_dir, files)
        write_flash_helpers(package_dir, command, artifact_name)

        staged_zip = staging_dir / f"{artifact_name}.zip"
        create_zip(package_dir, staged_zip)
        zip_path = output_dir / staged_zip.name
        if zip_path.exists() and not zip_path.is_file():
            raise ValueError(f"refusing to replace a non-file output path: {zip_path}")
        staged_zip.replace(zip_path)

    print(zip_path.as_posix())
    return zip_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=".")
    parser.add_argument("--config", default=DEFAULT_CONFIG.as_posix())
    parser.add_argument("--framework", choices=("esp-idf", "arduino"), required=True)
    parser.add_argument("--project", help="Repo-relative project or sketch path.")
    parser.add_argument("--build-dir", help="Framework build output directory.")
    parser.add_argument("--output-dir", default="releases/dist")
    parser.add_argument("--name", help="Firmware archive name.")
    parser.add_argument("--framework-version", help="ESP-IDF tag or Arduino core version.")
    parser.add_argument("--target", help="ESP target override.")
    parser.add_argument("--git-sha", default="")
    args = parser.parse_args()
    try:
        package(args)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
