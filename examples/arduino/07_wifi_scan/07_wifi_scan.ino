/*
 * SPDX-FileCopyrightText: 2026 Waveshare Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Arduino.h>
#include <WiFi.h>

#define MAX_AP_RECORDS (12)

void setup(void)
{
    Serial.begin(115200);
    delay(100);
    Serial.println("ESP32-C5-LCD-1.47_Wi-Fi_scan");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
}

void loop(void)
{
    Serial.println("StartScan");
    const int ap_count = WiFi.scanNetworks(false, false);

    if (ap_count < 0) {
        Serial.printf("ScanFailed:%d\r\n", ap_count);
    } else {
        Serial.printf("FoundAPs:%d\r\n", ap_count);
        const int shown = ap_count > MAX_AP_RECORDS ? MAX_AP_RECORDS : ap_count;
        for (int i = 0; i < shown; i++) {
            const String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) {
                continue;
            }
            Serial.printf("%d:%s,RSSI=%d,CH=%d\r\n",
                          i + 1,
                          ssid.c_str(),
                          WiFi.RSSI(i),
                          WiFi.channel(i));
        }
    }

    WiFi.scanDelete();
    delay(5000);
}
