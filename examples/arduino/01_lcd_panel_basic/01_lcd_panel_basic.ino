/*
 * SPDX-FileCopyrightText: 2026 Waveshare Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Arduino.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#else
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

#include <Arduino_GFX_Library.h>

#define LCD_H_RES              (172)
#define LCD_V_RES              (320)
#define LCD_SPI_FREQ_HZ        (40 * 1000 * 1000)
#define LCD_SPI_SCLK           (7)
#define LCD_SPI_MOSI           (6)
#define LCD_SPI_MISO           (GFX_NOT_DEFINED)
#define LCD_SPI_CS             (23)
#define LCD_SPI_DC             (24)
#define LCD_SPI_RST            (26)
#define LCD_BACKLIGHT          (10)
#define LCD_X_GAP              (34)
#define LCD_Y_GAP              (0)
#define LCD_ROTATION_LANDSCAPE (3)

#define BACKLIGHT_LEDC_CH      (0)
#define BACKLIGHT_LEDC_FREQ_HZ (5000)
#define BACKLIGHT_LEDC_BITS    (8)

#define BLOCK_H                (28)
#define BLOCK_STEP             (1)
#define FRAME_INTERVAL_MS      (45)
#define EDGE_HOLD_FRAMES       (10)

static Arduino_DataBus *s_bus = new Arduino_ESP32SPI(
    LCD_SPI_DC, LCD_SPI_CS, LCD_SPI_SCLK, LCD_SPI_MOSI, LCD_SPI_MISO);

static Arduino_GFX *s_gfx = new Arduino_ST7789(
    s_bus, LCD_SPI_RST, LCD_ROTATION_LANDSCAPE, true,
    LCD_H_RES, LCD_V_RES,
    LCD_X_GAP, LCD_Y_GAP, LCD_X_GAP, LCD_Y_GAP);

static const uint16_t s_bar_colors[] = {
    0xf800,
    0x07e0,
    0x001f,
    0xffff,
    0x0000,
};

static void set_backlight(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    const uint32_t duty = (percent * ((1 << BACKLIGHT_LEDC_BITS) - 1)) / 100;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(LCD_BACKLIGHT, duty);
#else
    ledcWrite(BACKLIGHT_LEDC_CH, duty);
#endif
}

static void init_backlight(uint8_t percent)
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(LCD_BACKLIGHT, BACKLIGHT_LEDC_FREQ_HZ, BACKLIGHT_LEDC_BITS);
#else
    ledcSetup(BACKLIGHT_LEDC_CH, BACKLIGHT_LEDC_FREQ_HZ, BACKLIGHT_LEDC_BITS);
    ledcAttachPin(LCD_BACKLIGHT, BACKLIGHT_LEDC_CH);
#endif
    set_backlight(percent);
}

static void draw_color_bars(void)
{
    const int bar_count = sizeof(s_bar_colors) / sizeof(s_bar_colors[0]);
    const int bar_width = s_gfx->width() / bar_count;

    for (int i = 0; i < bar_count; i++) {
        const int x = i * bar_width;
        const int width = (i == bar_count - 1) ? (s_gfx->width() - x) : bar_width;
        s_gfx->fillRect(x, 0, width, s_gfx->height(), s_bar_colors[i]);
    }
}

static void restore_background_rect(int x, int y, int width, int height)
{
    const int bar_count = sizeof(s_bar_colors) / sizeof(s_bar_colors[0]);
    const int bar_width = s_gfx->width() / bar_count;

    for (int i = 0; i < bar_count; i++) {
        const int bar_x = i * bar_width;
        const int bar_w = (i == bar_count - 1) ? (s_gfx->width() - bar_x) : bar_width;
        const int x1 = x > bar_x ? x : bar_x;
        const int x2 = (x + width) < (bar_x + bar_w) ? (x + width) : (bar_x + bar_w);
        if (x2 > x1) {
            s_gfx->fillRect(x1, y, x2 - x1, height, s_bar_colors[i]);
        }
    }
}

static void draw_motion_frame(int previous_y, int y)
{
    const int block_x = 32;
    const int block_w = s_gfx->width() - 64;

    s_gfx->fillRect(block_x, y, block_w, BLOCK_H, 0x001f);
    if (previous_y >= 0) {
        if (y > previous_y) {
            restore_background_rect(block_x, previous_y, block_w, y - previous_y);
        } else if (y < previous_y) {
            restore_background_rect(block_x, y + BLOCK_H, block_w, previous_y - y);
        }
    }
}

void setup(void)
{
    Serial.begin(115200);
    delay(100);
    Serial.println("ESP32-C5-LCD-1.47 LCD panel basic");

    if (!s_gfx->begin(LCD_SPI_FREQ_HZ)) {
        Serial.println("LCD init failed");
        while (true) {
            delay(1000);
        }
    }

    s_gfx->invertDisplay(false);
    s_gfx->displayOn();
    init_backlight(80);
    draw_color_bars();
}

void loop(void)
{
    static int y = 0;
    static int direction = 1;
    static int edge_hold_frames = 0;
    static bool first_frame = true;
    static uint32_t last_frame_ms = 0;

    const uint32_t now = millis();
    if (now - last_frame_ms < FRAME_INTERVAL_MS) {
        delay(1);
        return;
    }
    last_frame_ms = now;

    if (first_frame) {
        draw_motion_frame(-1, y);
        first_frame = false;
        return;
    }

    if (edge_hold_frames > 0) {
        edge_hold_frames--;
        return;
    }

    int next_y = y + direction * BLOCK_STEP;
    if (next_y <= 0) {
        next_y = 0;
        direction = 1;
        edge_hold_frames = EDGE_HOLD_FRAMES;
    } else if (next_y >= s_gfx->height() - BLOCK_H) {
        next_y = s_gfx->height() - BLOCK_H;
        direction = -1;
        edge_hold_frames = EDGE_HOLD_FRAMES;
    }

    draw_motion_frame(y, next_y);
    y = next_y;
}
