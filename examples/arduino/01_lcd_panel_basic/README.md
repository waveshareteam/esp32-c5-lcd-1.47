<!--
SPDX-FileCopyrightText: 2026 Waveshare Electronics
SPDX-License-Identifier: Apache-2.0
-->

# LCD Panel Basic

This sketch initializes the ST7789 LCD with `GFX_Library_for_Arduino` without starting LVGL. It draws color bars once, then animates a moving block with partial updates for a smoother frame rate.

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32c5 examples/arduino/01_lcd_panel_basic
```
