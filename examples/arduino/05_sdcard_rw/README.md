<!--
SPDX-FileCopyrightText: 2026 Waveshare Electronics
SPDX-License-Identifier: Apache-2.0
-->

# SD Card RW

This sketch displays an LVGL status page on the LCD, mounts the microSD card over SDSPI, and runs a small file read/write check.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c5:PartitionScheme=huge_app --libraries libraries \
  examples/arduino/05_sdcard_rw
```
