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

#include "esp_heap_caps.h"

#include <Arduino_GFX_Library.h>
#include <lvgl.h>

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR != 5 || LVGL_VERSION_PATCH != 0
#error "This example expects LVGL 9.5.0, the same version used by the ESP-IDF examples."
#endif

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

#define LVGL_BUFFER_HEIGHT     (40)
#define BACKLIGHT_LEDC_CH      (0)
#define BACKLIGHT_LEDC_FREQ_HZ (5000)
#define BACKLIGHT_LEDC_BITS    (8)

static Arduino_DataBus *s_bus = new Arduino_ESP32SPI(
    LCD_SPI_DC, LCD_SPI_CS, LCD_SPI_SCLK, LCD_SPI_MOSI, LCD_SPI_MISO);

static Arduino_GFX *s_gfx = new Arduino_ST7789(
    s_bus, LCD_SPI_RST, LCD_ROTATION_LANDSCAPE, true,
    LCD_H_RES, LCD_V_RES,
    LCD_X_GAP, LCD_Y_GAP, LCD_X_GAP, LCD_Y_GAP);

static uint8_t *s_lvgl_buf;
static uint32_t s_last_tick_ms;
static uint32_t s_last_fade_ms;
static lv_obj_t *s_value_label;
static lv_obj_t *s_bar;
static int s_brightness;
static int s_step = 5;

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

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int32_t width = area->x2 - area->x1 + 1;
    const int32_t height = area->y2 - area->y1 + 1;

    s_gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, width, height);
    lv_display_flush_ready(disp);
}

static void init_display(void)
{
    if (!s_gfx->begin(LCD_SPI_FREQ_HZ)) {
        Serial.println("LCD init failed");
        while (true) {
            delay(1000);
        }
    }

    s_gfx->invertDisplay(false);
    s_gfx->fillScreen(0x0000);
    s_gfx->displayOn();
    init_backlight(0);
}

static void init_lvgl(void)
{
    lv_init();

    const uint32_t buf_size = (uint32_t)s_gfx->width() * LVGL_BUFFER_HEIGHT * sizeof(lv_color16_t);
    s_lvgl_buf = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_lvgl_buf) {
        s_lvgl_buf = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_8BIT);
    }
    if (!s_lvgl_buf) {
        Serial.println("No memory for LVGL draw buffer");
        while (true) {
            delay(1000);
        }
    }

    lv_display_t *disp = lv_display_create(s_gfx->width(), s_gfx->height());
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_buffers(disp, s_lvgl_buf, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    s_last_tick_ms = millis();
}

static void create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Backlight");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    s_value_label = lv_label_create(scr);
    lv_label_set_text(s_value_label, "0%");
    lv_obj_set_style_text_color(s_value_label, lv_color_hex(0xfacc15), 0);
    lv_obj_set_style_text_font(s_value_label, &lv_font_montserrat_20, 0);
    lv_obj_align(s_value_label, LV_ALIGN_CENTER, 0, -10);

    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, lv_pct(80), 14);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, 30);
    lv_bar_set_range(s_bar, 0, 100);
}

static void update_ui(int percent)
{
    char text[8];
    snprintf(text, sizeof(text), "%d%%", percent);
    lv_label_set_text(s_value_label, text);
    lv_bar_set_value(s_bar, percent, LV_ANIM_OFF);
}

static void lvgl_loop_once(void)
{
    const uint32_t now = millis();
    lv_tick_inc(now - s_last_tick_ms);
    s_last_tick_ms = now;
    lv_timer_handler();
}

void setup(void)
{
    Serial.begin(115200);
    delay(100);
    Serial.println("ESP32-C5-LCD-1.47 backlight fade");

    init_display();
    init_lvgl();
    create_ui();
}

void loop(void)
{
    lvgl_loop_once();

    const uint32_t now = millis();
    if (now - s_last_fade_ms >= 90) {
        s_last_fade_ms = now;
        set_backlight(s_brightness);
        update_ui(s_brightness);

        s_brightness += s_step;
        if (s_brightness >= 100) {
            s_brightness = 100;
            s_step = -5;
        } else if (s_brightness <= 0) {
            s_brightness = 0;
            s_step = 5;
        }
    }

    delay(5);
}
