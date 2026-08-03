#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include "config.h"
#include "rtc_driver.h"
#include "shift_manager.h"

#define WIFI_SSID "Embedsol Technologies LLP"  // default, used only to seed NVS on first boot
#define WIFI_PASS "EmbedSolTech"

char wifi_ssid[32] = "";
char wifi_pass[64] = "";
Preferences wifi_prefs;

// One-time / on-demand WiFi+NTP sync. Connects, fetches time, writes it to
// the DS3231, then turns WiFi off - this device is offline the rest of the
// time and never needs to reconnect automatically.

void do_timesync()
{
    if (wifi_ssid[0] == '\0')
    {
        Serial.println("ERR - no WiFi credentials set. Use: wifi <ssid> <password>");
        return;
    }

    Serial.print("Connecting to WiFi: ");
    Serial.println(wifi_ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_pass);

    unsigned long start_ms = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start_ms) < WIFI_CONNECT_TIMEOUT_MS)
    {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("ERR - WiFi connection failed, RTC time unchanged");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
    }

    Serial.println("WiFi connected, fetching NTP time...");
    configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, NTP_SYNC_TIMEOUT_MS))
    {
        Serial.println("ERR - NTP sync failed, RTC time unchanged");
    }
    else
    {
        uint16_t year  = (uint16_t)(timeinfo.tm_year + 1900);
        uint8_t  month = (uint8_t)(timeinfo.tm_mon + 1);
        uint8_t  day   = (uint8_t)timeinfo.tm_mday;
        uint8_t  hour  = (uint8_t)timeinfo.tm_hour;
        uint8_t  minute= (uint8_t)timeinfo.tm_min;
        uint8_t  second= (uint8_t)timeinfo.tm_sec;

        if (!ds3231_set_time(year, month, day, hour, minute, second))
        {
            Serial.println("ERR - failed to write time to DS3231 (no response on I2C bus)");
        }
        else
        {
            uint8_t rb_day, rb_month, rb_hour, rb_minute;
            uint16_t rb_year;
            bool read_ok = ds3231_read_time(rb_day, rb_month, rb_year, rb_hour, rb_minute);
            bool matches = read_ok && rb_day == day && rb_month == month &&
                            rb_year == year && rb_hour == hour && rb_minute == minute;

            if (!matches)
            {
                Serial.println("ERR - RTC write could not be verified (read-back did not match). Try 'timesync' again.");
            }
            else
            {
                Serial.print("OK - RTC synced via NTP and verified: ");
                Serial.print(rb_year); Serial.print("-");
                Serial.print(rb_month); Serial.print("-");
                Serial.print(rb_day); Serial.print(" ");
                Serial.print(rb_hour); Serial.print(":");
                Serial.println(rb_minute);
                current_shift = compute_shift(rb_hour);
            }
        }
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi turned off (device stays offline for production)");
}

#endif
