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

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "bsp/esp-bsp.h"
#include "lvgl.h"

#define SHOWCASE_SD_FILE      BSP_SD_MOUNT_POINT "/showcase.txt"
#define SHOWCASE_SPIFFS_FILE  BSP_SPIFFS_MOUNT_POINT "/showcase.txt"
#define SHOWCASE_TASK_STACK   (8 * 1024)
#define SHOWCASE_MAX_APS      8
#define DASHBOARD_ROW_Y_START 45
#define DASHBOARD_ROW_STEP    20
#define DASHBOARD_ROW_HEIGHT  18

static const char *TAG = "board_showcase";

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

static item_t s_items[ITEM_COUNT] = {
    [ITEM_LCD] = {.name = "LCD"},
    [ITEM_LED] = {.name = "WS2812"},
    [ITEM_SPIFFS] = {.name = "SPIFFS"},
    [ITEM_SD] = {.name = "SD"},
    [ITEM_WIFI] = {.name = "Wi-Fi"},
};

static lv_obj_t *s_final_label;

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

    // LVGL object updates must be serialized with the BSP display lock.
    if (!bsp_display_lock(0)) {
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
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x111827), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "ESP32-C5-LCD-1.47");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Board showcase");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xb6c2d0), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 28);

    for (int i = 0; i < ITEM_COUNT; i++) {
        // Each dashboard row tracks one board resource check.
        lv_obj_t *row = lv_obj_create(scr);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(94), DASHBOARD_ROW_HEIGHT);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, DASHBOARD_ROW_Y_START + i * DASHBOARD_ROW_STEP);
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
        s_items[i].current = STATUS_WAIT;
    }

    s_final_label = lv_label_create(scr);
    lv_label_set_text(s_final_label, "Starting...");
    lv_obj_set_style_text_color(s_final_label, lv_color_hex(0xfacc15), 0);
    lv_obj_align(s_final_label, LV_ALIGN_BOTTOM_MID, 0, -2);
}

static esp_err_t write_read_file(const char *path, const char *payload)
{
    char readback[96] = {0};
    size_t expected = strlen(payload);

    FILE *file = fopen(path, "wb");
    if (!file) {
        return ESP_FAIL;
    }

    size_t written = fwrite(payload, 1, expected, file);
    int close_ret = fclose(file);
    if (written != expected || close_ret != 0) {
        return ESP_FAIL;
    }

    file = fopen(path, "rb");
    if (!file) {
        return ESP_FAIL;
    }

    size_t read_len = fread(readback, 1, sizeof(readback) - 1, file);
    fclose(file);

    if (read_len != expected || memcmp(readback, payload, expected) != 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase nvs failed");
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t wifi_scan_once(char *detail, size_t detail_size)
{
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "nvs init failed");

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {
            .min = 100,
            .max = 300,
        },
    };

    ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        esp_wifi_stop();
        esp_wifi_deinit();
        return ret;
    }

    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        esp_wifi_stop();
        esp_wifi_deinit();
        return ret;
    }

    wifi_ap_record_t records[SHOWCASE_MAX_APS];
    memset(records, 0, sizeof(records));
    uint16_t record_count = ap_count > SHOWCASE_MAX_APS ? SHOWCASE_MAX_APS : ap_count;
    int best_rssi = -127;
    if (record_count > 0) {
        ret = esp_wifi_scan_get_ap_records(&record_count, records);
        if (ret != ESP_OK) {
            esp_wifi_stop();
            esp_wifi_deinit();
            return ret;
        }
        for (int i = 0; i < record_count; i++) {
            if (records[i].rssi > best_rssi) {
                best_rssi = records[i].rssi;
            }
        }
    }

    esp_wifi_stop();
    esp_wifi_deinit();

    if (ap_count == 0) {
        snprintf(detail, detail_size, "No AP found");
        return ESP_ERR_NOT_FOUND;
    }

    snprintf(detail, detail_size, "%u AP, best %d", ap_count, best_rssi);
    return ESP_OK;
}

