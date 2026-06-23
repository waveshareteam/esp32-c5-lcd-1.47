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
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <WiFi.h>
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

#define SD_SCLK (7)
#define SD_MOSI (6)
#define SD_MISO (5)
#define SD_CS   (4)


#define RGB_PIN (8)


#ifndef RGB_BRIGHTNESS
#define RGB_BRIGHTNESS (64)
#endif

#define LVGL_BUFFER_HEIGHT     (40)
#define BACKLIGHT_LEDC_CH      (0)
#define BACKLIGHT_LEDC_FREQ_HZ (5000)
#define BACKLIGHT_LEDC_BITS    (8)

#define SPIFFS_TEST_FILE "/showcase.txt"
#define SD_TEST_FILE     "/showcase.txt"
#define DASH_ROW_Y_START (45)
#define DASH_ROW_STEP    (20)
#define DASH_ROW_HEIGHT  (18)

static const uint32_t s_sd_freq_list[] = {
    400000,
    1000000,
    4000000,
    10000000,
};

typedef enum {
    ITEM_LCD = 0,
    ITEM_LED,
    ITEM_SPIFFS,
    ITEM_SD,
    ITEM_WIFI,
    ITEM_COUNT,
} item_id_t;

typedef enum {
    STATUS_WAIT = 0,
    STATUS_RUN,
    STATUS_PASS,
    STATUS_WARN,
    STATUS_FAIL,
} item_status_t;

typedef struct {
    const char *name;
    lv_obj_t *row;
    lv_obj_t *status;
    lv_obj_t *detail;
    item_status_t current;
} item_t;

static Arduino_DataBus *s_bus = new Arduino_ESP32SPI(
    LCD_SPI_DC, LCD_SPI_CS, LCD_SPI_SCLK, LCD_SPI_MOSI, LCD_SPI_MISO);

static Arduino_GFX *s_gfx = new Arduino_ST7789(
    s_bus, LCD_SPI_RST, LCD_ROTATION_LANDSCAPE, true,
    LCD_H_RES, LCD_V_RES,
    LCD_X_GAP, LCD_Y_GAP, LCD_X_GAP, LCD_Y_GAP);

static uint8_t *s_lvgl_buf;
static uint32_t s_last_tick_ms;
static lv_obj_t *s_final_label;
static bool s_failed;
static bool s_warned;

static item_t s_items[ITEM_COUNT] = {
    {"LCD", nullptr, nullptr, nullptr, STATUS_WAIT},
    {"WS2812", nullptr, nullptr, nullptr, STATUS_WAIT},
    {"SPIFFS", nullptr, nullptr, nullptr, STATUS_WAIT},
    {"SD", nullptr, nullptr, nullptr, STATUS_WAIT},
    {"Wi-Fi", nullptr, nullptr, nullptr, STATUS_WAIT},
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

static void set_led(uint8_t red, uint8_t green, uint8_t blue)
{
    rgbLedWriteOrdered(RGB_PIN, LED_COLOR_ORDER_RGB, red, green, blue);
}

static void init_sd_pins(void)
{
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(SD_MISO, INPUT_PULLUP);
    pinMode(SD_MOSI, INPUT_PULLUP);
    pinMode(SD_SCLK, INPUT_PULLUP);
    delay(20);
}

static bool mount_sd_card(uint32_t *mounted_freq)
{
    init_sd_pins();
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

    for (int i = 0; i < sizeof(s_sd_freq_list) / sizeof(s_sd_freq_list[0]); i++) {
        const uint32_t freq = s_sd_freq_list[i];
        if (SD.begin(SD_CS, SPI, freq)) {
            if (SD.cardType() == CARD_NONE || SD.cardSize() == 0) {
                SD.end();
                digitalWrite(SD_CS, HIGH);
                delay(100);
                continue;
            }
            if (mounted_freq) {
                *mounted_freq = freq;
            }
            return true;
        }

        SD.end();
        digitalWrite(SD_CS, HIGH);
        delay(100);
    }

    return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int32_t width = area->x2 - area->x1 + 1;
    const int32_t height = area->y2 - area->y1 + 1;

    s_gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, width, height);
    lv_display_flush_ready(disp);
}

