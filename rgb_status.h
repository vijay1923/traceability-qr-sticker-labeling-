#ifndef RGB_STATUS_H
#define RGB_STATUS_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

// barcode_status is defined (not just declared) directly inside
// usb_scanner_Lib.h - that header has NO include guard, so it's only ever
// #include'd once in the whole project (main.ino). Re-including it here
// would redefine everything in it a second time and fail to compile.
// Declaring just the one symbol this file actually needs avoids that.
extern uint8_t barcode_status; // used by base_color() below

extern Adafruit_NeoPixel RGB;

extern uint32_t red;
extern uint32_t green;
extern uint32_t blue;
extern uint32_t yellow;
extern uint32_t cyan;
extern uint32_t magenta;
extern uint32_t white;
extern uint32_t orange;

extern bool flashing;
extern unsigned long flash_start_ms;

void RGB_init();
void RGB_setColor(uint32_t color);
uint32_t base_color();
void RGB_flash(uint32_t color);

#endif
