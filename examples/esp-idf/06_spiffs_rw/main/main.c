/*
 * SPDX-FileCopyrightText: 2026 Waveshare Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "bsp/esp-bsp.h"

#define SPIFFS_TEST_FILE BSP_SPIFFS_MOUNT_POINT "/hello.txt"

static const char *TAG = "spiffs_rw";

void app_main(void)
{
    // SPIFFS uses the "storage" partition from this example's partition table.
    const char *payload = "Hello from ESP32-C5-LCD-1.47 SPIFFS\n";
    char readback[96] = {0};

    ESP_LOGI(TAG, "Mount SPIFFS");
    ESP_ERROR_CHECK(bsp_spiffs_mount());

    FILE *file = fopen(SPIFFS_TEST_FILE, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Open for write failed: %s", strerror(errno));
        ESP_ERROR_CHECK(bsp_spiffs_unmount());
        return;
    }

    size_t expected = strlen(payload);
    size_t written = fwrite(payload, 1, expected, file);
    fclose(file);
    if (written != expected) {
        ESP_LOGE(TAG, "Write failed");
        ESP_ERROR_CHECK(bsp_spiffs_unmount());
        return;
    }

    file = fopen(SPIFFS_TEST_FILE, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Open for read failed: %s", strerror(errno));
        ESP_ERROR_CHECK(bsp_spiffs_unmount());
        return;
    }

    size_t read_len = fread(readback, 1, sizeof(readback) - 1, file);
    fclose(file);
    if (read_len != expected || memcmp(readback, payload, expected) != 0) {
        ESP_LOGE(TAG, "Readback mismatch");
        ESP_ERROR_CHECK(bsp_spiffs_unmount());
        return;
    }

    ESP_LOGI(TAG, "Readback: %s", readback);
    ESP_ERROR_CHECK(bsp_spiffs_unmount());
    ESP_LOGI(TAG, "SPIFFS demo complete");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