static void showcase_task(void *arg)
{
    (void)arg;
    bool led_ready = false;

    // Run hardware checks outside app_main so display startup remains responsive.
    set_item(ITEM_LCD, STATUS_PASS, "LVGL + backlight");

    set_item(ITEM_LED, STATUS_RUN, "Cycling");
    esp_err_t ret = bsp_ws2812b_init();
    if (ret == ESP_OK) {
        led_ready = true;
        ret = bsp_setledcolor(0, 32, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
        ret |= bsp_setledcolor(0, 0, 32, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
        ret |= bsp_setledcolor(0, 0, 0, 32);
        set_item(ITEM_LED, ret == ESP_OK ? STATUS_PASS : STATUS_FAIL,
                 ret == ESP_OK ? "RGB OK" : "Set failed");
    } else {
        set_item(ITEM_LED, STATUS_FAIL, esp_err_to_name(ret));
    }

    set_item(ITEM_SPIFFS, STATUS_RUN, "Mount");
    ret = bsp_spiffs_mount();
    if (ret == ESP_OK) {
        ret = write_read_file(SHOWCASE_SPIFFS_FILE, "showcase spiffs ok\n");
        bsp_spiffs_unmount();
        set_item(ITEM_SPIFFS, ret == ESP_OK ? STATUS_PASS : STATUS_FAIL,
                 ret == ESP_OK ? "RW OK" : "RW failed");
    } else {
        set_item(ITEM_SPIFFS, STATUS_FAIL, esp_err_to_name(ret));
    }

    set_item(ITEM_SD, STATUS_RUN, "Mount");
    ret = bsp_sdcard_mount();
    if (ret == ESP_OK) {
        char detail[48];
        uint64_t card_mb = 0;
        if (bsp_sdcard) {
            card_mb = ((uint64_t)bsp_sdcard->csd.capacity * bsp_sdcard->csd.sector_size) / (1024 * 1024);
        }
        ret = write_read_file(SHOWCASE_SD_FILE, "showcase sd ok\n");
        bsp_sdcard_unmount();
        if (ret == ESP_OK && card_mb > 0) {
            snprintf(detail, sizeof(detail), "%llu MB RW OK", (unsigned long long)card_mb);
            set_item(ITEM_SD, STATUS_PASS, detail);
        } else if (ret == ESP_OK) {
            set_item(ITEM_SD, STATUS_WARN, "No card");
        } else {
            set_item(ITEM_SD, STATUS_FAIL, "RW failed");
        }
    } else {
        set_item(ITEM_SD, STATUS_WARN, "No card");
    }

    set_item(ITEM_WIFI, STATUS_RUN, "Scanning");
    char wifi_detail[48] = {0};
    ret = wifi_scan_once(wifi_detail, sizeof(wifi_detail));
    set_item(ITEM_WIFI, ret == ESP_OK ? STATUS_PASS : STATUS_WARN, wifi_detail[0] ? wifi_detail : esp_err_to_name(ret));

    bool failed = false;
    bool warned = false;
    for (int i = 0; i < ITEM_COUNT; i++) {
        if (s_items[i].current == STATUS_FAIL) {
            failed = true;
        } else if (s_items[i].current == STATUS_WARN) {
            warned = true;
        }
    }

    if (bsp_display_lock(0)) {
        lv_label_set_text(s_final_label, failed ? "Finished with failures" :
                          warned ? "Finished with warnings" : "Showcase complete");
        lv_obj_set_style_text_color(s_final_label, failed ? lv_color_hex(0xff6b6b) :
                                    warned ? lv_color_hex(0xfacc15) : lv_color_hex(0x86efac), 0);
        bsp_display_unlock();
    }

    while (1) {
        if (led_ready) {
            if (failed) {
                bsp_setledcolor(0, 24, 0, 0);
            } else if (warned) {
                bsp_setledcolor(0, 20, 16, 0);
            } else {
                bsp_setledcolor(0, 0, 24, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
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

    BaseType_t task_ret = xTaskCreate(showcase_task, "showcase", SHOWCASE_TASK_STACK, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create showcase task");
    }
}
