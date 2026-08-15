# Firmware Archives

The `Build Examples and Firmware` workflow packages each configured ESP-IDF and
Arduino build as a directly flashable ZIP. The checked-in factory image is
validated separately and is never uploaded as a generated source-build
artifact.

Every archive contains:

```text
README.md
SHA256SUMS
flash.sh
flash.bat
flash_args.txt
manifest.json
bin/*.bin
```

The helpers write one combined image at flash offset `0x0`. For ESP32-C5, the
combined image contains erased-flash padding before the bootloader header at
`0x2000`. Install esptool with an available Python 3 launcher:

```bash
python -m pip install esptool
python3 -m pip install esptool
py -3 -m pip install esptool
./flash.sh /dev/ttyUSB0
```

Only one install command is needed. `flash.sh` detects `python` or `python3`;
`flash.bat` detects `python` or `py -3`. On Windows, run `flash.bat COMx`.

## Package An ESP-IDF Build

```bash
idf.py -C examples/esp-idf/01_lcd_panel_basic \
  -B build/01_lcd_panel_basic set-target esp32c5 build
python3 releases/package_firmware.py \
  --framework esp-idf \
  --project examples/esp-idf/01_lcd_panel_basic \
  --build-dir build/01_lcd_panel_basic \
  --framework-version v5.5.5 \
  --target esp32c5 \
  --git-sha "$(git rev-parse HEAD)"
```

The packager reads ESP-IDF's generated `flasher_args.json`, preserves every
source segment, validates the configured target and bootloader offset, and
creates the combined image.

## Package An Arduino Build

Compile with the exact FQBN from `config/ci.json`, pass
`--libraries libraries`, and select a stable directory with `--build-path` so
the generated `boot_app0.bin` remains available. Then run the packager with
`--framework arduino`. It prefers a matching bootloader, partition table, boot
application, and application component set and constructs a compact complete
image that ends at the application segment. Arduino's merged image is retained
only as a compatibility fallback when those components are not available.
The configured ESP32-C5 FQBN enables USB CDC on boot, and CI verifies the
expanded Arduino Core property and compiler definition before building release
firmware.

Set `SOURCE_DATE_EPOCH` to the source commit's Unix timestamp when reproducible
ZIP bytes are required. The package name includes the first seven characters of
the supplied Git SHA.

## Release Gate

Stable `vMAJOR.MINOR.PATCH` tags build 24 configured variants. Before publishing,
`validate_release_artifacts.py` rejects missing, duplicate, or extra variants,
wrong targets, manifests from a different commit, unexpected archive members,
and flash helpers that differ from the manifest command. GitHub Actions creates
a fresh draft, uploads the verified ZIP set, compares every remote attachment
name and SHA-256 digest with the local files, verifies stable-release metadata,
and only then publishes the Release. A rerun replaces an existing draft for the
same tag but refuses to alter an already published Release.

## Factory Firmware Boundary

The checked-in factory image, target, offsets, size, and SHA-256 digest are
pinned in `config/factory-firmware.json`. Verify it with:

```bash
python3 releases/validate_factory_firmware.py
```

The factory workflow uploads nothing. This repository contains no separate
factory-demo microSD resources.

## Download CI Artifacts

When the repository has a GitHub remote, the downloader infers its
owner/repository even if `origin` points to GitLab:

```bash
python3 releases/download_artifacts.py \
  --branch main \
  --clean
```

Use `--repo OWNER/REPOSITORY` when running outside this checkout, when no
GitHub remote is configured, or when remotes point to different GitHub
repositories. `GITHUB_REPOSITORY` can supply the same default.
Choose the trusted branch explicitly; implicit latest-run lookup considers only
successful `push` runs. Use `--run-id` for a reviewed manual run, or use
`--artifact` and `--pattern` to select artifacts.
Authentication is read from `GH_TOKEN`, `GITHUB_TOKEN`, or `gh auth token` and
is never written to repository files.
