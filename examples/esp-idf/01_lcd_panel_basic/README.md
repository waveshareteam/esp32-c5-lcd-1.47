<!--
SPDX-FileCopyrightText: 2026 Waveshare Electronics
SPDX-License-Identifier: Apache-2.0
-->

# LCD Panel Basic

This example initializes the ST7789 LCD through the BSP without starting LVGL. It draws color bars and a simple moving block directly with `esp_lcd_panel_draw_bitmap()`.

```bash
idf.py -C examples/esp-idf/01_lcd_panel_basic set-target esp32c5
idf.py -C examples/esp-idf/01_lcd_panel_basic build flash monitor
```
