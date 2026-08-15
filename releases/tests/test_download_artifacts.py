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
    def remote_command(self, *args: str) -> tuple[str, ...]:
        return ("git", "-C", str(MODULE.REPO_ROOT), *args)

    def test_default_repo_finds_github_when_origin_is_gitlab(self) -> None:
        commands = {
            self.remote_command("remote"): "origin\ngithub",
            self.remote_command(
                "remote", "get-url", "--all", "origin"
            ): "ssh://git@gitlab.example.com:222/owner/project.git",
            self.remote_command(
                "remote", "get-url", "--all", "github"
            ): "git@github.com:waveshareteam/esp32-c5-lcd-1.47.git",
        }

        with mock.patch.dict(MODULE.os.environ, {}, clear=True), mock.patch.object(
            MODULE, "run_text", side_effect=lambda command: commands.get(tuple(command))
        ) as run_text:
            self.assertEqual(
                MODULE.default_repo(), "waveshareteam/esp32-c5-lcd-1.47"
            )

        run_text.assert_has_calls(
            [
                mock.call(list(self.remote_command("remote"))),
                mock.call(
                    list(self.remote_command("remote", "get-url", "--all", "origin"))
                ),
                mock.call(
                    list(self.remote_command("remote", "get-url", "--all", "github"))
                ),
            ]
        )

    def test_default_repo_accepts_duplicate_urls_for_one_repository(self) -> None:
        commands = {
            self.remote_command("remote"): "origin\nupstream",
            self.remote_command(
                "remote", "get-url", "--all", "origin"
            ): "https://github.com/owner/repository.git",
            self.remote_command(
                "remote", "get-url", "--all", "upstream"
            ): "ssh://git@github.com:22/owner/repository.git",
        }

        with mock.patch.dict(MODULE.os.environ, {}, clear=True), mock.patch.object(
            MODULE, "run_text", side_effect=lambda command: commands.get(tuple(command))
        ):
            self.assertEqual(MODULE.default_repo(), "owner/repository")

    def test_default_repo_rejects_ambiguous_github_remotes_without_leaking_urls(
        self,
    ) -> None:
        secret = "not-a-real-token"
        commands = {
            self.remote_command("remote"): "origin\nupstream",
            self.remote_command(
                "remote", "get-url", "--all", "origin"
            ): f"https://user:{secret}@github.com/owner/first.git",
            self.remote_command(
                "remote", "get-url", "--all", "upstream"
            ): "git@github.com:owner/second.git",
        }

        with mock.patch.dict(MODULE.os.environ, {}, clear=True), mock.patch.object(
            MODULE, "run_text", side_effect=lambda command: commands.get(tuple(command))
        ), self.assertRaisesRegex(RuntimeError, "multiple GitHub repositories") as raised:
            MODULE.default_repo()

        self.assertNotIn(secret, str(raised.exception))
        self.assertNotIn("owner/first", str(raised.exception))
        self.assertNotIn("owner/second", str(raised.exception))

    def test_default_repo_uses_environment_before_git_remotes(self) -> None:
        with mock.patch.dict(
            MODULE.os.environ, {"GITHUB_REPOSITORY": "owner/repository"}, clear=True
        ), mock.patch.object(MODULE, "run_text") as run_text:
            self.assertEqual(MODULE.default_repo(), "owner/repository")

        run_text.assert_not_called()

    def test_default_repo_rejects_invalid_environment_without_using_remotes(self) -> None:
        with mock.patch.dict(
            MODULE.os.environ, {"GITHUB_REPOSITORY": "not a repository"}, clear=True
        ), mock.patch.object(MODULE, "run_text") as run_text, self.assertRaisesRegex(
            RuntimeError, "GITHUB_REPOSITORY is invalid"
        ):
            MODULE.default_repo()

        run_text.assert_not_called()

    def test_parse_github_repo_supports_common_remote_urls(self) -> None:
        values = (
            "owner/repository",
            "owner/repository.git",
            "git@github.com:owner/repository.git",
            "https://user:secret@github.com/owner/repository.git",
            "ssh://git@github.com:22/owner/repository.git",
        )

        for value in values:
            with self.subTest(value=value):
                self.assertEqual(MODULE.parse_github_repo(value), "owner/repository")

    def test_parse_github_repo_rejects_other_hosts_and_extra_paths(self) -> None:
        values = (
            "https://gitlab.com/owner/repository.git",
            "https://github.com/owner/repository/extra",
            "https://github.com.evil.example/owner/repository",
            "owner/repository/extra",
        )

        for value in values:
            with self.subTest(value=value):
                self.assertIsNone(MODULE.parse_github_repo(value))

    def test_token_skips_blank_environment_values(self) -> None:
        with mock.patch.dict(
            MODULE.os.environ,
            {"GH_TOKEN": "   ", "GITHUB_TOKEN": " github-token "},
            clear=True,
        ), mock.patch.object(MODULE, "run_text") as run_text:
            self.assertEqual(MODULE.token(), "github-token")

        run_text.assert_not_called()

    @mock.patch.object(MODULE, "default_repo")
    @mock.patch.object(MODULE, "token", return_value=None)
    def test_explicit_repo_skips_remote_discovery(
        self, token: mock.Mock, default_repo: mock.Mock
    ) -> None:
        with tempfile.TemporaryDirectory() as output, mock.patch.object(
            sys,
            "argv",
            [
                "download_artifacts.py",
                "--repo",
                "owner/repository",
                "--output-dir",
                output,
            ],
        ), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(MODULE.main(), 1)

        default_repo.assert_not_called()
        token.assert_not_called()

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
