from __future__ import annotations

import hashlib
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from typing import Callable


RELEASES_DIR = Path(__file__).resolve().parents[1]
PACKAGE_SCRIPT = RELEASES_DIR / "package_firmware.py"
sys.path.insert(0, str(RELEASES_DIR))
import validate_firmware  # noqa: E402


def esp_image(chip_id: int = 23, size: int = 32) -> bytes:
    image = bytearray(b"\xe9" + b"B" * (size - 1))
    image[12:14] = chip_id.to_bytes(2, "little")
    return bytes(image)


class ValidateFirmwareTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.repo = Path(self.temporary.name)
        (self.repo / "config").mkdir()
        self.config = self.repo / "config/ci.json"
        self.config.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "board": {
                        "name": "ESP32-C5-LCD-1.47",
                        "module": "ESP32-C5",
                        "target": "esp32c5",
                        "bootloader_offset": "0x2000",
                    },
                }
            ),
            encoding="utf-8",
        )

        project = self.repo / "examples/esp-idf/demo"
        project.mkdir(parents=True)
        build = self.repo / "build"
        (build / "bootloader").mkdir(parents=True)
        (build / "partition_table").mkdir()
        (build / "bootloader/bootloader.bin").write_bytes(esp_image())
        (build / "partition_table/partition-table.bin").write_bytes(b"P" * 32)
        (build / "demo.bin").write_bytes(b"A" * 64)
        (build / "flasher_args.json").write_text(
            json.dumps(
                {
                    "flash_files": {
                        "0x2000": "bootloader/bootloader.bin",
                        "0x8000": "partition_table/partition-table.bin",
                        "0x10000": "demo.bin",
                    },
                    "extra_esptool_args": {"chip": "esp32c5"},
                }
            ),
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                sys.executable,
                str(PACKAGE_SCRIPT),
                "--repo",
                str(self.repo),
                "--framework",
                "esp-idf",
                "--project",
                "examples/esp-idf/demo",
                "--build-dir",
                "build",
                "--name",
                "validator-fixture",
                "--framework-version",
                "v5.5.5",
                "--git-sha",
                "a" * 40,
                "--output-dir",
                "out",
            ],
            cwd=self.repo,
            capture_output=True,
            text=True,
            env={**os.environ, "SOURCE_DATE_EPOCH": "0"},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.archive = self.repo / f"out/validator-fixture-{'a' * 7}.zip"
        self.board_contract = validate_firmware.load_board_contract(self.config)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def rewrite_archive(self, mutate: Callable[[dict[str, bytes]], None]) -> None:
        with zipfile.ZipFile(self.archive) as source:
            members = {
                info.filename: source.read(info.filename)
                for info in source.infolist()
                if not info.is_dir()
            }
        mutate(members)
        replacement = self.archive.with_suffix(".replacement.zip")
        with zipfile.ZipFile(replacement, "w", compression=zipfile.ZIP_DEFLATED) as output:
            for name, data in sorted(members.items()):
                output.writestr(name, data)
        replacement.replace(self.archive)

    @staticmethod
    def manifest_entry(members: dict[str, bytes]) -> tuple[str, dict]:
        name = next(path for path in members if path.endswith("/manifest.json"))
        return name, json.loads(members[name].decode("utf-8"))

    @staticmethod
    def replace_checksum(members: dict[str, bytes], root: str, relative: str, digest: str) -> None:
        name = f"{root}/SHA256SUMS"
        lines = members[name].decode("utf-8").splitlines()
        members[name] = (
            "\n".join(
                f"{digest}  {relative}" if line.endswith(f"  {relative}") else line
                for line in lines
            )
            + "\n"
        ).encode("utf-8")

    def test_valid_fixture_is_accepted(self) -> None:
        validate_firmware.validate_zip(self.archive, self.board_contract)

    def test_rejects_tampered_combined_image_even_with_updated_digest(self) -> None:
        def mutate(members: dict[str, bytes]) -> None:
            manifest_name, manifest = self.manifest_entry(members)
            root = manifest_name.split("/", 1)[0]
            combined_relative = manifest["combined_bin"]
            combined_name = f"{root}/{combined_relative}"
            combined = bytearray(members[combined_name])
            application = max(manifest["segments"], key=lambda item: int(item["offset"], 0))
            combined[int(application["offset"], 0)] ^= 0x01
            members[combined_name] = bytes(combined)
            digest = hashlib.sha256(combined).hexdigest()
            combined_record = next(
                record for record in manifest["files"] if record["file"] == combined_relative
            )
            combined_record["sha256"] = digest
            members[manifest_name] = json.dumps(manifest).encode("utf-8")
            self.replace_checksum(members, root, combined_relative, digest)

        self.rewrite_archive(mutate)

        with self.assertRaisesRegex(ValueError, "does not contain segment"):
            validate_firmware.validate_zip(self.archive, self.board_contract)

    def test_rejects_combined_image_padded_beyond_the_last_segment(self) -> None:
        def mutate(members: dict[str, bytes]) -> None:
            manifest_name, manifest = self.manifest_entry(members)
            root = manifest_name.split("/", 1)[0]
            combined_relative = manifest["combined_bin"]
            combined_name = f"{root}/{combined_relative}"
            combined = members[combined_name] + b"\xff" * 4096
            members[combined_name] = combined
            digest = hashlib.sha256(combined).hexdigest()
            combined_record = next(
                record for record in manifest["files"] if record["file"] == combined_relative
            )
            combined_record["size"] = len(combined)
            combined_record["sha256"] = digest
            members[manifest_name] = json.dumps(manifest).encode("utf-8")
            self.replace_checksum(members, root, combined_relative, digest)

        self.rewrite_archive(mutate)

        with self.assertRaisesRegex(ValueError, "does not match segment layout"):
            validate_firmware.validate_zip(self.archive, self.board_contract)

    def test_rejects_tampered_board_identity(self) -> None:
        def mutate(members: dict[str, bytes]) -> None:
            manifest_name, manifest = self.manifest_entry(members)
            manifest["board"] = "different-board"
            members[manifest_name] = json.dumps(manifest).encode("utf-8")

        self.rewrite_archive(mutate)

        with self.assertRaisesRegex(ValueError, "does not match configured value"):
            validate_firmware.validate_zip(self.archive, self.board_contract)

    def test_rejects_wrong_chip_id_even_with_updated_digests(self) -> None:
        def mutate(members: dict[str, bytes]) -> None:
            manifest_name, manifest = self.manifest_entry(members)
            root = manifest_name.split("/", 1)[0]
            bootloader = next(
                record for record in manifest["segments"] if int(record["offset"], 0) == 0x2000
            )
            bootloader_name = f"{root}/{bootloader['file']}"
            bootloader_data = bytearray(members[bootloader_name])
            bootloader_data[12:14] = (13).to_bytes(2, "little")
            members[bootloader_name] = bytes(bootloader_data)

            combined_relative = manifest["combined_bin"]
            combined_name = f"{root}/{combined_relative}"
            combined_data = bytearray(members[combined_name])
            combined_data[0x2000 + 12 : 0x2000 + 14] = (13).to_bytes(2, "little")
            members[combined_name] = bytes(combined_data)

            updates = {
                bootloader["file"]: hashlib.sha256(bootloader_data).hexdigest(),
                combined_relative: hashlib.sha256(combined_data).hexdigest(),
            }
            for records in (manifest["files"], manifest["segments"]):
                for record in records:
                    if record["file"] in updates:
                        record["sha256"] = updates[record["file"]]
            for relative, digest in updates.items():
                self.replace_checksum(members, root, relative, digest)
            members[manifest_name] = json.dumps(manifest).encode("utf-8")

        self.rewrite_archive(mutate)

        with self.assertRaisesRegex(ValueError, "chip ID mismatch"):
            validate_firmware.validate_zip(self.archive, self.board_contract)

    def test_rejects_tampered_checksum_file(self) -> None:
        def mutate(members: dict[str, bytes]) -> None:
            manifest_name, manifest = self.manifest_entry(members)
            root = manifest_name.split("/", 1)[0]
            self.replace_checksum(members, root, manifest["combined_bin"], "0" * 64)

        self.rewrite_archive(mutate)

        with self.assertRaisesRegex(ValueError, "SHA256SUMS does not match"):
            validate_firmware.validate_zip(self.archive, self.board_contract)

    def test_rejects_an_unexpected_non_binary_file(self) -> None:
        def mutate(members: dict[str, bytes]) -> None:
            manifest_name, _ = self.manifest_entry(members)
            root = manifest_name.split("/", 1)[0]
            members[f"{root}/unexpected.txt"] = b"not part of the package contract\n"

        self.rewrite_archive(mutate)

        with self.assertRaisesRegex(ValueError, "package file inventory mismatch"):
            validate_firmware.validate_zip(self.archive, self.board_contract)

    def test_rejects_non_canonical_and_out_of_root_members(self) -> None:
        original = self.archive.read_bytes()
        root = self.archive.stem
        invalid_members = (
            f"{root}//README.md",
            f"{root}/./README.md",
            root,
            "../../outside/",
        )
        for member in invalid_members:
            with self.subTest(member=member):
                self.archive.write_bytes(original)
                with zipfile.ZipFile(self.archive, "a") as output:
                    output.writestr(member, b"unexpected\n")
                with self.assertRaises(ValueError):
                    validate_firmware.validate_zip(self.archive, self.board_contract)

    def test_rejects_a_special_file_member(self) -> None:
        replacement = self.archive.with_suffix(".replacement.zip")
        with zipfile.ZipFile(self.archive) as source, zipfile.ZipFile(
            replacement, "w", compression=zipfile.ZIP_DEFLATED
        ) as output:
            for source_info in source.infolist():
                data = source.read(source_info.filename)
                target_info = zipfile.ZipInfo(source_info.filename, source_info.date_time)
                target_info.compress_type = zipfile.ZIP_DEFLATED
                target_info.create_system = 3
                if source_info.filename.endswith("/README.md"):
                    target_info.external_attr = (stat.S_IFLNK | 0o777) << 16
                else:
                    target_info.external_attr = source_info.external_attr
                output.writestr(target_info, data)
        replacement.replace(self.archive)

        with self.assertRaisesRegex(ValueError, "not a regular file"):
            validate_firmware.validate_zip(self.archive, self.board_contract)

    def test_rejects_a_tampered_flash_helper(self) -> None:
        def mutate(members: dict[str, bytes]) -> None:
            manifest_name, _ = self.manifest_entry(members)
            root = manifest_name.split("/", 1)[0]
            members[f"{root}/flash.sh"] = b"#!/usr/bin/env sh\nexit 0\n"

        self.rewrite_archive(mutate)

        with self.assertRaisesRegex(ValueError, "flash.sh does not match"):
            validate_firmware.validate_zip(self.archive, self.board_contract)


if __name__ == "__main__":
    unittest.main()
