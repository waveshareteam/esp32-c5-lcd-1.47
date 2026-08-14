from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


RELEASES_DIR = Path(__file__).resolve().parents[1]
SCRIPT = RELEASES_DIR / "validate_github_release.py"
SPEC = importlib.util.spec_from_file_location("validate_github_release", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ValidateGithubReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.artifacts = self.root / "artifacts"
        self.artifacts.mkdir()
        (self.artifacts / "alpha.zip").write_bytes(b"alpha")
        (self.artifacts / "beta.zip").write_bytes(b"beta")
        self.tag = "v1.0.0"
        self.release = {
            "draft": True,
            "tag_name": self.tag,
            "name": self.tag,
            "prerelease": False,
            "body": "Verified firmware archives.",
            "assets": [self.asset(path) for path in sorted(self.artifacts.glob("*.zip"))],
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def asset(path: Path) -> dict[str, object]:
        return {
            "name": path.name,
            "size": path.stat().st_size,
            "digest": MODULE.sha256(path),
        }

    def test_accepts_complete_draft_release(self) -> None:
        self.assertEqual(MODULE.validate_release(self.release, self.artifacts, self.tag), 2)

    def test_reports_pending_digest(self) -> None:
        self.release["assets"][0]["digest"] = None
        with self.assertRaisesRegex(MODULE.PendingReleaseAssets, "alpha.zip"):
            MODULE.validate_release(self.release, self.artifacts, self.tag)

    def test_reports_missing_asset_as_pending(self) -> None:
        self.release["assets"].pop()
        with self.assertRaisesRegex(MODULE.PendingReleaseAssets, "not listed yet"):
            MODULE.validate_release(self.release, self.artifacts, self.tag)

    def test_accepts_downloaded_assets_when_remote_listing_is_pending(self) -> None:
        self.release["assets"].pop()
        downloaded = self.root / "downloaded"
        downloaded.mkdir()
        for source in self.artifacts.glob("*.zip"):
            (downloaded / source.name).write_bytes(source.read_bytes())
        self.assertEqual(
            MODULE.validate_release(self.release, self.artifacts, self.tag, downloaded), 2
        )

    def test_accepts_downloaded_assets_when_digest_is_pending(self) -> None:
        self.release["assets"][0]["digest"] = None
        downloaded = self.root / "downloaded"
        downloaded.mkdir()
        for source in self.artifacts.glob("*.zip"):
            (downloaded / source.name).write_bytes(source.read_bytes())
        self.assertEqual(
            MODULE.validate_release(self.release, self.artifacts, self.tag, downloaded), 2
        )

    def test_rejects_tampered_downloaded_asset(self) -> None:
        self.release["assets"][0]["digest"] = None
        downloaded = self.root / "downloaded"
        downloaded.mkdir()
        for source in self.artifacts.glob("*.zip"):
            (downloaded / source.name).write_bytes(source.read_bytes())
        (downloaded / "alpha.zip").write_bytes(b"wrong")
        with self.assertRaisesRegex(ValueError, "downloaded.*sizes or digests differ"):
            MODULE.validate_release(self.release, self.artifacts, self.tag, downloaded)

    def test_rejects_known_wrong_digest_while_another_is_pending(self) -> None:
        self.release["assets"][0]["digest"] = None
        self.release["assets"][1]["digest"] = "sha256:" + "0" * 64
        with self.assertRaisesRegex(ValueError, "digests differ"):
            MODULE.validate_release(self.release, self.artifacts, self.tag)

    def test_rejects_wrong_digest(self) -> None:
        self.release["assets"][0]["digest"] = "sha256:" + "0" * 64
        with self.assertRaisesRegex(ValueError, "digests differ"):
            MODULE.validate_release(self.release, self.artifacts, self.tag)

    def test_rejects_wrong_size(self) -> None:
        self.release["assets"][0]["size"] += 1
        with self.assertRaisesRegex(ValueError, "sizes differ"):
            MODULE.validate_release(self.release, self.artifacts, self.tag)

    def test_rejects_extra_asset(self) -> None:
        self.release["assets"].pop()
        self.release["assets"].append(
            {"name": "extra.zip", "size": 1, "digest": "sha256:" + "0" * 64}
        )
        with self.assertRaisesRegex(ValueError, "unexpected assets"):
            MODULE.validate_release(self.release, self.artifacts, self.tag)

    def test_rejects_duplicate_asset(self) -> None:
        self.release["assets"].append(dict(self.release["assets"][0]))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            MODULE.validate_release(self.release, self.artifacts, self.tag)

    def test_rejects_invalid_release_metadata(self) -> None:
        cases = {
            "draft": False,
            "tag_name": "v2.0.0",
            "name": "wrong title",
            "prerelease": True,
            "body": "",
        }
        for field, value in cases.items():
            with self.subTest(field=field):
                release = dict(self.release)
                release[field] = value
                with self.assertRaises(ValueError):
                    MODULE.validate_release(release, self.artifacts, self.tag)

    def test_cli_uses_temporary_failure_exit_code(self) -> None:
        self.release["assets"][0]["digest"] = None
        release_json = self.root / "release.json"
        release_json.write_text(json.dumps(self.release), encoding="utf-8")
        argv = [
            str(SCRIPT),
            str(release_json),
            str(self.artifacts),
            "--tag",
            self.tag,
        ]
        original = sys.argv
        try:
            sys.argv = argv
            self.assertEqual(MODULE.main(), MODULE.PENDING_EXIT_CODE)
        finally:
            sys.argv = original


if __name__ == "__main__":
    unittest.main()
