<!--
SPDX-FileCopyrightText: 2026 Waveshare Electronics
SPDX-License-Identifier: Apache-2.0
-->

# SD Card Read/Write

This example starts the LCD, mounts the microSD card through SDSPI, writes a file, reads it back, and shows every step on the screen.

```bash
idf.py -C examples/esp-idf/05_sdcard_rw set-target esp32c5
idf.py -C examples/esp-idf/05_sdcard_rw build flash monitor
```
