/*
 * SPDX-FileCopyrightText: 2026 Waveshare Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "bsp/esp-bsp.h"

static const char *TAG = "ws2812_rgb";

typedef struct {
    const char *name;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} color_step_t;

static const color_step_t s_colors[] = {
    // Use moderate brightness values suitable for the onboard indicator LED.
    {"red", 64, 0, 0},
    {"green", 0, 64, 0},
    {"blue", 0, 0, 64},
    {"cyan", 0, 48, 48},
    {"yellow", 48, 32, 0},
    {"white", 32, 32, 32},
    {"off", 0, 0, 0},
};

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_ws2812b_init());

    while (1) {
        // Cycle the single onboard WS2812B through common customer-test colors.
        for (int i = 0; i < sizeof(s_colors) / sizeof(s_colors[0]); i++) {
            ESP_LOGI(TAG, "Set LED %s", s_colors[i].name);
            ESP_ERROR_CHECK(bsp_setledcolor(0, s_colors[i].red, s_colors[i].green, s_colors[i].blue));
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}
