# Firmware And Factory Recovery

This repository contains source-built example packages and one checked-in
factory recovery image. They have different provenance and must not be mixed.

## Source-Built Example Packages

CI and `releases/package_firmware.py` create one flashable ZIP per configured
build. Each archive contains:

```text
README.md
SHA256SUMS
flash.sh
flash.bat
flash_args.txt
manifest.json
bin/<combined image>
bin/<original binary segments>
```

The combined image is flashed at offset `0x0`. On ESP32-C5 it normally contains
erased-flash padding before the first Espressif image header at `0x2000`; that
header offset is recorded and validated in `manifest.json`.

After extracting an archive, install esptool and use the included helper:

```text
python3 -m pip install esptool
./flash.sh /dev/ttyACM0
py -3 -m pip install esptool
flash.bat COMx
```

Use the first two commands on Linux or macOS and the last two on Windows.

The manifest records the board, target, framework version, source project,
source commit, segment offsets, hashes, and equivalent esptool command.

## Factory Recovery Firmware

`firmware/ESP32-C5-LCD-1.47-Test.bin` is the complete factory test/recovery
image. Flash it at offset `0x0`; its first image header is at `0x2000`. The
expected size and SHA-256 digest are pinned in
`config/factory-firmware.json` and verified against the board identity in
`config/ci.json`. Stable-tag release runs perform the same validation before
publishing, including the ESP32-C5 chip ID in the Espressif image header.

The repository does not contain the factory firmware source or a reproducible
factory build recipe. It also does not contain factory-demo microSD media. The
microSD examples create their own test files.

Run this before distributing or flashing the checked-in image:

```bash
python3 releases/validate_factory_firmware.py
```

Generated source-build archives and downloaded workflow artifacts belong in
ignored output directories and must not replace the checked-in factory image.
