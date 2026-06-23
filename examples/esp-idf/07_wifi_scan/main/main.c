/*
 * SPDX-FileCopyrightText: 2026 Waveshare Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

// Limit scan and log output so the monitor stays readable when many APs are nearby.
#define MAX_SCAN_RECORDS 24
#define MAX_PRINT_RECORDS 12
#define WIFI_SCAN_TASK_STACK_SIZE (8 * 1024)

static const char *TAG = "wifi_scan";
static wifi_ap_record_t s_records[MAX_SCAN_RECORDS];

static bool ssid_to_printable_ascii(const uint8_t *ssid, char *out, size_t out_size)
{
    if (out_size == 0) {
        return false;
    }

    size_t len = 0;
    while (len < 32 && ssid[len] != '\0') {
        uint8_t ch = ssid[len];
        if (ch < 0x20 || ch > 0x7e) {
            out[0] = '\0';
            return false;
        }

        if (len + 1 < out_size) {
            out[len] = (char)ch;
        }
        len++;
    }

    if (len == 0 || len >= out_size) {
        out[0] = '\0';
        return false;
    }

    out[len] = '\0';
    return true;
}

static esp_err_t init_nvs(void)
{
    // NVS is required by the Wi-Fi driver and may need erasing after partition changes.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase nvs failed");
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t init_wifi_sta(void)
{
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "nvs init failed");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");

    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set sta mode failed");
    return esp_wifi_start();
}

static void wifi_scan_task(void *arg)
{
    (void)arg;

    ESP_ERROR_CHECK(init_wifi_sta());

    while (1) {
        // Active scan gives quick customer-visible feedback on the serial monitor.
        wifi_scan_config_t scan_config = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = false,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
            .scan_time.active = {
                .min = 100,
                .max = 300,
            },
        };

        ESP_LOGI(TAG, "StartScan");
        ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

        uint16_t ap_count = 0;
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

        memset(s_records, 0, sizeof(s_records));
        uint16_t record_count = ap_count > MAX_SCAN_RECORDS ? MAX_SCAN_RECORDS : ap_count;
        if (record_count > 0) {
            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&record_count, s_records));
        }

        ESP_LOGI(TAG, "FoundAPs:%u", ap_count);
        uint16_t printed_count = 0;
        uint16_t skipped_count = 0;
        for (int i = 0; i < record_count; i++) {
            char ssid[33];
            if (!ssid_to_printable_ascii(s_records[i].ssid, ssid, sizeof(ssid))) {
                skipped_count++;
                continue;
            }

            ESP_LOGI(TAG, "%u:%s,RSSI=%d,CH=%u",
                     (unsigned)(printed_count + 1), ssid, s_records[i].rssi, s_records[i].primary);
            printed_count++;
            if (printed_count >= MAX_PRINT_RECORDS) {
                skipped_count += record_count - i - 1;
                break;
            }
        }

        if (skipped_count > 0) {
            ESP_LOGI(TAG, "SkippedSSIDs:%u", skipped_count);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    BaseType_t ret = xTaskCreate(wifi_scan_task, "wifi_scan", WIFI_SCAN_TASK_STACK_SIZE, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "FailedCreateWiFiScanTask");
    }
}
