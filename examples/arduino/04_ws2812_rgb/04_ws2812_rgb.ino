/*
 * SPDX-FileCopyrightText: 2026 Waveshare Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <Arduino.h>


#define RGB_PIN (8)


#ifndef RGB_BRIGHTNESS
#define RGB_BRIGHTNESS (64)
#endif

typedef struct {
    const char *name;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} color_step_t;

static const color_step_t s_colors[] = {
    {"red", RGB_BRIGHTNESS, 0, 0},
    {"green", 0, RGB_BRIGHTNESS, 0},
    {"blue", 0, 0, RGB_BRIGHTNESS},
    {"cyan", 0, RGB_BRIGHTNESS, RGB_BRIGHTNESS},
    {"yellow", RGB_BRIGHTNESS, RGB_BRIGHTNESS, 0},
    {"white", RGB_BRIGHTNESS, RGB_BRIGHTNESS, RGB_BRIGHTNESS},
    {"off", 0, 0, 0},
};

static void set_led(uint8_t red, uint8_t green, uint8_t blue)
{
    rgbLedWriteOrdered(RGB_PIN, LED_COLOR_ORDER_RGB, red, green, blue);

}

void setup(void)
{
    Serial.begin(115200);
    delay(100);
    Serial.println("ESP32-C5-LCD-1.47 WS2812 RGB");

    set_led(0, 0, 0);
}

void loop(void)
{
    for (int i = 0; i < sizeof(s_colors) / sizeof(s_colors[0]); i++) {
        Serial.printf("Set LED %s\r\n", s_colors[i].name);
        set_led(s_colors[i].red, s_colors[i].green, s_colors[i].blue);
        delay(500);
    }
}
