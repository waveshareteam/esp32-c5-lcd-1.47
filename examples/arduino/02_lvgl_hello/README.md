<!--
SPDX-FileCopyrightText: 2026 Waveshare Electronics
SPDX-License-Identifier: Apache-2.0
-->

# LVGL Hello

This sketch starts LVGL `9.5.0` and draws a compact dashboard on the LCD using `GFX_Library_for_Arduino` as the display driver.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c5:PartitionScheme=huge_app --libraries libraries \
  examples/arduino/02_lvgl_hello
```
