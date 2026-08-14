<!--
SPDX-FileCopyrightText: 2026 Waveshare Electronics
SPDX-License-Identifier: Apache-2.0
-->

# Board Showcase

This integrated sketch shows board status on the LCD while checking the onboard WS2812B LED, SPIFFS, optional microSD card, and Wi-Fi scan.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c5:PartitionScheme=huge_app --libraries libraries \
  examples/arduino/08_board_showcase
```

The CI `huge_app` partition provides a 3 MiB application slot and an
`0xE0000`-byte (896 KiB) SPIFFS partition.