static void lvgl_loop_once(void)
{
    const uint32_t now = millis();
    lv_tick_inc(now - s_last_tick_ms);
    s_last_tick_ms = now;
    lv_timer_handler();
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
    init_backlight(85);
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

static const char *status_text(item_status_t status)
{
    switch (status) {
    case STATUS_RUN:
        return "RUN";
    case STATUS_PASS:
        return "PASS";
    case STATUS_WARN:
        return "CHECK";
    case STATUS_FAIL:
        return "FAIL";
    case STATUS_WAIT:
    default:
        return "WAIT";
    }
}

static lv_color_t status_color(item_status_t status)
{
    switch (status) {
    case STATUS_RUN:
        return lv_color_hex(0x1d4ed8);
    case STATUS_PASS:
        return lv_color_hex(0x15803d);
    case STATUS_WARN:
        return lv_color_hex(0xfacc15);
    case STATUS_FAIL:
        return lv_color_hex(0xb91c1c);
    case STATUS_WAIT:
    default:
        return lv_color_hex(0x374151);
    }
}

static void set_item(item_id_t id, item_status_t status, const char *detail)
{
    s_items[id].current = status;
    lv_obj_set_style_bg_color(s_items[id].row, status_color(status), 0);
    lv_label_set_text(s_items[id].status, status_text(status));
    lv_label_set_text(s_items[id].detail, detail ? detail : "");
    lvgl_loop_once();
}

static void create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32-C5-LCD-1.47");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Arduino showcase");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xb6c2d0), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 28);

    for (int i = 0; i < ITEM_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(scr);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(94), DASH_ROW_HEIGHT);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, DASH_ROW_Y_START + i * DASH_ROW_STEP);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, status_color(STATUS_WAIT), 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, s_items[i].name);
        lv_obj_set_style_text_color(name, lv_color_hex(0xffffff), 0);
        lv_obj_set_width(name, 72);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 7, 0);

        s_items[i].status = lv_label_create(row);
        lv_label_set_text(s_items[i].status, status_text(STATUS_WAIT));
        lv_obj_set_style_text_color(s_items[i].status, lv_color_hex(0xffffff), 0);
        lv_obj_set_width(s_items[i].status, 52);
        lv_obj_set_style_text_align(s_items[i].status, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_items[i].status, LV_ALIGN_LEFT_MID, 80, 0);

        s_items[i].detail = lv_label_create(row);
        lv_label_set_text(s_items[i].detail, "");
        lv_obj_set_style_text_color(s_items[i].detail, lv_color_hex(0xffffff), 0);
        lv_obj_set_width(s_items[i].detail, 160);
        lv_label_set_long_mode(s_items[i].detail, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(s_items[i].detail, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(s_items[i].detail, LV_ALIGN_RIGHT_MID, -7, 0);

        s_items[i].row = row;
        s_items[i].current = STATUS_WAIT;
    }

    s_final_label = lv_label_create(scr);
    lv_label_set_text(s_final_label, "Starting...");
    lv_obj_set_style_text_color(s_final_label, lv_color_hex(0xfacc15), 0);
    lv_obj_align(s_final_label, LV_ALIGN_BOTTOM_MID, 0, -2);
}

static bool file_rw_check(fs::FS &fs, const char *path, const char *payload)
{
    fs.remove(path);
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
        return false;
    }

    const size_t written = file.print(payload);
    file.close();
    if (written != strlen(payload)) {
        return false;
    }

    file = fs.open(path, FILE_READ);
    if (!file) {
        return false;
    }

    String readback = file.readString();
    file.close();
    return readback == payload;
}

static void run_showcase(void)
{
    set_item(ITEM_LCD, STATUS_PASS, "ST7789 ready");

    set_item(ITEM_LED, STATUS_RUN, "Cycling");
    set_led(24, 0, 0);
    delay(120);
    set_led(0, 24, 0);
    delay(120);
    set_led(0, 0, 24);
    delay(120);
    set_item(ITEM_LED, STATUS_PASS, "RGB OK");

    set_item(ITEM_SPIFFS, STATUS_RUN, "Mounting");
    if (SPIFFS.begin(true) && file_rw_check(SPIFFS, SPIFFS_TEST_FILE, "SPIFFS showcase OK\n")) {
        char detail[32];
        snprintf(detail, sizeof(detail), "%u KB used", SPIFFS.usedBytes() / 1024);
        set_item(ITEM_SPIFFS, STATUS_PASS, detail);
    } else {
        set_item(ITEM_SPIFFS, STATUS_FAIL, "RW failed");
    }

    set_item(ITEM_SD, STATUS_RUN, "Mounting");
    uint32_t sd_freq = 0;
    if (mount_sd_card(&sd_freq)) {
        if (file_rw_check(SD, SD_TEST_FILE, "SD showcase OK\n")) {
            char detail[32];
            if (sd_freq >= 1000000) {
                snprintf(detail, sizeof(detail), "%llu MB @%luM",
                         (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)),
                         (unsigned long)(sd_freq / 1000000));
            } else {
                snprintf(detail, sizeof(detail), "%llu MB @%luK",
                         (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)),
                         (unsigned long)(sd_freq / 1000));
            }
            set_item(ITEM_SD, STATUS_PASS, detail);
        } else {
            set_item(ITEM_SD, STATUS_FAIL, "RW failed");
        }
        SD.end();
    } else {
        set_item(ITEM_SD, STATUS_WARN, "No card");
    }

    set_item(ITEM_WIFI, STATUS_RUN, "Scanning");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    int ap_count = WiFi.scanNetworks(false, true);
    if (ap_count >= 0) {
        char detail[32];
        snprintf(detail, sizeof(detail), "%d APs found", ap_count);
        set_item(ITEM_WIFI, STATUS_PASS, detail);
    } else {
        set_item(ITEM_WIFI, STATUS_WARN, "Scan failed");
    }
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);

    s_failed = false;
    s_warned = false;
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (s_items[i].current == STATUS_FAIL) {
            s_failed = true;
        } else if (s_items[i].current == STATUS_WARN) {
            s_warned = true;
        }
    }

    lv_label_set_text(s_final_label, s_failed ? "Finished with failures" :
                      s_warned ? "Finished with warnings" : "Showcase complete");
    lv_obj_set_style_text_color(s_final_label, s_failed ? lv_color_hex(0xff6b6b) :
                                s_warned ? lv_color_hex(0xfacc15) : lv_color_hex(0x86efac), 0);
    lvgl_loop_once();
}

void setup(void)
{
    Serial.begin(115200);
    delay(100);
    Serial.println("ESP32-C5-LCD-1.47 board showcase");

    init_display();
    init_lvgl();
    set_led(0, 0, 0);

    create_ui();
    run_showcase();
}

void loop(void)
{
    lvgl_loop_once();
    set_led(s_failed ? 24 : s_warned ? 20 : 0,
            s_failed ? 0 : s_warned ? 16 : 24,
            0);
    delay(1000);
}
