#include <Arduino.h>
#include "rtc_driver.h"

void rtc_init()
{
    Wire.begin(SDA_PIN, SCL_PIN); // initialize I2C for DS3231 RTC
    Serial.println("RTC Initialized");
}

uint8_t dec2bcd(uint8_t val)
{
    return (uint8_t)(((val / 10) << 4) | (val % 10));
}

uint8_t bcd2dec(uint8_t val)
{
    return (uint8_t)(((val >> 4) * 10) + (val & 0x0F));
}

bool ds3231_is_time_valid()
{
    Wire.beginTransmission(DS3231_ADDRESS);
    Wire.write(0x0F);
    if (Wire.endTransmission(false) != 0)
        return false; // I2C comm failure -> treat as not valid

    if (Wire.requestFrom(DS3231_ADDRESS, 1) != 1)
        return false;

    uint8_t status = Wire.read();
    return (status & 0x80) == 0; // OSF=0 -> valid
}

bool ds3231_read_time(uint8_t &day, uint8_t &month, uint16_t &year, uint8_t &hour, uint8_t &minute)
{
    if (!ds3231_is_time_valid())
    return false;

    Wire.beginTransmission(DS3231_ADDRESS);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0)
        return false;

    if (Wire.requestFrom(DS3231_ADDRESS, 7) != 7)
        return false;

    (void)Wire.read();              // seconds - unused for our label format
    uint8_t min_raw   = Wire.read();
    uint8_t hour_raw  = Wire.read();
    (void)Wire.read();              // day-of-week - unused
    uint8_t day_raw   = Wire.read();
    uint8_t month_raw = Wire.read();
    uint8_t year_raw  = Wire.read();

    minute = bcd2dec(min_raw);
    hour   = bcd2dec(hour_raw & 0x3F);   // mask off 12/24hr mode bits
    day    = bcd2dec(day_raw);
    month  = bcd2dec(month_raw & 0x7F);  // mask off century bit
    year   = 2000 + bcd2dec(year_raw);

    return true;
}

bool ds3231_set_time(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    Wire.beginTransmission(DS3231_ADDRESS);
    Wire.write(0x00); // start at seconds register
    Wire.write(dec2bcd(second));
    Wire.write(dec2bcd(minute));
    Wire.write(dec2bcd(hour));     // 24hr mode (bit6=0, safe for hour 0-23)
    Wire.write(dec2bcd(1));        // day-of-week, unused, arbitrary value
    Wire.write(dec2bcd(day));
    Wire.write(dec2bcd(month));    // bit7 century flag left 0
    Wire.write(dec2bcd((uint8_t)(year % 100)));
    if (Wire.endTransmission() != 0)
        return false;

    // Clear the Oscillator Stop Flag now that a valid time has been written
    Wire.beginTransmission(DS3231_ADDRESS);
    Wire.write(0x0F);
    Wire.endTransmission(false);
    if (Wire.requestFrom(DS3231_ADDRESS, 1) == 1)
    {
        uint8_t status = Wire.read();
        status &= ~0x80;
        Wire.beginTransmission(DS3231_ADDRESS);
        Wire.write(0x0F);
        Wire.write(status);
        Wire.endTransmission();
    }

    return true;
}
