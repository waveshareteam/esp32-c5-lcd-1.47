<!--
SPDX-FileCopyrightText: 2026 Waveshare Electronics
SPDX-License-Identifier: Apache-2.0
-->

# Backlight Fade

This sketch fades the LCD backlight with Arduino LEDC APIs and shows the current brightness with LVGL `9.5.0`.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c5:PartitionScheme=huge_app --libraries libraries \
  examples/arduino/03_backlight_fade
```
