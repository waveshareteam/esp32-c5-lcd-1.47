# Factory Firmware

`ESP32-C5-LCD-1.47-Test.bin` is a complete factory test and recovery image for
ESP32-C5-LCD-1.47. Flash the file at offset `0x0`; the first Espressif image
header is at offset `0x2000`.

Before distribution or recovery use, verify the checked-in file against
`config/factory-firmware.json`:

```bash
python3 releases/validate_factory_firmware.py
```

The current pinned image is 1,388,832 bytes with SHA-256
`6b6ef3729df2a442c94a897ac789fe9d3bcf0be72378a1138e92b5c713267724`.
Factory firmware source and a reproducible build recipe are not included in
this repository. No separate factory-demo microSD runtime resources are stored
here; the maintained microSD examples create their own test files.
