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
#include "watchdog.h"
#include "web_portal.h"

void setup()
{
    Serial.begin(115200);
    Serial.println("Molding Machine : Traceability Labeling System");
    delay(1000);
    Serial.println("Initializing Peripherals...");
    RGB_init();
    usb_scannerInit();
    set_barcode_callback(onBarcodeScanned);
    delay(500); 
    rtc_init();
    printer_init();
    hmi_init();

    boot_millis_ref = millis();
    cfg_prefs.begin("cfg", false);
    load_config_from_nvs();
   // wifi_creds_load();
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
            current_shift = compute_shift(boot_hour, boot_minute);
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
        Serial.println("ERR - LittleFS mount failed (no 'spiffs'-labeled partition found, or it needs formatting).");
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

    // Fix: previously STICKER (shift count) was only ever written when an
    // inspection window closed, or on shift rollover - so after a reboot the
    // HMI showed whatever was left on screen (stale, or blank on a fresh
    // display) until the first window finished. Write the real value now,
    // immediately after shift_ok_total is resolved above (resumed from
    // checkpoint, reset for a new shift, or defaulted to 0 if LittleFS isn't
    // mounted) - covers every boot case, never blank, never waiting on a scan.
    hmi.Write_UString((uint16_t)STICKER, String(shift_ok_total));

    if (barcode_status)
    {
        Serial.println("Scanner Connected");
        RGB_setColor(green); // ready
    }
    else
    {
        Serial.println("Scanner disconnected");
        RGB_setColor(red); // not ready
    }
    set_scanner_fault(!barcode_status);
    printer_status_t boot_printer_status = PRINTER_STATUS_OTHER_ERROR;
    bool boot_printer_ok = poll_printer_status(boot_printer_status);
    bool boot_printer_fault = !(boot_printer_ok && boot_printer_status == PRINTER_STATUS_NORMAL);

    Serial.println(boot_printer_fault ? "Printer disconnected/faulty at boot" : "Printer Connected");

    // Seed printer_fault_active from this boot-time check instead of leaving
    // it at its default (false) until the first periodic poll in loop() gets
    // around to it - otherwise a printer that's already faulty at power-on
    // wouldn't be flagged for up to PRINTER_POLL_INTERVAL_MS after boot.
    set_printer_fault(boot_printer_fault, boot_printer_status, boot_printer_ok);
    last_printer_response_ok = boot_printer_ok;
    last_printer_status_valid = boot_printer_ok;
    if (boot_printer_ok)
    {
        last_logged_printer_status = boot_printer_status;
    }

    // Armed here, not earlier: LittleFS.begin(true) above can legitimately
    // take several seconds formatting on a first boot / corrupt filesystem,
    // which could exceed WATCHDOG_TIMEOUT_S and cause a spurious reset loop
    // if the watchdog were already running during that step.
    watchdog_init();

    // set_scanner_fault()/set_printer_fault() above already called
    // system_fault_recompute(), which latches system_state into SYS_ERROR
    // here if a fault was already present at boot - don't stomp that back
    // to WAIT_FOR_START.
    if (system_state != SYS_ERROR)
    {
        enter_state(SYS_WAIT_FOR_START);  // wait for first scan to synchronize
    }
}

