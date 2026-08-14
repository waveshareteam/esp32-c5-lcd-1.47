<!--
SPDX-FileCopyrightText: 2026 Waveshare Electronics
SPDX-License-Identifier: Apache-2.0
-->

# WS2812 RGB

This sketch cycles the onboard WS2812B LED through RGB colors with the ESP32 Arduino core `rgbLedWrite()` helper.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c5:PartitionScheme=huge_app --libraries libraries \
  examples/arduino/04_ws2812_rgb
```
