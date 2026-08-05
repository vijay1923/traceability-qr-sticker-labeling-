#include <usb_scanner_Lib.h>
#include "config.h"
#include "diagnostics.h"
#include "rgb_status.h"
#include "state_machine.h"
#include "rtc_driver.h"
#include "storage_manager.h"
#include "config_manager.h"
#include "shift_manager.h"
#include "time_sync.h"
#include "printer_manager.h"
#include "scanner_manager.h"
#include "qr_generator.h"
#include "production_manager.h"
#include "command_console.h"
#include "hmi.h"

void setup()
{
    Serial.begin(115200);
    Serial.println("Molding Machine : Traceability Labeling System");
    delay(1000);
    Serial.println("Initializing Peripherals...");
    RGB_init();
    usb_scannerInit();
    set_barcode_callback(onBarcodeScanned);
    rtc_init();
    printer_init();
    hmi_init();

    boot_millis_ref = millis();
    cfg_prefs.begin("cfg", false);
    load_config_from_nvs();
    bool rtc_valid_at_boot = false;
    uint8_t boot_day = 0, boot_month = 0, boot_hour = 0, boot_minute = 0;
    uint16_t boot_year = 0;

    if (ds3231_is_time_valid())
    {
        if (ds3231_read_time(boot_day, boot_month, boot_year, boot_hour, boot_minute))
        {
            rtc_valid_at_boot = true;
            Serial.print("RTC OK - current time: ");
            Serial.print(boot_year); Serial.print("-");
            Serial.print(boot_month); Serial.print("-");
            Serial.print(boot_day); Serial.print(" ");
            Serial.print(boot_hour); Serial.print(":");
            Serial.println(boot_minute);
            current_shift = compute_shift(boot_hour);
        }
        hmi.Time_Stamp(boot_day, boot_month, (uint8_t)(boot_year % 100), boot_hour, boot_minute, 0);
    }
    else
    {
        Serial.println("RTC time NOT SET / lost power - printing blocked until set.");
        Serial.println("Use: settime YYYY MM DD HH MM SS   OR   wifi <ssid> <pass> then timesync");
       

    }
    littlefs_mounted = LittleFS.begin(true); // true = format on mount failure
    if (!littlefs_mounted)
    {
        // LittleFS.begin() mounts the partition labeled "spiffs" by default
        // (a historical quirk of the ESP32 Arduino core - LittleFS reuses
        // that label rather than requiring a partition literally named
        // "littlefs"). This almost always means the board's partition table
        // has no partition with that label - a Tools > Partition Scheme
        // setting in the Arduino IDE (or a custom partitions.csv), not a
        // code bug. Firmware continues running with stats/checkpoint
        // persistence disabled rather than getting stuck.
        Serial.println("ERR - LittleFShmi_write_text mount failed (no 'spiffs'-labeled partition found, or it needs formatting).");
        Serial.println("      Fix: Arduino IDE -> Tools -> Partition Scheme -> pick a scheme with SPIFFS/data space, then re-upload.");
        Serial.println("      Continuing WITHOUT persistent shift stats until this is fixed.");
    }
    else
    {
        Serial.println("LittleFS mounted OK");
        if (!LittleFS.exists(STATS_DIR))
        {
            LittleFS.mkdir(STATS_DIR);
        }

        // Ensure the current RTC month file exists immediately at boot
        // (example: /stats/0826.bat), even before first shift rollover write.
        if (rtc_valid_at_boot)
        {
            sync_month_stats_file(boot_month, boot_year);
        }

        // Attempt to recover shift-in-progress stats from the last
        // checkpoint instead of silently starting every counter at 0.
        uint8_t ck_day, ck_month;  
        uint16_t ck_year;
        char ck_shift;
        uint32_t ck_ok, ck_ng;
        uint16_t ck_counter;

        if (read_checkpoint(ck_day, ck_month, ck_year, ck_shift, ck_ok, ck_ng, ck_counter))
        {
            if (rtc_valid_at_boot && ck_shift == current_shift)
            {
                // Same shift as before the reset/power loss - resume exactly
                // where we left off.
                shift_ok_total = ck_ok;
                shift_ng_total = ck_ng;
                shift_counter = ck_counter;
                shift_rec_day = ck_day;
                shift_rec_month = ck_month;
                shift_rec_year = ck_year;
                Serial.print("RESUMED shift stats from checkpoint: OK=");
                Serial.print(ck_ok);
                Serial.print(" NG=");
                Serial.print(ck_ng);
                Serial.print(" counter=");
                Serial.println(ck_counter);
            }
            else
            {
                // The shift that was running before power loss has already
                // ended (or RTC isn't trustworthy) - finalize it as a
                // completed record instead of quietly discarding it, then
                // start fresh totals.
                Serial.println("Checkpoint belongs to a shift that already ended - finalizing it now");
                write_shift_stats(ck_day, ck_month, ck_year, ck_shift, ck_ok, ck_ng);
                shift_ok_total = 0;
                shift_ng_total = 0;
                shift_counter = 0;
                if (rtc_valid_at_boot)
                {
                    shift_rec_day = boot_day;
                    shift_rec_month = boot_month;
                    shift_rec_year = boot_year;
                }
            }
        }
        else if (rtc_valid_at_boot)
        {
            // No checkpoint at all (first-ever boot, or file missing) -
            // anchor the shift record start date to right now.
            shift_rec_day = boot_day;
            shift_rec_month = boot_month;
            shift_rec_year = boot_year;
        }
    }

    if (barcode_status)
    {
        Serial.println("Scanner Connected");
        RGB_setColor(green); // ready
        hmi.Write_UString((uint16_t)SCANNER_STATUS, String("Connected"));
    }
    else
    {
        Serial.println("Scanner disconnected");
        RGB_setColor(red); // not ready
        hmi.Write_UString((uint16_t)SCANNER_STATUS, String("Disconnected"));
    }
    printer_status_t boot_printer_status = PRINTER_STATUS_OTHER_ERROR;
    bool boot_printer_ok = poll_printer_status(boot_printer_status);
    set_printer_fault_state(boot_printer_ok, boot_printer_status); // seed fault cache from boot poll, don't leave it at default

    if (boot_printer_ok && boot_printer_status == PRINTER_STATUS_NORMAL)
    {
        Serial.println("Printer Connected");
        hmi.Write_UString((uint16_t)PRINTER_STATUS, String("Connected"));    
    }
    else
    {
        Serial.println("Printer disconnected");
        hmi.Write_UString((uint16_t)PRINTER_STATUS, String("Disconnected"));
    }
    enter_state(SYS_WAIT_FOR_START);  // wait for first scan to synchronize
}

