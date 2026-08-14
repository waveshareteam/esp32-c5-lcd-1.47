from __future__ import annotations

import contextlib
import importlib.util
import io
import sys
import tempfile
import unittest
import urllib.parse
from pathlib import Path
from unittest import mock


RELEASES_DIR = Path(__file__).resolve().parents[1]
SCRIPT = RELEASES_DIR / "download_artifacts.py"
SPEC = importlib.util.spec_from_file_location("download_artifacts", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class DownloadArtifactsTests(unittest.TestCase):
    @mock.patch.object(MODULE, "read_json")
    def test_latest_run_is_limited_to_successful_pushes_on_the_selected_branch(
        self, read_json: mock.Mock
    ) -> None:
        expected = {"id": 42, "html_url": "https://github.com/owner/repo/actions/runs/42"}
        read_json.return_value = {"workflow_runs": [expected]}

        self.assertEqual(MODULE.latest_run("owner/repo", "examples.yml", "main", None), expected)

        url = read_json.call_args.args[0]
        query = urllib.parse.parse_qs(urllib.parse.urlsplit(url).query)
        self.assertEqual(query["status"], ["success"])
        self.assertEqual(query["event"], ["push"])
        self.assertEqual(query["branch"], ["main"])

    @mock.patch.object(MODULE, "token", return_value=None)
    def test_latest_lookup_requires_an_explicit_branch(self, token: mock.Mock) -> None:
        with tempfile.TemporaryDirectory() as output, mock.patch.object(
            sys,
            "argv",
            ["download_artifacts.py", "--repo", "owner/repo", "--output-dir", output],
        ), contextlib.redirect_stderr(io.StringIO()) as stderr:
            result = MODULE.main()

        self.assertEqual(result, 1)
        self.assertIn("--branch is required", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
