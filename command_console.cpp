#include <Arduino.h>
#include "command_console.h"

void process_serial_line(char *raw_line, uint8_t raw_len)
{
    raw_line[raw_len] = '\0';

    // Trim any trailing \r or \n left over regardless of how we got called
    while (raw_len > 0 && (raw_line[raw_len - 1] == '\r' || raw_line[raw_len - 1] == '\n'))
    {
        raw_line[--raw_len] = '\0';
    }

    if (raw_line[0] == '\0')
        return; // nothing to do

    int y, mo, d, h, mi, s;
    char ssid_buf[32];
    char pass_buf[64];

    if (sscanf(raw_line, "settime %d %d %d %d %d %d", &y, &mo, &d, &h, &mi, &s) == 6)
    {
        if (!ds3231_set_time((uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)h, (uint8_t)mi, (uint8_t)s))
        {
            Serial.println("ERR - Failed to write time to RTC (no response on I2C bus).");
            Serial.println("Please try again: settime YYYY MM DD HH MM SS");
        }
        else
        {
            // Read it back and verify it actually landed correctly - an
            // acknowledged I2C write is not proof the data is correct.
            uint8_t rb_day, rb_month, rb_hour, rb_minute;
            uint16_t rb_year;

            bool read_ok = ds3231_read_time(rb_day, rb_month, rb_year, rb_hour, rb_minute);
            bool matches = read_ok &&
                            rb_day == (uint8_t)d &&
                            rb_month == (uint8_t)mo &&
                            rb_year == (uint16_t)y &&
                            rb_hour == (uint8_t)h &&
                            rb_minute == (uint8_t)mi;

            if (!matches)
            {
                Serial.println("ERR - RTC write could not be verified (read-back did not match).");
                Serial.println("Data may not be written correctly. Please try again: settime YYYY MM DD HH MM SS");
            }
            else
            {
                current_shift = compute_shift((uint8_t)h);
                Serial.print("OK - RTC time set and verified: ");
                Serial.print(rb_year); Serial.print("-");
                Serial.print(rb_month); Serial.print("-");
                Serial.print(rb_day); Serial.print(" ");
                Serial.print(rb_hour); Serial.print(":");
                Serial.println(rb_minute);
            }
        }
    }
    else if (sscanf(raw_line, "wifi %31s %63s", ssid_buf, pass_buf) == 2)
    {
        strncpy(wifi_ssid, ssid_buf, sizeof(wifi_ssid) - 1);
        wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
        strncpy(wifi_pass, pass_buf, sizeof(wifi_pass) - 1);
        wifi_pass[sizeof(wifi_pass) - 1] = '\0';
        wifi_prefs.putString("ssid", wifi_ssid);
        wifi_prefs.putString("pass", wifi_pass);
        Serial.println("OK - WiFi credentials stored in NVS (not connected yet)");
    }
    else if (strcmp(raw_line, "timesync") == 0)
    {
        do_timesync();
    }
    else if (strcmp(raw_line, "listfiles") == 0)
    {
        cmd_listfiles();
    }
    else if (strncmp(raw_line, "getfile ", 8) == 0)
    {
        const char *filename = raw_line + 8;
        cmd_getfile(filename);
    }
    else if (strcmp(raw_line, "stats") == 0)
    {
        // Quick visibility into the in-RAM/checkpointed running totals
        // without needing to pull a file, since these can't be seen otherwise
        // until the shift actually rolls over.
        Serial.print("CURRENT SHIFT STATS -> shift: ");
        Serial.print(current_shift == '\0' ? '?' : current_shift);
        Serial.print(" started: ");
        Serial.print(shift_rec_day); Serial.print("/");
        Serial.print(shift_rec_month); Serial.print("/");
        Serial.print(shift_rec_year);
        Serial.print(" OK: "); Serial.print(shift_ok_total);
        Serial.print(" NG: "); Serial.print(shift_ng_total);
        Serial.print(" counter: "); Serial.println(shift_counter);
    }
    else if (strcmp(raw_line, "config") == 0)
    {
        print_config();
    }
    else if (strncmp(raw_line, "setconfig ", 10) == 0)
    {
        char key_buf[16];
        long value;
        if (sscanf(raw_line + 10, "%15s %ld", key_buf, &value) == 2)
        {
            if (set_config_value(key_buf, value))
            {
                Serial.print("OK - ");
                Serial.print(key_buf);
                Serial.print(" set to ");
                Serial.println(value);

                if (strcmp(key_buf, "cavity") == 0)
                {
                    hmi.Write_UString((uint16_t)MOLD_CAVITY, String(g_cavity_count));
                }
            }
            else
            {
                Serial.print("ERR - invalid key or out-of-range value: ");
                Serial.println(raw_line + 10);
            }
        }
        else
        {
            Serial.println("ERR - usage: setconfig <key> <value>");
        }
    }
    else if (strcmp(raw_line, "resetconfig") == 0)
    {
        reset_config_to_defaults();
        hmi.Write_UString((uint16_t)MOLD_CAVITY, String(g_cavity_count));
    }
    else if (strcmp(raw_line, "webportal start") == 0)
    {
        webportal_start();
    }
    else if (strcmp(raw_line, "webportal stop") == 0)
    {
        webportal_stop();
    }
    else if (strcmp(raw_line, "webportal status") == 0)
    {
        Serial.println(webportal_is_running() ? "Web portal: RUNNING" : "Web portal: stopped");
    }
    else if (strcmp(raw_line, "reboot") == 0)
    {
        Serial.println("Rebooting...");
        delay(200); // let the message actually get out over serial before the UART goes down mid-restart
        ESP.restart();
    }
    else if (strcmp(raw_line, "version") == 0)
    {
        Serial.print("Firmware version: ");
        Serial.println(FW_VERSION);
        Serial.print("Build: ");
        Serial.println(FW_BUILD_INFO);
    }
    else if (strcmp(raw_line, "resetdata") == 0)
    {
        Serial.println("WARNING - this permanently deletes ALL stored production data (stats + checkpoint).");
        Serial.println("This does NOT happen automatically on firmware updates and must be run deliberately.");
        Serial.println("If you're sure, type: resetdata CONFIRM");
    }
    else if (strcmp(raw_line, "resetdata CONFIRM") == 0)
    {
        reset_all_stats_data();
    }
    else if (strcmp(raw_line, "verbose on") == 0)
    {
        verbose_logging = true;
        Serial.println("OK - verbose logging ON");
    }
    else if (strcmp(raw_line, "verbose off") == 0)
    {
        verbose_logging = false;
        Serial.println("OK - verbose logging OFF");
    }
    else if (strcmp(raw_line, "uptime") == 0)
    {
        unsigned long secs = (millis() - boot_millis_ref) / 1000UL;
        unsigned long hrs  = secs / 3600UL;
        unsigned long mins = (secs % 3600UL) / 60UL;
        unsigned long rem_secs = secs % 60UL;
        Serial.print("Uptime: ");
        Serial.print(hrs); Serial.print("h ");
        Serial.print(mins); Serial.print("m ");
        Serial.print(rem_secs); Serial.println("s");
    }
    else if (strncmp(raw_line, "hmitext ", 8) == 0)
    {
        unsigned int vp;
        int consumed = 0;
        if (sscanf(raw_line + 8, "%x %n", &vp, &consumed) == 1 && consumed > 0)
        {
            const char *text = raw_line + 8 + consumed;
            hmi.Write_UString((uint16_t)vp, String(text));
            Serial.print("OK - wrote text to VP 0x");
            Serial.print(vp, HEX);
            Serial.print(": ");
            Serial.println(text);
        }
        else
        {
            Serial.println("ERR - usage: hmitext <vp_hex> <text>   e.g. hmitext 5000 HELLO");
        }
    }
    else if (strncmp(raw_line, "hmiwrite ", 9) == 0)
    {
        unsigned int vp;
        int value;
        if (sscanf(raw_line + 9, "%x %d", &vp, &value) == 2)
        {
            hmi.Write_UString((uint16_t)vp, String(value));
            Serial.print("Vp : "); 
            Serial.println(int(vp));
            Serial.print("OK - wrote number to VP 0x");
            Serial.print(vp, HEX);
            Serial.print(": ");
            Serial.println(value);
        }
        else
        {
            Serial.println("ERR - usage: hmiwrite <vp_hex> <number>   e.g. hmiwrite 5500 1234");
        }
    }
    else if (strncmp(raw_line, "print ", 6) == 0)
    {
        const char *data = raw_line + 6;
        while (*data == ' ' || *data == '\t')
        {
            data++;
        }

        if (*data == '\0')
        {
            Serial.println("ERR - usage: print <data>   e.g. print 123");
        }
        else
        {
            printer_status_t pre_status = PRINTER_STATUS_OTHER_ERROR;
            bool pre_ok = poll_printer_status(pre_status);

            if (!pre_ok || pre_status != PRINTER_STATUS_NORMAL)
            {
                Serial.print("ERR - printer not ready before manual print. Status: ");
                Serial.println(pre_ok ? printer_status_to_text(pre_status) : "NO RESPONSE");
                set_printer_fault(true, pre_status, pre_ok);
            }
            else
            {
                if (printer_fault_active)
                {
                    set_printer_fault(false, pre_status, pre_ok);
                }

                hmi.Write_UString((uint16_t)QR_CODE, String(data));
                send_to_printer(data);
                Serial.print("OK - manual QR print sent: ");
                Serial.println(data);
            }
        }
    }
    else if (strcmp(raw_line, "hmihelp") == 0)
    {
        hmi_test_help();
    }
    else if (strcmp(raw_line, "help") == 0)
    {
        Serial.println("Available commands:");
        Serial.println("  settime YYYY MM DD HH MM SS   -> manual RTC set, no WiFi needed");
        Serial.println("  wifi <ssid> <password>        -> store WiFi credentials in NVS (does not connect)");
        Serial.println("  timesync                      -> connect, fetch NTP, write RTC, then WiFi off");
        Serial.println("  webportal start|stop|status   -> web dashboard over a WiFi hotspot (fixed SSID/password)");
        Serial.println("  reboot                        -> restart the device");
        Serial.println("  listfiles                     -> list /stats files (MMYY.csv monthly stats + current.ckp checkpoint)");
        Serial.println("  getfile <filename>            -> retrieve a file in /stats (example: getfile 0826.csv)");
        Serial.println("  stats                         -> show current (not-yet-finalized) shift totals");
        Serial.println("  config                        -> show current runtime configuration");
        Serial.println("  setconfig <key> <value>       -> change + persist one config value (keys: cycletime, inspwindow, cavity, shift_a, shift_b)");
        Serial.println("  resetconfig                   -> reset config to compiled-in defaults");
        Serial.println("  version                       -> show firmware version and build date");
        Serial.println("  resetdata                     -> shows warning; requires 'resetdata CONFIRM' to actually wipe /stats");
        Serial.println("  hmitext <vp_hex> <text>       -> write text to a VP address (calibration: identify which VP is which)");
        Serial.println("  hmiwrite <vp_hex> <number>    -> write a number to a VP address (calibration)");
        Serial.println("  print <data>                  -> generate and send a QR label for manual data");
        Serial.println("  hmihelp                       -> show HMI-focused help");
        Serial.println("  verbose on|off                -> toggle extra diagnostic logging");
        Serial.println("  uptime                        -> show time since boot");
        Serial.println("  help                          -> display this help message");
    }
    else
    {
        Serial.print("ERR - unknown command: ");
        Serial.println(raw_line);
    }
}
