#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include "config.h"
#include "rtc_driver.h"
#include "shift_manager.h"

#include "secrets.h" // WIFI_SSID_DEFAULT / WIFI_PASS_DEFAULT

// Defined in time_sync.cpp.
extern char wifi_ssid[32];
extern char wifi_pass[64];
extern Preferences wifi_prefs;

// Call once from setup(), before anything might call do_timesync(). Loads
// wifi_ssid/wifi_pass from NVS if they were previously set via the "wifi"
// console command; otherwise seeds NVS with WIFI_SSID_DEFAULT/
// WIFI_PASS_DEFAULT from secrets.h (one-time only - every boot after that
// reads back whatever's actually in NVS, ignoring the compiled-in default).
void wifi_creds_load();

// One-time / on-demand WiFi+NTP sync. Connects, fetches time, writes it to
// the DS3231, then turns WiFi off - this device is offline the rest of the
// time and never needs to reconnect automatically.
void do_timesync();

#endif
