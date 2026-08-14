# Continuous Integration

The `Build Examples and Firmware` workflow discovers first-party projects with
`scripts/discover_examples.py`. Toolchain versions and board settings come from
`config/ci.json`.

## Supported Matrix

| Surface | Pinned version | Target | Builds |
| --- | --- | --- | --- |
| ESP-IDF | `v5.5.5` | `esp32c5` | 8 |
| ESP-IDF | `v6.0.2` | `esp32c5` | 8 |
| Arduino CLI / Arduino-ESP32 | `1.5.1` / `3.3.11` | ESP32-C5, 4 MB Flash | 8 |

A complete release therefore requires exactly 24 firmware ZIP archives. Update
`config/ci.json`, discovery expectations, and release-validator tests together
when changing this matrix.

## Triggers And Selection

Pull requests and pushes run the workflow when examples, vendored Arduino
libraries, shared configuration, discovery code, or release tooling changes.
Manual dispatch accepts `all`, an example name such as `03_backlight_fade`, or a
repository-relative example path. An unknown selector fails validation.

Tags beginning with `v` also start the workflow, but the validation job accepts
only stable `vMAJOR.MINOR.PATCH` tags. Pre-release suffixes and malformed tags do
not reach the release job. The tag target, checked-out `HEAD`, and GitHub event
commit must resolve to the same commit, and that commit must be reachable from
the repository's default branch, before any release build proceeds.

## Arduino Board Options

Arduino uses the ESP32-C5 board with 4 MB Flash, no PSRAM, 80 MHz QIO flash,
921600 upload speed, and the `huge_app` partition scheme. Its generated table
provides a 3 MiB application slot and a `0xE0000`-byte (896 KiB) SPIFFS
partition; the Arduino menu rounds this to "1MB SPIFFS." The larger application
slot is required by the integrated board showcase. The complete FQBN is stored
in `config/ci.json`. Libraries under the
repository-root `libraries/` directory are passed explicitly to Arduino CLI;
library-owned examples are not product CI inputs. Build outputs stay under the
CI build directory and are not exported back into the source sketch folders.

## Release Safety

Every build creates a ZIP containing a manifest, checksums, original binary
segments, a combined flash image, and Windows/POSIX flash helpers. Packaging
and archive validation independently require the combined image's Espressif
chip ID to identify an ESP32-C5 rather than trusting manifest text alone. The
release gate also validates the checked-in factory image's digest, size,
bootloader header offset, and ESP32-C5 image chip ID, because GitHub's automatic
source archives include that file even though it is not uploaded as a generated
firmware asset. The release job runs only after both build matrices succeed.
It then verifies the exact `(project, framework, version, target, git_sha)`
matrix, creates a draft GitHub Release, uploads all assets, compares the remote
and local attachment names and SHA-256 digests, and only then makes the release
public. A rerun replaces an existing draft for the same tag before regenerating
its title and notes; it refuses to alter an already published Release.

Workflow permissions default to `contents: read`; only the release job receives
`contents: write`. It uses the scoped `github.token`, so no repository-specific
release token is required. Third-party Actions are pinned to full commit hashes
and remain maintainable through the monthly Dependabot configuration.

## GitHub Repository Prerequisites

The workflow becomes active only after these files are pushed to a GitHub
repository. A non-GitHub `origin` cannot run GitHub Actions or publish GitHub
Releases. GitHub Actions must be enabled, and organization/repository policy
must allow the release job's requested `contents: write` permission. The
workflow uses only the built-in `github.token`; do not add a personal access
token for normal releases.

The workflow never creates a tag. Push a reviewed tag matching
`vMAJOR.MINOR.PATCH` to the GitHub repository to start a release build. A GitHub
ruleset protecting `v*` tags is recommended so only authorized maintainers can
trigger publication. Both lightweight and annotated tags resolve correctly;
signature requirements, if desired, should be enforced by the repository
ruleset because the reference repository's existing tags are annotated but
unsigned.

Compilation and package validation do not replace physical-board testing. LCD
color order and orientation, backlight behavior, microSD access, radio behavior,
and GPIO levels still require hardware verification.
