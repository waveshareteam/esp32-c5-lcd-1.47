# Waveshare ESP32-C5-LCD-1.47

[English](README.md)

ESP32-C5-LCD-1.47 是一款基于 ESP32-C5 的紧凑型开发板，内置 4 MB Flash，
集成 1.47 英寸 172 x 320 ST7789 SPI LCD、microSD 卡槽和一颗 WS2812B RGB LED。
本板不带触摸控制器、音频编解码器、RTC、I/O 扩展器或板载 PSRAM。官方产品页
与仓库内原理图对芯片后缀的标注不一致（`ESP32-C5FH4` 与 `ESP32-C5HF4`），具体
差异见硬件参考，不应只依据其中一个来源推断变体。ESP32-C5 支持 Wi-Fi 6、
Bluetooth LE 和 IEEE 802.15.4；本仓库目前提供 Wi-Fi 扫描示例，但尚未提供
Bluetooth 或 802.15.4 示例。

- [购买链接](https://www.waveshare.net/shop/ESP32-C5-LCD-1.47.htm)
- [产品文档](https://docs.waveshare.net/ESP32-C5-LCD-1.47/)
- [硬件参考](HARDWARE_REFERENCE_CN.md)

![ESP32-C5-LCD-1.47](assets/Product-1.webp)

## 仓库内容

仓库包含 8 组一一对应的 ESP-IDF 和 Arduino 示例、Arduino 显示库、工厂恢复
固件、原理图及结构图纸。

| 示例 | 功能 |
| --- | --- |
| `01_lcd_panel_basic` | ST7789 初始化与直接绘图 |
| `02_lvgl_hello` | LVGL 显示集成 |
| `03_backlight_fade` | LCD 背光 PWM |
| `04_ws2812_rgb` | 板载 RGB LED |
| `05_sdcard_rw` | 通过共用 SPI 读写 microSD |
| `06_spiffs_rw` | SPIFFS 读写 |
| `07_wifi_scan` | Wi-Fi 网络扫描 |
| `08_board_showcase` | 综合板卡自检 |

首方工程位于 `examples/esp-idf/` 和 `examples/arduino/`，Arduino 库单独位于
仓库根目录的 `libraries/`。

## 构建配置

ESP-IDF 工程以 `esp32c5` 为目标，并使用各工程的 `sdkconfig.defaults`。
Arduino 编译必须加入 `--libraries libraries`；经过配置的完整 FQBN、Arduino
CLI、Arduino-ESP32 和 ESP-IDF 版本统一保存在 `config/ci.json`。

GitHub Actions 会自动发现示例并构建 24 个发布产物：8 个工程分别使用两个
ESP-IDF 版本构建，再加 8 个 Arduino 示例。只有稳定的 `vMAJOR.MINOR.PATCH`
标签、全部构建成功且所有 ZIP 均与标签提交匹配时，才会公开 GitHub Release。
详情见[持续集成](docs/ci.md)和[固件说明](docs/firmware.md)。

## 文档

- [硬件参考](HARDWARE_REFERENCE_CN.md)
- [仓库结构](docs/repository-structure.md)
- [组件说明](docs/components.md)
- [固件归档](releases/README.md)
- [贡献指南](CONTRIBUTING.md)
- [技术支持](SUPPORT.md)
- [安全策略](SECURITY.md)

## 许可证

本仓库中的项目自有文件遵循 Apache License 2.0。`libraries/` 下的第三方库
保留其原始许可证，请查看各库的元数据和许可文件。
