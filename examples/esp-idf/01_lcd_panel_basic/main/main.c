/*
 * SPDX-FileCopyrightText: 2026 Waveshare Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"

#define DRAW_BLOCK_HEIGHT 40
#define MOVING_BAR_X 24
#define MOVING_BAR_WIDTH 124
#define MOVING_BAR_HEIGHT 28
#define MOVING_BAR_STEP 2
#define MOVING_BAR_DELAY_MS 40

static const char *TAG = "lcd_basic";

// RGB565 values are stored in the byte order required by this ST7789 panel.
static const uint16_t s_bar_colors[] = {
    0x00f8,
    0xe007,
    0x1f00,
    0xffff,
    0x0000,
};

static bool lcd_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel_io;
    (void)edata;

    SemaphoreHandle_t transfer_done = (SemaphoreHandle_t)user_ctx;
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(transfer_done, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
}

static uint16_t lcd_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    uint16_t color = rgb565(red, green, blue);
    // The ST7789 panel on this board expects RGB565 bytes in swapped order.
    return (color << 8) | (color >> 8);
}

static void fill_buffer(uint16_t *buffer, size_t pixels, uint16_t color)
{
    for (size_t i = 0; i < pixels; i++) {
        buffer[i] = color;
    }
}

static uint16_t color_bar_color_for_x(int x)
{
    const int bar_count = sizeof(s_bar_colors) / sizeof(s_bar_colors[0]);
    const int bar_width = BSP_LCD_H_RES / bar_count;
    int index = x / bar_width;

    if (index >= bar_count) {
        index = bar_count - 1;
    }

    return s_bar_colors[index];
}

static esp_err_t wait_lcd_transfer_done(SemaphoreHandle_t transfer_done)
{
    if (xSemaphoreTake(transfer_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "LCD color transfer timeout");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static esp_err_t push_bitmap(esp_lcd_panel_handle_t panel, SemaphoreHandle_t transfer_done,
                             uint16_t *buffer,
                             int x_start, int y_start, int width, int height)
{
    // The SPI LCD driver queues color transfers, so the DMA buffer must stay unchanged
    // until on_color_trans_done is called.
    (void)xSemaphoreTake(transfer_done, 0);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel, x_start, y_start,
                                                  x_start + width, y_start + height,
                                                  buffer),
                        TAG, "draw bitmap failed");
    return wait_lcd_transfer_done(transfer_done);
}

static esp_err_t draw_solid_rect(esp_lcd_panel_handle_t panel, SemaphoreHandle_t transfer_done,
                                 uint16_t *buffer,
                                 int x_start, int y_start, int width, int height, uint16_t color)
{
    fill_buffer(buffer, width * height, color);
    return push_bitmap(panel, transfer_done, buffer, x_start, y_start, width, height);
}

static esp_err_t draw_color_bars_region(esp_lcd_panel_handle_t panel, SemaphoreHandle_t transfer_done,
                                        uint16_t *buffer,
                                        int x_start, int y_start, int width, int height)
{
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            buffer[row * width + col] = color_bar_color_for_x(x_start + col);
        }
    }

    return push_bitmap(panel, transfer_done, buffer, x_start, y_start, width, height);
}

static esp_err_t draw_moving_bar_region(esp_lcd_panel_handle_t panel, SemaphoreHandle_t transfer_done,
                                        uint16_t *buffer,
                                        int old_y, int new_y, uint16_t bar_color)
{
    int y_start = (old_y < new_y) ? old_y : new_y;
    int y_end = ((old_y > new_y) ? old_y : new_y) + MOVING_BAR_HEIGHT;
    int height = y_end - y_start;

    for (int row = 0; row < height; row++) {
        int screen_y = y_start + row;
        bool inside_bar = (screen_y >= new_y) && (screen_y < new_y + MOVING_BAR_HEIGHT);

        for (int col = 0; col < MOVING_BAR_WIDTH; col++) {
            buffer[row * MOVING_BAR_WIDTH + col] =
                inside_bar ? bar_color : color_bar_color_for_x(MOVING_BAR_X + col);
        }
    }

    return push_bitmap(panel, transfer_done, buffer,
                       MOVING_BAR_X, y_start, MOVING_BAR_WIDTH, height);
}

static esp_err_t draw_color_bars(esp_lcd_panel_handle_t panel, SemaphoreHandle_t transfer_done,
                                 uint16_t *buffer)
{
    // Draw the background in DMA-sized horizontal chunks instead of allocating a full frame buffer.
    for (int y = 0; y < BSP_LCD_V_RES; y += DRAW_BLOCK_HEIGHT) {
        int height = (y + DRAW_BLOCK_HEIGHT > BSP_LCD_V_RES) ? (BSP_LCD_V_RES - y) : DRAW_BLOCK_HEIGHT;
        ESP_RETURN_ON_ERROR(draw_color_bars_region(panel, transfer_done, buffer,
                                                   0, y, BSP_LCD_H_RES, height),
                            TAG, "draw color bar failed");
    }
    return ESP_OK;
}

void app_main(void)
{
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;

    // The BSP creates the LCD SPI bus, panel IO, and ST7789 panel driver.
    ESP_ERROR_CHECK(bsp_display_new(NULL, &panel, &io));
    ESP_ERROR_CHECK(bsp_display_brightness_init());
    ESP_ERROR_CHECK(bsp_display_brightness_set(80));

    SemaphoreHandle_t transfer_done = xSemaphoreCreateBinary();
    if (!transfer_done) {
        ESP_LOGE(TAG, "No memory for LCD transfer semaphore");
        return;
    }

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = lcd_color_trans_done_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io, &cbs, transfer_done));

    const size_t buffer_pixels = BSP_LCD_H_RES * DRAW_BLOCK_HEIGHT;
    uint16_t *buffer = heap_caps_malloc(buffer_pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!buffer) {
        ESP_LOGE(TAG, "No DMA memory for LCD buffer");
        vSemaphoreDelete(transfer_done);
        return;
    }

    ESP_ERROR_CHECK(draw_color_bars(panel, transfer_done, buffer));
    vTaskDelay(pdMS_TO_TICKS(1200));

    int y = 0;
    int direction = 1;
    uint16_t moving_bar_color = lcd_rgb565(255, 196, 0);
    ESP_ERROR_CHECK(draw_solid_rect(panel, transfer_done, buffer,
                                    MOVING_BAR_X, y, MOVING_BAR_WIDTH, MOVING_BAR_HEIGHT,
                                    moving_bar_color));

    while (1) {
        int old_y = y;
        y += direction * MOVING_BAR_STEP;
        if (y <= 0) {
            y = 0;
            direction = 1;
        } else if (y >= BSP_LCD_V_RES - MOVING_BAR_HEIGHT) {
            y = BSP_LCD_V_RES - MOVING_BAR_HEIGHT;
            direction = -1;
        }

        // Redraw the old and new bar area in one LCD transaction to avoid visible flicker.
        ESP_ERROR_CHECK(draw_moving_bar_region(panel, transfer_done, buffer,
                                               old_y, y, moving_bar_color));

        vTaskDelay(pdMS_TO_TICKS(MOVING_BAR_DELAY_MS));
    }
}
