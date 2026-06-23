/*
 * SPDX-FileCopyrightText: 2026 Waveshare Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "backlight_fade";
static lv_obj_t *s_value_label;
static lv_obj_t *s_bar;

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
    if (!s_value_label || !s_bar) {
        return;
    }

    // Mirror the current backlight PWM value on the LVGL label and bar.
    char text[8];
    snprintf(text, sizeof(text), "%d%%", percent);
    lv_label_set_text(s_value_label, text);
    lv_bar_set_value(s_bar, percent, LV_ANIM_OFF);
}

void app_main(void)
{
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }

    bsp_display_rotate(disp, LV_DISPLAY_ROTATION_90);

    if (bsp_display_lock(0)) {
        create_ui();
        bsp_display_unlock();
    }

    int brightness = 0;
    int step = 5;

    while (1) {
        // Sweep brightness up and down to demonstrate the LEDC backlight driver.
        ESP_ERROR_CHECK(bsp_display_brightness_set(brightness));
        if (bsp_display_lock(0)) {
            update_ui(brightness);
            bsp_display_unlock();
        }

        brightness += step;
        if (brightness >= 100) {
            brightness = 100;
            step = -5;
        } else if (brightness <= 0) {
            brightness = 0;
            step = 5;
        }

        vTaskDelay(pdMS_TO_TICKS(90));
    }
}
