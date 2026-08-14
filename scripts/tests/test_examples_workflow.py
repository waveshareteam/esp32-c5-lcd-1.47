from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPO_ROOT / ".github/workflows/examples.yml"
WORKFLOWS_DIR = REPO_ROOT / ".github/workflows"


class ExamplesWorkflowTests(unittest.TestCase):
    def setUp(self) -> None:
        self.workflow = WORKFLOW.read_text(encoding="utf-8")
        self.all_workflows = "\n".join(
            path.read_text(encoding="utf-8") for path in sorted(WORKFLOWS_DIR.glob("*.yml"))
        )

    def test_c5_inventory_and_build_inputs_are_pinned(self) -> None:
        self.assertEqual(self.workflow.count("--expect-examples 8"), 4)
        self.assertIn("--libraries libraries", self.workflow)
        self.assertIn("BUILD_TARGET: ${{ matrix.target }}", self.workflow)
        self.assertIn("ESP32-C5-LCD-1.47-$EXAMPLE_SLUG", self.workflow)

    def test_container_git_command_trusts_the_workspace(self) -> None:
        self.assertIn(
            'git -c safe.directory="$GITHUB_WORKSPACE" show -s --format=%ct "$GITHUB_SHA"',
            self.workflow,
        )

    def test_release_requires_success_and_exact_commit_coverage(self) -> None:
        self.assertNotIn("if: always()", self.workflow)
        self.assertIn("startsWith(github.ref, 'refs/tags/v')", self.workflow)
        self.assertIn("needs.build-esp-idf.result == 'success'", self.workflow)
        self.assertIn("needs.build-arduino.result == 'success'", self.workflow)
        self.assertIn('git rev-parse "$RELEASE_TAG^{commit}"', self.workflow)
        self.assertIn('git rev-parse HEAD', self.workflow)
        self.assertIn('"$tagged_commit" != "$GITHUB_SHA"', self.workflow)
        self.assertIn("fetch-depth: 0", self.workflow)
        self.assertIn('git merge-base --is-ancestor "$GITHUB_SHA" "$default_branch_ref"', self.workflow)
        self.assertIn(
            'releases/validate_release_artifacts.py release-artifacts\n          --git-sha "$GITHUB_SHA"',
            self.workflow,
        )
        self.assertIn("python3 releases/validate_factory_firmware.py", self.workflow)

    def test_arduino_outputs_stay_in_the_ci_build_directory(self) -> None:
        self.assertIn('--output-dir "$build_dir"', self.workflow)
        self.assertNotIn("--export-binaries", self.workflow)

    def test_release_is_draft_until_assets_are_verified(self) -> None:
        release = self.workflow.split("  release:\n", 1)[1]
        create = release.index("Prepare draft release")
        upload = release.index("Upload release assets")
        verify = release.index("Verify draft asset inventory")
        publish = release.index("Publish verified release")
        self.assertLess(create, upload)
        self.assertLess(upload, verify)
        self.assertLess(verify, publish)
        self.assertIn("--draft", release)
        self.assertIn("Refusing to overwrite an existing published Release", release)
        self.assertIn('gh release delete "$GITHUB_REF_NAME"', release)
        self.assertNotIn("--cleanup-tag", release)
        self.assertIn('--title "$GITHUB_REF_NAME"', release)
        self.assertIn("--clobber", release)
        self.assertIn('asset.get("digest")', release)
        self.assertIn("if local_assets != remote_assets", release)
        self.assertIn("Release must remain a draft", release)
        self.assertIn('release.get("name") != os.environ["RELEASE_TAG"]', release)
        self.assertIn('release.get("prerelease") is not False', release)
        self.assertIn("Generated release notes must not be empty", release)
        self.assertIn("--draft=false", release)

    def test_permissions_and_stable_semver_gate_are_scoped(self) -> None:
        self.assertEqual(self.workflow.count("contents: write"), 1)
        self.assertIn("contents: read", self.workflow)
        release = self.workflow.split("  release:\n", 1)[1]
        self.assertIn("persist-credentials: false", release)
        self.assertIn(
            "^v(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$",
            self.workflow,
        )

    def test_third_party_actions_are_pinned_to_full_commit_shas(self) -> None:
        references = re.findall(r"uses:\s+[^@\s]+@([^\s#]+)", self.all_workflows)
        self.assertTrue(references)
        self.assertTrue(all(re.fullmatch(r"[0-9a-f]{40}", ref) for ref in references))

    def test_downloader_changes_trigger_validation(self) -> None:
        self.assertEqual(self.workflow.count('"releases/download_artifacts.py"'), 2)

    def test_c5_component_registry_dependencies_are_exactly_pinned(self) -> None:
        manifests = sorted((REPO_ROOT / "examples/esp-idf").glob("*/main/idf_component.yml"))
        bsp_manifests = [path for path in manifests if path.parts[-3] != "07_wifi_scan"]
        self.assertEqual(len(bsp_manifests), 7)
        expected = (
            '  waveshare/esp32_c5_lcd_1_47:\n    version: "1.0.0"\n    public: true',
            '  espressif/esp_lvgl_port:\n    version: "2.9.0"',
            '  espressif/led_strip:\n    version: "3.0.3"',
            '  lvgl/lvgl:\n    version: "9.5.0"',
        )
        for path in bsp_manifests:
            content = path.read_text(encoding="utf-8")
            with self.subTest(path=path):
                self.assertNotIn('version: "*"', content)
                for snippet in expected:
                    self.assertIn(snippet, content)


if __name__ == "__main__":
    unittest.main()
