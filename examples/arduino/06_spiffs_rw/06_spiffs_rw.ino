/*
 * SPDX-FileCopyrightText: 2026 Waveshare Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Arduino.h>
#include <SPIFFS.h>

#define SPIFFS_TEST_FILE "/hello.txt"

static bool write_read_check(void)
{
    const char *payload = "Hello from ESP32-C5-LCD-1.47 SPIFFS\n";

    SPIFFS.remove(SPIFFS_TEST_FILE);
    File file = SPIFFS.open(SPIFFS_TEST_FILE, FILE_WRITE);
    if (!file) {
        Serial.println("Open for write failed");
        return false;
    }

    const size_t written = file.print(payload);
    file.close();
    if (written != strlen(payload)) {
        Serial.println("Write failed");
        return false;
    }

    file = SPIFFS.open(SPIFFS_TEST_FILE, FILE_READ);
    if (!file) {
        Serial.println("Open for read failed");
        return false;
    }

    String readback = file.readString();
    file.close();

    if (readback != payload) {
        Serial.println("Readback mismatch");
        return false;
    }

    Serial.print("Readback: ");
    Serial.print(readback);
    return true;
}

void setup(void)
{
    Serial.begin(115200);
    delay(100);
    Serial.println("ESP32-C5-LCD-1.47 SPIFFS RW");

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        return;
    }

    Serial.printf("SPIFFS total: %u bytes, used: %u bytes\r\n", SPIFFS.totalBytes(), SPIFFS.usedBytes());
    Serial.println(write_read_check() ? "SPIFFS demo complete" : "SPIFFS demo failed");
}

void loop(void)
{
    delay(1000);
}
