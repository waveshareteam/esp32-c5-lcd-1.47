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

#define SD_SCLK (7)
#define SD_MOSI (6)
#define SD_MISO (5)
#define SD_CS   (4)

#define SD_TEST_FILE          "/esp32_c5_lcd_demo.txt"
#define SD_DISPLAY_FILE       "/sdcard" SD_TEST_FILE
#define SD_TEST_PAYLOAD       "ESP32-C5-LCD-1.47 SD card read/write OK\n"
#define SD_POWERUP_DELAY_MS   (2000)
#define LVGL_BUFFER_HEIGHT    (40)
#define BACKLIGHT_LEDC_CH     (0)
#define BACKLIGHT_LEDC_FREQ_HZ (5000)
#define BACKLIGHT_LEDC_BITS   (8)
#define ROW_Y_START           (66)
#define ROW_Y_STEP            (20)
#define ROW_HEIGHT            (18)

typedef enum {
    ITEM_MOUNT = 0,
    ITEM_CARD,
    ITEM_WRITE,
    ITEM_READ,
    ITEM_UNMOUNT,
    ITEM_COUNT,
} item_id_t;

typedef enum {
    STATUS_WAIT = 0,
    STATUS_RUN,
    STATUS_PASS,
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
static lv_obj_t *s_summary_label;

static item_t s_items[ITEM_COUNT] = {
    {"Mount", nullptr, nullptr, nullptr, STATUS_WAIT},
    {"Card", nullptr, nullptr, nullptr, STATUS_WAIT},
    {"Write", nullptr, nullptr, nullptr, STATUS_WAIT},
    {"Read", nullptr, nullptr, nullptr, STATUS_WAIT},
    {"Unmount", nullptr, nullptr, nullptr, STATUS_WAIT},
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

static const char *card_type_name(uint8_t type)
{
    switch (type) {
    case CARD_MMC:
        return "MMC";
    case CARD_SD:
        return "SDSC";
    case CARD_SDHC:
        return "SDHC";
    case CARD_NONE:
    default:
        return "NONE";
    }
}

static const char *status_text(item_status_t status)
{
    switch (status) {
    case STATUS_RUN:
        return "RUN";
    case STATUS_PASS:
        return "PASS";
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
        return lv_color_hex(0x2563eb);
    case STATUS_PASS:
        return lv_color_hex(0x15803d);
    case STATUS_FAIL:
        return lv_color_hex(0xb91c1c);
    case STATUS_WAIT:
    default:
        return lv_color_hex(0x374151);
    }
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int32_t width = area->x2 - area->x1 + 1;
    const int32_t height = area->y2 - area->y1 + 1;

    digitalWrite(SD_CS, HIGH);
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

static void set_summary(const char *text, lv_color_t color)
{
    lv_label_set_text(s_summary_label, text);
    lv_obj_set_style_text_color(s_summary_label, color, 0);
    lvgl_loop_once();
}

static void set_item(item_id_t id, item_status_t status, const char *detail)
{
    s_items[id].current = status;
    lv_obj_set_style_bg_color(s_items[id].row, status_color(status), 0);
    lv_label_set_text(s_items[id].status, status_text(status));
    lv_label_set_text(s_items[id].detail, detail ? detail : "");
    lvgl_loop_once();
}

static void init_sd_pins(void)
{
    pinMode(LCD_SPI_CS, OUTPUT);
    digitalWrite(LCD_SPI_CS, HIGH);
    pinMode(LCD_SPI_DC, OUTPUT);
    digitalWrite(LCD_SPI_DC, HIGH);
    pinMode(LCD_SPI_RST, OUTPUT);
    digitalWrite(LCD_SPI_RST, HIGH);

    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    pinMode(SD_MISO, INPUT_PULLUP);
    pinMode(SD_MOSI, INPUT_PULLUP);
    pinMode(SD_SCLK, INPUT_PULLUP);
    delay(20);
}

static bool mount_sd_card(char *detail, size_t detail_size)
{
    init_sd_pins();
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

    if (!SD.begin(SD_CS)) {
        snprintf(detail, detail_size, "SD.begin failed");
        Serial.println("SD mount failed");
        return false;
    }

    const uint8_t card_type = SD.cardType();
    if (card_type == CARD_NONE) {
        snprintf(detail, detail_size, "No card");
        Serial.println("SD mount failed: no card");
        SD.end();
        return false;
    }

    snprintf(detail, detail_size, "/sdcard");
    Serial.printf("SD mount success, type: %s\r\n", card_type_name(card_type));
    return true;
}

static bool write_file(char *detail, size_t detail_size)
{
    SD.remove(SD_TEST_FILE);

    File file = SD.open(SD_TEST_FILE, FILE_WRITE);
    if (!file) {
        snprintf(detail, detail_size, "open failed");
        Serial.println("Open for write failed");
        return false;
    }

    const size_t expected = strlen(SD_TEST_PAYLOAD);
    const size_t written = file.print(SD_TEST_PAYLOAD);
    file.close();

    if (written != expected) {
        snprintf(detail, detail_size, "%u/%u B", (unsigned)written, (unsigned)expected);
        Serial.println("Write failed");
        return false;
    }

    snprintf(detail, detail_size, "%u B written", (unsigned)written);
    return true;
}

static bool read_file(char *detail, size_t detail_size)
{
    File file = SD.open(SD_TEST_FILE, FILE_READ);
    if (!file) {
        snprintf(detail, detail_size, "open failed");
        Serial.println("Open for read failed");
        return false;
    }

    String readback = file.readString();
    file.close();

    if (readback != SD_TEST_PAYLOAD) {
        snprintf(detail, detail_size, "mismatch");
        Serial.println("Readback mismatch");
        return false;
    }

    snprintf(detail, detail_size, "Readback OK");
    Serial.print("Readback: ");
    Serial.print(readback);
    return true;
}

static void init_display(void)
{
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

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

static void create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "SD Card Read/Write");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    s_summary_label = lv_label_create(scr);
    lv_label_set_text(s_summary_label, "Waiting for microSD card");
    lv_obj_set_style_text_color(s_summary_label, lv_color_hex(0xb6c2d0), 0);
    lv_obj_align(s_summary_label, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *file_label = lv_label_create(scr);
    lv_label_set_text(file_label, "File: " SD_DISPLAY_FILE);
    lv_obj_set_width(file_label, lv_pct(92));
    lv_label_set_long_mode(file_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_color(file_label, lv_color_hex(0x93c5fd), 0);
    lv_obj_align(file_label, LV_ALIGN_TOP_MID, 0, 48);

    for (int i = 0; i < ITEM_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(scr);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(94), ROW_HEIGHT);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, ROW_Y_START + i * ROW_Y_STEP);
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

    lvgl_loop_once();
}

void setup(void)
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("ESP32-C5-LCD-1.47 SD card RW");

    init_display();
    init_lvgl();
    create_ui();

    delay(SD_POWERUP_DELAY_MS);

    char detail[64];
    set_summary("Mounting microSD card...", lv_color_hex(0xfacc15));
    set_item(ITEM_MOUNT, STATUS_RUN, "/sdcard");
    if (!mount_sd_card(detail, sizeof(detail))) {
        set_item(ITEM_MOUNT, STATUS_FAIL, detail);
        set_summary("Mount failed. Check card insertion.", lv_color_hex(0xff6b6b));
        return;
    }
    set_item(ITEM_MOUNT, STATUS_PASS, detail);

    const uint64_t card_mb = SD.cardSize() / (1024ULL * 1024ULL);
    snprintf(detail, sizeof(detail), "%s, %llu MB", card_type_name(SD.cardType()), (unsigned long long)card_mb);
    Serial.printf("Card size: %llu MB\r\n", (unsigned long long)card_mb);
    set_item(ITEM_CARD, STATUS_PASS, detail);

    set_item(ITEM_WRITE, STATUS_RUN, "Creating file");
    const bool write_ok = write_file(detail, sizeof(detail));
    set_item(ITEM_WRITE, write_ok ? STATUS_PASS : STATUS_FAIL, detail);
    if (!write_ok) {
        set_summary("Write failed", lv_color_hex(0xff6b6b));
    }

    set_item(ITEM_READ, STATUS_RUN, "Verifying file");
    const bool read_ok = read_file(detail, sizeof(detail));
    set_item(ITEM_READ, read_ok ? STATUS_PASS : STATUS_FAIL, detail);
    if (!read_ok) {
        set_summary("Readback failed", lv_color_hex(0xff6b6b));
    }

    set_item(ITEM_UNMOUNT, STATUS_RUN, "Unmounting");
    SD.end();
    set_item(ITEM_UNMOUNT, STATUS_PASS, "Done");

    const bool pass = write_ok && read_ok;
    set_summary(pass ? "SD card test complete" : "SD card test failed",
                pass ? lv_color_hex(0x86efac) : lv_color_hex(0xff6b6b));
    Serial.println(pass ? "SD card demo complete" : "SD card demo failed");
}

void loop(void)
{
    lvgl_loop_once();
    delay(5);
}
