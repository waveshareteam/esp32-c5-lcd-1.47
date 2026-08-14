<!--
SPDX-FileCopyrightText: 2026 Waveshare Electronics
SPDX-License-Identifier: Apache-2.0
-->

# SPIFFS RW

This sketch mounts SPIFFS and runs a small file read/write check in flash.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c5:PartitionScheme=huge_app --libraries libraries \
  examples/arduino/06_spiffs_rw
```

The CI `huge_app` partition provides a 3 MiB application slot and an
`0xE0000`-byte (896 KiB) SPIFFS partition.
