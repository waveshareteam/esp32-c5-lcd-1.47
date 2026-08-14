#!/usr/bin/env python3
"""Validate the checked-in factory firmware against its repository metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


DEFAULT_CONFIG = Path("config/factory-firmware.json")
DEFAULT_CI_CONFIG = Path("config/ci.json")
EXPECTED_SCHEMA = 1
ESP_IMAGE_HEADER = b"\xe9"
ESP_IMAGE_CHIP_ID_OFFSET = 12
ESP_IMAGE_CHIP_IDS = {
    "esp32c5": 23,
}
REQUIRED_FIELDS = {
    "schema_version",
    "name",
    "board",
    "hardware_variant",
    "target",
    "image",
    "offset",
    "image_header_offset",
    "size",
    "sha256",
    "sdcard_resources_included",
}


def resolve_inside(repo: Path, value: str | Path, description: str) -> Path:
    candidate = Path(value)
    resolved = candidate.resolve() if candidate.is_absolute() else (repo / candidate).resolve()
    try:
        resolved.relative_to(repo)
    except ValueError as exc:
        raise ValueError(f"{description} must be inside the repository") from exc
    return resolved


def parse_offset(value: object) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise ValueError("factory firmware offset must be an integer or numeric string")
    try:
        return value if isinstance(value, int) else int(value, 0)
    except ValueError as exc:
        raise ValueError("factory firmware offset is not a valid integer") from exc


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_board_contract(repo: Path, config_value: str | Path) -> tuple[Path, dict[str, str]]:
    config_path = resolve_inside(repo, config_value, "CI configuration")
    if not config_path.is_file():
        raise FileNotFoundError(f"CI configuration not found: {config_path}")
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if not isinstance(config, dict) or config.get("schema_version") != EXPECTED_SCHEMA:
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
    return config_path, result


def validate_factory_firmware(
    repo_value: str | Path = ".",
    config_value: str | Path = DEFAULT_CONFIG,
    ci_config_value: str | Path = DEFAULT_CI_CONFIG,
) -> dict[str, Any]:
    repo = Path(repo_value).resolve()
    if not repo.is_dir():
        raise FileNotFoundError(f"repository directory not found: {repo}")

    config_path = resolve_inside(repo, config_value, "factory firmware configuration")
    if not config_path.is_file():
        raise FileNotFoundError(f"factory firmware configuration not found: {config_path}")
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if not isinstance(config, dict):
        raise ValueError("factory firmware configuration must be a JSON object")

    missing = REQUIRED_FIELDS - config.keys()
    if missing:
        raise ValueError("factory firmware configuration is missing: " + ", ".join(sorted(missing)))
    if config["schema_version"] != EXPECTED_SCHEMA:
        raise ValueError(f"unsupported factory firmware schema: {config['schema_version']!r}")

    ci_config_path, board_contract = load_board_contract(repo, ci_config_value)

    for field in ("name", "board", "hardware_variant", "target", "image"):
        if not isinstance(config[field], str) or not config[field].strip():
            raise ValueError(f"factory firmware field {field!r} must be a non-empty string")
    expected_values = {
        "board": board_contract["name"],
        "hardware_variant": board_contract["module"],
        "target": board_contract["target"],
    }
    for field, expected in expected_values.items():
        if config[field] != expected:
            raise ValueError(
                f"factory firmware {field} {config[field]!r} does not match CI configuration {expected!r}"
            )
    if parse_offset(config["offset"]) != 0:
        raise ValueError("factory firmware must be flashed at offset 0x0")
    header_offset = parse_offset(config["image_header_offset"])
    if header_offset < 0:
        raise ValueError("factory firmware image_header_offset must not be negative")
    configured_header_offset = parse_offset(board_contract["bootloader_offset"])
    if header_offset != configured_header_offset:
        raise ValueError(
            "factory firmware image_header_offset does not match the configured bootloader offset"
        )
    if config["sdcard_resources_included"] is not False:
        raise ValueError("factory firmware must explicitly exclude SD-card runtime resources")

    expected_size = config["size"]
    if isinstance(expected_size, bool) or not isinstance(expected_size, int) or expected_size <= 0:
        raise ValueError("factory firmware size must be a positive integer")
    expected_sha = config["sha256"]
    if not isinstance(expected_sha, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected_sha):
        raise ValueError("factory firmware sha256 must contain 64 hexadecimal characters")

    configured_image = Path(config["image"])
    image_path = resolve_inside(repo, config["image"], "factory firmware image")
    image_relative = image_path.relative_to(repo)
    if image_relative.parent != Path("firmware") or configured_image.suffix != ".bin":
        raise ValueError("factory firmware image must be a top-level firmware/*.bin file")
    if not image_path.is_file():
        raise FileNotFoundError(f"factory firmware image not found: {image_path}")
    actual_size = image_path.stat().st_size
    if actual_size != expected_size:
        raise ValueError(
            f"factory firmware size mismatch: expected {expected_size}, got {actual_size}"
        )
    actual_sha = sha256_file(image_path)
    if actual_sha.lower() != expected_sha.lower():
        raise ValueError(
            f"factory firmware SHA-256 mismatch: expected {expected_sha.lower()}, got {actual_sha}"
        )
    image_data = image_path.read_bytes()
    if header_offset >= len(image_data):
        raise ValueError("factory firmware image_header_offset is outside the image")
    actual_header_offset = next(
        (index for index, value in enumerate(image_data) if value != 0xFF), None
    )
    if actual_header_offset != header_offset:
        actual = "none" if actual_header_offset is None else f"{actual_header_offset:#x}"
        raise ValueError(
            f"factory firmware image header offset mismatch: expected {header_offset:#x}, got {actual}"
        )
    if image_data[header_offset : header_offset + 1] != ESP_IMAGE_HEADER:
        raise ValueError("factory firmware image header is not Espressif magic 0xe9")
    chip_id_end = header_offset + ESP_IMAGE_CHIP_ID_OFFSET + 2
    if chip_id_end > len(image_data):
        raise ValueError("factory firmware image is too short to contain an Espressif chip ID")
    expected_chip_id = ESP_IMAGE_CHIP_IDS.get(config["target"])
    if expected_chip_id is None:
        raise ValueError(f"unsupported factory firmware target: {config['target']!r}")
    chip_id_offset = header_offset + ESP_IMAGE_CHIP_ID_OFFSET
    actual_chip_id = int.from_bytes(image_data[chip_id_offset:chip_id_end], "little")
    if actual_chip_id != expected_chip_id:
        raise ValueError(
            f"factory firmware chip ID mismatch: expected {expected_chip_id}, got {actual_chip_id}"
        )

    return {
        "config": config_path.relative_to(repo).as_posix(),
        "ci_config": ci_config_path.relative_to(repo).as_posix(),
        "image": image_relative.as_posix(),
        "size": actual_size,
        "sha256": actual_sha,
        "target": config["target"],
        "chip_id": actual_chip_id,
        "offset": "0x0",
        "image_header_offset": f"0x{header_offset:x}",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=".")
    parser.add_argument("--config", default=DEFAULT_CONFIG.as_posix())
    parser.add_argument("--ci-config", default=DEFAULT_CI_CONFIG.as_posix())
    args = parser.parse_args()
    try:
        result = validate_factory_firmware(args.repo, args.config, args.ci_config)
        print(
            f"valid: {result['image']} "
            f"({result['size']} bytes, target={result['target']}, "
            f"chip_id={result['chip_id']}, sha256={result['sha256']})"
        )
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
