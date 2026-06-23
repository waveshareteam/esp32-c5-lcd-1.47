<!--
SPDX-FileCopyrightText: 2026 Waveshare Electronics
SPDX-License-Identifier: Apache-2.0
-->

# Board Showcase

This integrated sketch shows board status on the LCD while checking the onboard WS2812B LED, SPIFFS, optional microSD card, and Wi-Fi scan.

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32c5 examples/arduino/08_board_showcase
```

Select a partition scheme that includes SPIFFS before flashing.
