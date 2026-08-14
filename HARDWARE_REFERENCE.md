# ESP32-C5-LCD-1.47 Hardware Reference

This reference is derived from the checked-in schematic and cross-checked
against the first-party Arduino examples and the board BSP used by ESP-IDF.
Consult `hardware/schematics/` before adapting these assignments to another
revision.

## Core Hardware

| Feature | Device / interface |
| --- | --- |
| MCU / Flash | ESP32-C5 with 4 MB embedded Flash. The official product page names `ESP32-C5FH4`, while the checked-in schematic names `ESP32-C5HF4`; the exact suffix is therefore an unresolved source-document conflict. |
| PSRAM | No onboard PSRAM is listed on the product page or shown in the schematic; CI explicitly disables it. |
| Display | 1.47-inch ST7789, 172 x 320 RGB565 over SPI |
| Storage | microSD over SPI |
| User indicator | One WS2812B RGB LED |
| Touch | Not present on this board |
| Other onboard peripherals | No audio codec, speaker/microphone, RTC, or I/O expander is shown in the checked-in schematic. |

## Display Signals

| Signal | Assignment |
| --- | --- |
| SPI clock | GPIO7 |
| SPI MOSI | GPIO6 |
| LCD chip select | GPIO23 |
| LCD data/command | GPIO24 |
| LCD reset | GPIO26 |
| Backlight | GPIO10 |
| Example SPI frequency | 40 MHz |
| Panel gap | X 34, Y 0 |

The Arduino examples use landscape rotation 3. Treat changes to rotation,
panel gap, color order, or SPI frequency as hardware-facing changes.

## Storage And RGB LED

| Signal | Assignment |
| --- | --- |
| microSD clock | GPIO7 |
| microSD MOSI | GPIO6 |
| microSD MISO | GPIO5 |
| microSD chip select | GPIO4 |
| WS2812B data | GPIO8 |

The LCD and microSD card share clock and MOSI. Both chip-select signals must be
managed correctly when switching devices on the bus.

## Mechanical References And Units

| Measurement | Drawing value |
| --- | --- |
| LCD active area | 17.39 x 32.35 mm |
| LCM outline | 19.39 +/- 0.2 x 36.28 +/- 0.2 mm |

The dimensioned PDF uses millimetre-scale values, and the STEP model declares
SI millimetres. However, the checked-in DXF header declares `$MEASUREMENT=0`
and `$INSUNITS=1` (inches) even though its geometry and annotations match the
millimetre dimensions above. Importing that DXF according to its header can
make the model 25.4 times too large. Override the DXF import unit to millimetres
and verify the two reference dimensions before manufacturing or enclosure work.

The STEP assembly also contains legacy `ESP32-S3-LCD-1_47` names on PMMA and
adhesive components. The top-level assembly and PCBA are named for
ESP32-C5; those internal component labels do not indicate an S3 electrical
design or add S3 peripherals to this board.

## Validation Scope

The assignments above agree with repository hardware references and source.
Automated CI only validates configuration, compilation, and firmware packaging.
LCD output, backlight range, microSD reliability, RGB LED behavior, radio
operation, and behavior across hardware revisions still require physical-board
testing. Cross-check the PDF, DXF, and STEP files under `hardware/` rather than
trusting the DXF unit header alone when using mechanical dimensions.
