from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock


RELEASES_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RELEASES_DIR))
SCRIPT = RELEASES_DIR / "validate_release_artifacts.py"
SPEC = importlib.util.spec_from_file_location("validate_release_artifacts", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ValidateReleaseArtifactsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.repo = Path(self.temporary.name)
        self.artifacts = self.repo / "artifacts"
        self.artifacts.mkdir()
        self.git_sha = "a" * 40
        idf = self.repo / "examples/esp-idf/01_idf"
        idf.mkdir(parents=True)
        (idf / "CMakeLists.txt").touch()
        arduino = self.repo / "examples/arduino/01_arduino"
        arduino.mkdir(parents=True)
        (arduino / "01_arduino.ino").touch()
        (self.repo / "config").mkdir()
        (self.repo / "config/ci.json").write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "board": {
                        "name": "ESP32-C5-LCD-1.47",
                        "module": "ESP32-C5",
                        "target": "esp32c5",
                        "bootloader_offset": "0x2000",
                    },
                    "esp_idf": {"versions": ["v5.5.5", "v6.0.2"]},
                    "arduino": {"core": "3.3.11"},
                }
            ),
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def create_archive(
        self,
        name: str,
        framework: str,
        project: str,
        version: str,
        target: str = "esp32c5",
        git_sha: str | None = None,
    ) -> Path:
        path = self.artifacts / name
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr(
                f"{name.removesuffix('.zip')}/manifest.json",
                json.dumps(
                    {
                        "framework": framework,
                        "framework_version": version,
                        "project_path": project,
                        "target": target,
                        "git_sha": git_sha or self.git_sha,
                    }
                ),
            )
        return path

    def create_complete_matrix(self) -> None:
        self.create_archive(
            "idf-v5.zip", "esp-idf", "examples/esp-idf/01_idf", "v5.5.5"
        )
        self.create_archive(
            "idf-v6.zip", "esp-idf", "examples/esp-idf/01_idf", "v6.0.2"
        )
        self.create_archive(
            "arduino.zip", "arduino", "examples/arduino/01_arduino", "3.3.11"
        )

    @mock.patch.object(MODULE, "validate_zip")
    def test_accepts_exact_configured_matrix(self, validate_zip: mock.Mock) -> None:
        self.create_complete_matrix()

        self.assertEqual(
            MODULE.validate_release(self.repo, self.artifacts, self.git_sha), (3, 3)
        )
        self.assertEqual(validate_zip.call_count, 3)

    @mock.patch.object(MODULE, "validate_zip")
    def test_rejects_a_missing_version_variant(self, validate_zip: mock.Mock) -> None:
        self.create_archive(
            "idf-v5.zip", "esp-idf", "examples/esp-idf/01_idf", "v5.5.5"
        )
        self.create_archive(
            "arduino.zip", "arduino", "examples/arduino/01_arduino", "3.3.11"
        )

        with self.assertRaisesRegex(ValueError, r"v6\.0\.2"):
            MODULE.validate_release(self.repo, self.artifacts, self.git_sha)

    @mock.patch.object(MODULE, "validate_zip")
    def test_rejects_duplicate_variant(self, validate_zip: mock.Mock) -> None:
        self.create_complete_matrix()
        self.create_archive(
            "idf-v5-copy.zip", "esp-idf", "examples/esp-idf/01_idf", "v5.5.5"
        )

        with self.assertRaisesRegex(ValueError, "duplicate build variant"):
            MODULE.validate_release(self.repo, self.artifacts, self.git_sha)

    @mock.patch.object(MODULE, "validate_zip")
    def test_rejects_extra_or_wrong_variant(self, validate_zip: mock.Mock) -> None:
        self.create_complete_matrix()
        self.create_archive(
            "idf-extra.zip", "esp-idf", "examples/esp-idf/01_idf", "v5.4.0"
        )

        with self.assertRaisesRegex(ValueError, "unexpected build variant"):
            MODULE.validate_release(self.repo, self.artifacts, self.git_sha)

    @mock.patch.object(MODULE, "validate_zip")
    def test_rejects_wrong_source_commit(self, validate_zip: mock.Mock) -> None:
        self.create_complete_matrix()
        archive = self.artifacts / "arduino.zip"
        archive.unlink()
        self.create_archive(
            "arduino.zip",
            "arduino",
            "examples/arduino/01_arduino",
            "3.3.11",
            git_sha="b" * 40,
        )

        with self.assertRaisesRegex(ValueError, "does not match release commit"):
            MODULE.validate_release(self.repo, self.artifacts, self.git_sha)


if __name__ == "__main__":
    unittest.main()
