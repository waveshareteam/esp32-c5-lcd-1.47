# Components

## ESP-IDF

Seven display and peripheral examples declare
`waveshare/esp32_c5_lcd_1_47` through their local `idf_component.yml`. The
Wi-Fi scan example uses ESP-IDF directly and does not require the board BSP.
Every project declares ESP-IDF 5.3 or newer and carries an `esp32c5`, 4 MB flash
`sdkconfig.defaults` baseline.

The seven BSP projects explicitly pin the component-registry versions resolved
and tested by both CI toolchains: board BSP `1.0.0`, ESP LVGL port `2.9.0`, LED
strip `3.0.3`, and LVGL `9.5.0`. Keep those four pins synchronized across the
projects and test upgrades on both configured ESP-IDF versions.

Generated `dependencies.lock` files remain ignored because ESP-IDF 5.5 and 6.0
write incompatible lock schema versions for the same project. Exact registry
pins keep dependency selection stable; they do not by themselves guarantee
byte-for-byte identical compiler output or immutable container images.

## Arduino

Arduino sketches use repository-pinned copies of:

- GFX Library for Arduino `1.6.5`;
- LVGL `9.5.0`.

They live under `libraries/` and must be supplied with `--libraries libraries`
when compiling. These trees include upstream examples and third-party license
files; only the eight direct sketches under `examples/arduino/` belong to the
product build matrix.

Keep component upgrades separate from board-behavior changes. Display driver,
LVGL, storage, and BSP updates must be compiled across the configured matrix and
then checked on the physical ESP32-C5-LCD-1.47.