void loop()
{
    // Engineer commands over Serial - see command_console.h / "help" for full list.
    // Robust against any terminal line-ending setting: normally triggers on
    // '\r' or '\n', but if neither ever arrives, an idle timeout (200ms of
    // silence after the last received character) auto-completes the line.
    {
        static char line_buf[80];
        static uint8_t line_len = 0;
        static unsigned long last_char_ms = 0;

        while (Serial.available())
        {
            char c = Serial.read();
            last_char_ms = millis();

            if (c == '\n' || c == '\r')
            {
                process_serial_line(line_buf, line_len);
                line_len = 0;
            }
            else if (line_len < (sizeof(line_buf) - 1))
            {
                line_buf[line_len++] = c;
            }
        }

        // Idle-timeout fallback: no terminator ever arrived, but nothing new
        // has come in for 200ms - treat what we have as a complete command.
        if (line_len > 0 && (millis() - last_char_ms) > 200)
        {
            process_serial_line(line_buf, line_len);
            line_len = 0;
        }
    }

    unsigned long current_poll_interval = printer_fault_active ? PRINTER_FAULT_POLL_INTERVAL_MS : PRINTER_POLL_INTERVAL_MS;
    if (!printer_tx_busy && (millis() - last_printer_poll_ms) >= current_poll_interval)
    {
        last_printer_poll_ms = millis();

        printer_status_t printer_status = PRINTER_STATUS_OTHER_ERROR;
        bool poll_ok = poll_printer_status(printer_status);
        set_printer_fault_state(poll_ok, printer_status); // keep fault cache fresh every poll

        bool should_log = false;
        if (poll_ok)
        {
            if (!last_printer_response_ok ||
                !last_printer_status_valid ||
                printer_status != last_logged_printer_status)
            {
                should_log = true;
            }

            if (should_log)
            {
                Serial.print("Printer Status: 0x");
                Serial.print((uint8_t)printer_status, HEX);
                Serial.print(" -> ");
                Serial.println(printer_status_to_text(printer_status));
                hmi.Write_UString((uint16_t)PRINTER_STATUS, String("Connected"));
            }

            last_printer_response_ok = true;
            last_printer_status_valid = true;
            last_logged_printer_status = printer_status;
        }
        else
        {
            if (last_printer_response_ok)
            {
                should_log = true;
            }

            if (should_log)
            {
                Serial.println("Printer Status: NO RESPONSE");
                hmi.Write_UString((uint16_t)PRINTER_STATUS, String("Disconnected"));
            }

            last_printer_response_ok = false;
            last_printer_status_valid = false;
        }

        if (verbose_logging && littlefs_mounted)
        {
            Serial.print("  [verbose] LittleFS free: ");
            Serial.print(littlefs_free_bytes());
            Serial.println(" bytes");
        }
    }

    if (barcode_status != lastConnectedState)
    {
        if (barcode_status)
        {
            Serial.println("Scanner Connected");
            hmi.Write_UString((uint16_t)SCANNER_STATUS, String("Connected"));
        }
        else
        {
            Serial.println("Scanner disconnected");
            hmi.Write_UString((uint16_t)SCANNER_STATUS, String("Disconnected"));
        }

        lastConnectedState = barcode_status;
        if (!flashing)
        {
            RGB_setColor(base_color());
        }
    }

    if (flashing && (millis() - flash_start_ms >= FLASH_DURATION_MS))
    {
        flashing = false;
        RGB_setColor(base_color()); // ready
    }

    // INSPECTION_WINDOW -> CYCLE_TIME (fixed duration, no early close, no extension)
    if (system_state == SYS_INSPECTION_WINDOW &&
        (millis() - state_start_ms) >= ((unsigned long)g_inspection_window_s * 1000UL))
    {
        uint8_t ok = printed_count;
        uint8_t ng = (g_cavity_count > printed_count) ? (g_cavity_count - printed_count) : 0;

        shift_ok_total += ok;
        shift_ng_total += ng;
        hmi.Write_Number((uint16_t)STICKER, (uint16_t)shift_ok_total);
        Serial.print("Shift Totals -> OK: ");
        Serial.println(shift_ok_total);
    
#if ENABLE_OK_NG_SUMMARY
        Serial.print("Window Summary -> OK: ");
        Serial.print(ok);
        Serial.print(" NG: ");
        Serial.print(ng);
        Serial.print(" (Cavity: ");
        Serial.print(g_cavity_count);
        Serial.println(")");
#endif

        // Checkpoint the running shift totals to flash right after folding
        // this window's counts in, so a power loss any time before the next
        // window close only costs this one window, not the shift.
        write_checkpoint();

        // Apply any shift-boundary rollover that was deferred while this
        // window was open, now that its counts are safely folded in.
        if (shift_rollover_pending)
        {
            apply_shift_rollover(pending_shift, pending_day, pending_month, pending_year);
            shift_rollover_pending = false;
        }

        enter_state(SYS_CYCLE_TIME);
    }

    // CYCLE_TIME -> INSPECTION_WINDOW (automatic, no scan needed)
    if (system_state == SYS_CYCLE_TIME &&
        (millis() - state_start_ms) >= ((unsigned long)g_cycle_time_s * 1000UL))
    {
        enter_state(SYS_INSPECTION_WINDOW);
    }

    // Periodic, wall-clock-driven shift rollover check - independent of
    // scanning, so a boundary is never missed just because nothing was
    // scanned right around it.
    if ((millis() - last_shift_check_ms) >= SHIFT_CHECK_INTERVAL_MS)
    {
        last_shift_check_ms = millis();

        uint8_t day, month, hour, minute;
        uint16_t year;
        if (ds3231_read_time(day, month, year, hour, minute))
        {
            // Keep monthly file pre-created as month/year changes.
            sync_month_stats_file(month, year);

            char shift = compute_shift(hour);
            check_shift_rollover(shift, day, month, year);
        }
    }

    static uint8_t last_cavity_sent = 0xFF;
    static uint32_t last_sticker_sent = 0xFFFFFFFFUL;

    if (last_cavity_sent != g_cavity_count)
    {
       hmi.Write_UString((uint16_t)MOLD_CAVITY, String(g_cavity_count));
        last_cavity_sent = g_cavity_count;
        Serial.print("Cavity Count -> ");
        Serial.println(g_cavity_count);
    }


}
