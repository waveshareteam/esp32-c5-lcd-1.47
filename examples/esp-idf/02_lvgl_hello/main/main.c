/*
 * SPDX-FileCopyrightText: 2026 Waveshare Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "lvgl_hello";

// Keep one active-screen helper so the same source works with LVGL 8 and LVGL 9.
#if LVGL_VERSION_MAJOR >= 9
static lv_obj_t *active_screen(void)
{
    return lv_screen_active();
}
#else
static lv_obj_t *active_screen(void)
{
    return lv_scr_act();
}
#endif

static void create_ui(void)
{
    lv_obj_t *scr = active_screen();
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

void app_main(void)
{
    ESP_LOGI(TAG, "Start display");
    // Start the BSP display service, including the LCD panel and LVGL port.
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }

    bsp_display_rotate(disp, LV_DISPLAY_ROTATION_90);
    ESP_ERROR_CHECK(bsp_display_backlight_on());

    // LVGL calls must be serialized with the BSP display lock.
    if (bsp_display_lock(0)) {
        create_ui();
        bsp_display_unlock();
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
