<!--
SPDX-FileCopyrightText: 2026 Waveshare Electronics
SPDX-License-Identifier: Apache-2.0
-->

# SPIFFS Read/Write

This example mounts a SPIFFS partition named `storage`, writes a file, reads it back, and unmounts the filesystem.

```powershell
idf.py set-target esp32c5
idf.py build flash monitor
```
