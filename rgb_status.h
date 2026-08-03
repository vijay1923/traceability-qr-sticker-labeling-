#ifndef RGB_STATUS_H
#define RGB_STATUS_H

#include <Adafruit_NeoPixel.h>
#include "config.h"

Adafruit_NeoPixel RGB(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

uint32_t red;
uint32_t green;
uint32_t blue;
uint32_t yellow;
uint32_t cyan;
uint32_t magenta;
uint32_t white;

bool flashing = false;
unsigned long flash_start_ms = 0;

void RGB_init()
{
    RGB.begin();
    RGB.setBrightness(255);

    red   = RGB.Color(255, 0, 0);
    green = RGB.Color(0, 255, 0);
    blue  = RGB.Color(0, 0, 255);
    yellow = RGB.Color(255, 255, 0);
    cyan = RGB.Color(0, 255, 255);
    magenta = RGB.Color(255, 0, 255);
    white = RGB.Color(255, 255, 255);

    RGB.clear();
    RGB.show();
}

void RGB_setColor(uint32_t color)
{
    RGB.setPixelColor(0, color);
    RGB.show();
}

uint32_t base_color()
{
    return barcode_status ? green : red;
}

void RGB_flash(uint32_t color)
{
    RGB_setColor(color);
    flashing = true;
    flash_start_ms = millis();
}

#endif