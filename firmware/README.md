# Factory Firmware

[English](README.md) | [简体中文](README_ZH.md)

This directory stores the tested factory image for the ESP32-C5-LCD-1.47 development board.

| File | Purpose |
| --- | --- |
| [ESP32-C5-LCD-1.47_FactoryOnly_20260618.bin](ESP32-C5-LCD-1.47_FactoryOnly_20260618.bin) | Factory recovery firmware for the ESP32-C5-LCD-1.47 board, dated 20260618. Flash from `0x0`. |

Before flashing:

- Back up any device-specific data that must be preserved.
- Confirm the board model and hardware revision.
- Erase the target device before writing the merged image.
- Follow the flashing procedure in the product documentation.

SHA-256: `6B6EF3729DF2A442C94A897AC789FE9D3BCF0BE72378A1138E92B5C713267724`

This file is a single merged BIN for factory recovery.
