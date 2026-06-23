/*
 * SPDX-FileCopyrightText: 2026 Waveshare Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"

#include "bsp/esp-bsp.h"
#include "lvgl.h"

#define SD_TEST_FILE BSP_SD_MOUNT_POINT "/esp32_c5_lcd_demo.txt"
#define SD_TEST_PAYLOAD "ESP32-C5-LCD-1.47 SD card read/write OK\n"
#define ROW_Y_START 66
#define ROW_Y_STEP 20
#define ROW_HEIGHT 18

static const char *TAG = "sdcard_rw";

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
} item_t;

static item_t s_items[ITEM_COUNT] = {
    [ITEM_MOUNT] = {.name = "Mount"},
    [ITEM_CARD] = {.name = "Card"},
    [ITEM_WRITE] = {.name = "Write"},
    [ITEM_READ] = {.name = "Read"},
    [ITEM_UNMOUNT] = {.name = "Unmount"},
};

static lv_obj_t *s_summary_label;

// Keep one active-screen helper so the same source works with LVGL 8 and LVGL 9.
#if LVGL_VERSION_MAJOR >= 9
#define LABEL_LONG_DOT LV_LABEL_LONG_MODE_DOTS
static lv_obj_t *active_screen(void)
{
    return lv_screen_active();
}
#else
#define LABEL_LONG_DOT LV_LABEL_LONG_DOT
static lv_obj_t *active_screen(void)
{
    return lv_scr_act();
}
#endif

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

static void set_summary(const char *text, lv_color_t color)
{
    if (!bsp_display_lock(100)) {
        return;
    }

    lv_label_set_text(s_summary_label, text);
    lv_obj_set_style_text_color(s_summary_label, color, 0);
    bsp_display_unlock();
}

static void set_item(item_id_t id, item_status_t status, const char *detail)
{
    if (!bsp_display_lock(100)) {
        return;
    }

    lv_obj_set_style_bg_color(s_items[id].row, status_color(status), 0);
    lv_label_set_text(s_items[id].status, status_text(status));
    lv_label_set_text(s_items[id].detail, detail ? detail : "");
    bsp_display_unlock();
}

static void create_ui(void)
{
    lv_obj_t *scr = active_screen();
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
    lv_label_set_text(file_label, "File: " SD_TEST_FILE);
    lv_obj_set_width(file_label, lv_pct(92));
    lv_label_set_long_mode(file_label, LABEL_LONG_DOT);
    lv_obj_set_style_text_color(file_label, lv_color_hex(0x93c5fd), 0);
    lv_obj_align(file_label, LV_ALIGN_TOP_MID, 0, 48);

    for (int i = 0; i < ITEM_COUNT; i++) {
        // Each row is updated as the SD-card test advances.
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
        lv_label_set_long_mode(name, LABEL_LONG_DOT);
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
        lv_label_set_long_mode(s_items[i].detail, LABEL_LONG_DOT);
        lv_obj_set_style_text_align(s_items[i].detail, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(s_items[i].detail, LV_ALIGN_RIGHT_MID, -7, 0);

        s_items[i].row = row;
    }
}

static esp_err_t sd_write_file(char *detail, size_t detail_size)
{
    FILE *file = fopen(SD_TEST_FILE, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Open for write failed: %s", strerror(errno));
        snprintf(detail, detail_size, "open: %s", strerror(errno));
        return ESP_FAIL;
    }

    // Use a small test file so the check finishes quickly on newly formatted cards.
    size_t expected = strlen(SD_TEST_PAYLOAD);
    size_t written = fwrite(SD_TEST_PAYLOAD, 1, expected, file);
    int close_ret = fclose(file);
    if (written != expected || close_ret != 0) {
        ESP_LOGE(TAG, "Write failed: %s", strerror(errno));
        snprintf(detail, detail_size, "%u/%u B", (unsigned)written, (unsigned)expected);
        return ESP_FAIL;
    }

    snprintf(detail, detail_size, "%u B written", (unsigned)written);
    return ESP_OK;
}

static esp_err_t sd_read_file(char *detail, size_t detail_size)
{
    char readback[96] = {0};

    FILE *file = fopen(SD_TEST_FILE, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Open for read failed: %s", strerror(errno));
        snprintf(detail, detail_size, "open: %s", strerror(errno));
        return ESP_FAIL;
    }

    size_t expected = strlen(SD_TEST_PAYLOAD);
    size_t read_len = fread(readback, 1, sizeof(readback) - 1, file);
    fclose(file);

    if (read_len != expected || memcmp(readback, SD_TEST_PAYLOAD, expected) != 0) {
        ESP_LOGE(TAG, "Readback mismatch");
        snprintf(detail, detail_size, "%u/%u B mismatch", (unsigned)read_len, (unsigned)expected);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Readback: %s", readback);
    snprintf(detail, detail_size, "Readback OK");
    return ESP_OK;
}

void app_main(void)
{
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }

    bsp_display_rotate(disp, LV_DISPLAY_ROTATION_90);
    ESP_ERROR_CHECK(bsp_display_brightness_set(85));

    if (bsp_display_lock(0)) {
        create_ui();
        bsp_display_unlock();
    }

    ESP_LOGI(TAG, "Mount SD card");
    set_summary("Mounting microSD card...", lv_color_hex(0xfacc15));
    set_item(ITEM_MOUNT, STATUS_RUN, BSP_SD_MOUNT_POINT);

    // The BSP mounts the SDSPI card using the board pins from the schematic.
    esp_err_t ret = bsp_sdcard_mount();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        set_item(ITEM_MOUNT, STATUS_FAIL, esp_err_to_name(ret));
        set_summary("Mount failed. Check card insertion.", lv_color_hex(0xff6b6b));
        goto keep_screen;
    }
    set_item(ITEM_MOUNT, STATUS_PASS, BSP_SD_MOUNT_POINT);

    if (bsp_sdcard) {
        char card_detail[64];
        uint64_t card_mb = ((uint64_t)bsp_sdcard->csd.capacity * bsp_sdcard->csd.sector_size) / (1024 * 1024);
        ESP_LOGI(TAG, "Card name: %.8s, size: %llu MB", bsp_sdcard->cid.name, (unsigned long long)card_mb);
        snprintf(card_detail, sizeof(card_detail), "%.8s, %llu MB", bsp_sdcard->cid.name, (unsigned long long)card_mb);
        set_item(ITEM_CARD, STATUS_PASS, card_detail);
    } else {
        set_item(ITEM_CARD, STATUS_FAIL, "No card info");
    }

    char detail[64];
    set_item(ITEM_WRITE, STATUS_RUN, "Creating file");
    ret = sd_write_file(detail, sizeof(detail));
    set_item(ITEM_WRITE, ret == ESP_OK ? STATUS_PASS : STATUS_FAIL, detail);
    if (ret != ESP_OK) {
        set_summary("Write failed", lv_color_hex(0xff6b6b));
    }

    set_item(ITEM_READ, STATUS_RUN, "Verifying file");
    esp_err_t read_ret = sd_read_file(detail, sizeof(detail));
    set_item(ITEM_READ, read_ret == ESP_OK ? STATUS_PASS : STATUS_FAIL, detail);
    if (read_ret != ESP_OK) {
        set_summary("Readback failed", lv_color_hex(0xff6b6b));
    }

    set_item(ITEM_UNMOUNT, STATUS_RUN, "Unmounting");
    esp_err_t unmount_ret = bsp_sdcard_unmount();
    set_item(ITEM_UNMOUNT, unmount_ret == ESP_OK ? STATUS_PASS : STATUS_FAIL,
             unmount_ret == ESP_OK ? "Done" : esp_err_to_name(unmount_ret));

    bool pass = (ret == ESP_OK) && (read_ret == ESP_OK) && (unmount_ret == ESP_OK);
    set_summary(pass ? "SD card test complete" : "SD card test failed",
                pass ? lv_color_hex(0x86efac) : lv_color_hex(0xff6b6b));
    ESP_LOGI(TAG, "SD card demo complete");

keep_screen:
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
