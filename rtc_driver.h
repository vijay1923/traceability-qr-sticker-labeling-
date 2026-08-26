#ifndef RTC_DRIVER_H
#define RTC_DRIVER_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// ---- DS3231 driver (raw I2C, no external RTC library) ----
void rtc_init();
uint8_t dec2bcd(uint8_t val); // decimal -> binary coded decimal
uint8_t bcd2dec(uint8_t val); // binary coded decimal -> decimal

// Reads the Oscillator Stop Flag (status reg 0x0F, bit7). If set, the
// oscillator stopped at some point (power loss / never set) and the
// current time cannot be trusted.
bool ds3231_is_time_valid();

bool ds3231_read_time(uint8_t &day, uint8_t &month, uint16_t &year, uint8_t &hour, uint8_t &minute);
bool ds3231_set_time(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);

#endif