void loop()
{
    // Must run first, every pass - see watchdog.h for why this must not be
    // moved inside any blocking wait.
    watchdog_feed();

    // Services the web portal: handles any pending HTTP request
    // (WebServer.h is synchronous - this must run every pass or the portal
    // won't respond to anything). No-op instantly if the portal isn't running.
    webportal_service();

    // Expire any transient HMI conditions (e.g. cavity-limit reject message)
    // whose display duration has elapsed, and repaint MESSAGE if needed.
    hmi_condition_service();

    // Advances the queued config-QR confirmation/error messages (see
    // config_manager.cpp) - separate from hmi_condition_service() above
    // because that only clears an expired slot, it doesn't know to load the
    // *next* queued message into the same slot.
    config_confirm_service();

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

    // Poll faster while a fault is active, so recovery (and the paused
    // window resuming) is noticed as soon as possible instead of waiting
    // out the full normal interval.
    //
    // Non-blocking: kick off a poll when the interval elapses, then let
    // bg_poll_printer_status_service() pick up the reply (or timeout) on a
    // later pass. This is the poll that runs every cycle, forever, so it's
    // the one that actually needed to stop spending up to
    // PRINTER_REPLY_TIMEOUT_MS blocking loop() on every single pass - see
    // watchdog.h / printer_manager.h comments for the reasoning. The
    // boot-time check, the console "printer status" command, and the
    // pre/post-print checks are unchanged and still block briefly - those
    // are one-off, user- or scan-triggered, not a per-pass background cost.
    unsigned long current_poll_interval = printer_fault_active ? PRINTER_FAULT_POLL_INTERVAL_MS : PRINTER_POLL_INTERVAL_MS;
    if (!printer_tx_busy && bg_poll_state == BG_POLL_IDLE && (millis() - last_printer_poll_ms) >= current_poll_interval)
    {
        last_printer_poll_ms = millis();
        bg_poll_printer_status_start();
    }

    printer_status_t printer_status = PRINTER_STATUS_OTHER_ERROR;
    bool poll_ok = false;
    if (bg_poll_printer_status_service(printer_status, poll_ok))
    {
        // Fault detection/HMI/window-pause stay on the fast internal poll
        // rate above - but Serial logging is decoupled from it: log on
        // change, otherwise repeat at most every PRINTER_LOG_HEARTBEAT_MS.
        // Without this, a real fault at the 150ms fault-poll rate floods
        // the same Serial port the command console reads from and buries
        // typed commands/responses.
        bool status_changed = poll_ok
            ? !(last_printer_response_ok && last_printer_status_valid && printer_status == last_logged_printer_status)
            : last_printer_response_ok; // true only if LAST poll succeeded - i.e. this failure is new, not a repeat
        bool heartbeat_due = (!poll_ok || printer_status != PRINTER_STATUS_NORMAL) &&
                             (millis() - last_printer_serial_log_ms) >= PRINTER_LOG_HEARTBEAT_MS;
        bool should_log = status_changed || heartbeat_due;

        if (poll_ok)
        {
            if (should_log)
            {
                Serial.print("Printer Status: 0x");
                Serial.print((uint8_t)printer_status, HEX);
                Serial.print(" -> ");
                Serial.println(printer_status_to_text(printer_status));
                last_printer_serial_log_ms = millis();
            }

            set_printer_fault(printer_status != PRINTER_STATUS_NORMAL, printer_status, true);

            last_printer_response_ok = true;
            last_printer_status_valid = true;
            last_logged_printer_status = printer_status;
        }
        else
        {
            if (should_log)
            {
                Serial.println("Printer Status: NO RESPONSE");
                last_printer_serial_log_ms = millis();
            }

            set_printer_fault(true, PRINTER_STATUS_OTHER_ERROR, false);

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
        }
        else
        {
            Serial.println("Scanner disconnected");
        }
        set_scanner_fault(!barcode_status);

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

    // INSPECTION_WINDOW -> CYCLE_TIME (fixed duration, no early close, no
    // extension - EXCEPT time spent paused for a printer or scanner fault
    // doesn't count: window_pause_accum_ms is subtracted from elapsed time,
    // and the close check is skipped entirely while still actively paused,
    // so an outage can't burn cavities into NG just because the clock kept
    // running while nothing could be scanned/printed. A cavity-limit early
    // close happens separately, immediately, in production_manager.h.)
    if (system_state == SYS_INSPECTION_WINDOW && !window_currently_paused &&
        (millis() - state_start_ms - window_pause_accum_ms) >= ((unsigned long)g_inspection_window_s * 1000UL))
    {
        close_inspection_window();
    }

    // CYCLE_TIME -> INSPECTION_WINDOW (automatic, no scan needed)
    if (system_state == SYS_CYCLE_TIME &&
        (millis() - state_start_ms) >= ((unsigned long)g_cycle_time_s * 1000UL))
    {
        enter_state(SYS_INSPECTION_WINDOW);

        // This transition happens on a timer, not a scan, so unlike the
        // scan-triggered WAIT_FOR_START->INSPECTION_WINDOW path (which is
        // always preceded by a fresh Gate #0/#2 printer check), a fault
        // could already be active right as this window opens. enter_state()
        // already calls window_pause_recompute() itself using whatever
        // window_paused_for_printer/window_paused_for_scanner currently are,
        // so no extra handling is needed here - this comment just documents
        // why that call exists inside enter_state() rather than duplicating
        // fault-check logic at every place a window can open.
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

            char shift = compute_shift(hour, minute);
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
