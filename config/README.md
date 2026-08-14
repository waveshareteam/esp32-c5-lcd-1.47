# Shared Configuration

`ci.json` is the single source of truth for the CI framework versions, Arduino
CLI version, ESP32-C5 board options, and the `0x2000` bootloader offset.
Discovery, packaging, and archive validation read this file before producing or
accepting GitHub Actions artifacts.

`factory-firmware.json` identifies the complete factory flash image at offset
`0x0`, records the embedded image-header offset, and pins its size and SHA-256
digest. Factory integrity CI verifies the checked-in binary but does not publish
a generated archive. The microSD examples create their own test files and do not
depend on repository-managed runtime media.

Example-local `sdkconfig.defaults` files remain authoritative for ESP-IDF
project settings. Add a shared overlay here only when multiple projects consume
the same file.
