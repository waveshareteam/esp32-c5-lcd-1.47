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
#error "This example expects the repository-pinned LVGL 9.5.0 library."
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

#define LCD_BOOT_DELAY_MS      (1000)
#define LCD_PREINIT_DELAY_MS   (50)
#define LCD_RESET_LOW_MS       (20)
#define LCD_RESET_HIGH_MS      (120)
#define LCD_READY_DELAY_MS     (200)
#define LVGL_REDRAW_DELAY_MS   (80)
#define STARTUP_REDRAW_MS      (2000)
#define STARTUP_REDRAW_STEP_MS (100)
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
static uint32_t s_startup_redraw_until_ms;
static uint32_t s_last_startup_redraw_ms;

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

static void reset_lcd_panel(void)
{
    pinMode(LCD_SPI_CS, OUTPUT);
    digitalWrite(LCD_SPI_CS, HIGH);
    pinMode(LCD_SPI_DC, OUTPUT);
    digitalWrite(LCD_SPI_DC, HIGH);
    delay(LCD_PREINIT_DELAY_MS);

    pinMode(LCD_SPI_RST, OUTPUT);
    digitalWrite(LCD_SPI_RST, LOW);
    delay(LCD_RESET_LOW_MS);
    digitalWrite(LCD_SPI_RST, HIGH);
    delay(LCD_RESET_HIGH_MS);
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
    init_backlight(0);
    reset_lcd_panel();

    if (!s_gfx->begin(LCD_SPI_FREQ_HZ)) {
        Serial.println("LCD init failed");
        while (true) {
            delay(1000);
        }
    }

    s_gfx->invertDisplay(false);
    s_gfx->fillScreen(0x0000);
    s_gfx->displayOn();
    delay(LCD_READY_DELAY_MS);
    set_backlight(100);
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
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x18202a), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32-C5-LCD-1.47");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "LVGL display demo");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xaec0d3), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 38);

    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, lv_pct(92), 82);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x223142), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x3d536b), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *line1 = lv_label_create(panel);
    lv_label_set_text(line1, "LCD: ST7789 172x320");
    lv_obj_set_style_text_color(line1, lv_color_hex(0xffffff), 0);
    lv_obj_align(line1, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *line2 = lv_label_create(panel);
    lv_label_set_text(line2, "SPI: GPIO7 CLK / GPIO6 MOSI");
    lv_obj_set_style_text_color(line2, lv_color_hex(0xc8d6e5), 0);
    lv_obj_align(line2, LV_ALIGN_TOP_LEFT, 0, 24);

    lv_obj_t *bar = lv_bar_create(panel);
    lv_obj_set_size(bar, lv_pct(100), 10);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_value(bar, 72, LV_ANIM_OFF);
}

static void lvgl_loop_once(void)
{
    const uint32_t now = millis();
    lv_tick_inc(now - s_last_tick_ms);
    s_last_tick_ms = now;
    lv_timer_handler();
}

static void keep_startup_redraw_active(void)
{
    const uint32_t now = millis();
    if ((int32_t)(now - s_startup_redraw_until_ms) >= 0) {
        return;
    }
    if (now - s_last_startup_redraw_ms < STARTUP_REDRAW_STEP_MS) {
        return;
    }

    s_last_startup_redraw_ms = now;
    lv_obj_invalidate(lv_screen_active());
}

void setup(void)
{
    delay(LCD_BOOT_DELAY_MS);

    init_display();
    init_lvgl();
    create_ui();
    lvgl_loop_once();
    delay(LVGL_REDRAW_DELAY_MS);
    lv_obj_invalidate(lv_screen_active());
    lvgl_loop_once();
    s_startup_redraw_until_ms = millis() + STARTUP_REDRAW_MS;
    s_last_startup_redraw_ms = millis();

    Serial.begin(115200);
    delay(50);
    Serial.println("LCD init ok");
    Serial.println("ESP32-C5-LCD-1.47 LVGL hello");
}

void loop(void)
{
    lvgl_loop_once();
    keep_startup_redraw_active();
    delay(5);
}
