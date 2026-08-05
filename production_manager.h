#ifndef PRODUCTION_MANAGER_H
#define PRODUCTION_MANAGER_H

#include "config.h"
#include "config_manager.h"
#include "diagnostics.h"
#include "state_machine.h"
#include "shift_manager.h"
#include "scanner_manager.h"
#include "qr_generator.h"
#include "printer_manager.h"
#include "rgb_status.h"

// Defensive guard against a reentrant call from the scanner library's
// callback (e.g. fired again before the previous call finishes). Cheap
// regardless of whether the library can actually do this - if it can't,
// this never triggers; if it can, shared globals (barcode_data,
// extracted_data, final_qr) never get touched concurrently.
bool scan_processing = false;

void onBarcodeScanned(const char *qrcode, int length)
{
    if (scan_processing)
    {
        Serial.println("REJECTED - reentrant scan callback ignored (already processing a scan)");
        return;
    }
    scan_processing = true;

    Serial.print("Scanned QR : ");
    Serial.println(qrcode);

    if (verbose_logging)
    {
        Serial.print("  [verbose] raw length: ");
        Serial.print(length);
        Serial.print(", system_state: ");
        Serial.println((int)system_state);
    }

    // Gate #1: system busy in CYCLE_TIME -> reject immediately, don't process further
    if (system_state == SYS_CYCLE_TIME)
    {
        Serial.print("REJECTED - scan received during CYCLE_TIME (busy): ");
        Serial.println(qrcode);
        RGB_flash(blue); // rejected: system busy
        scan_processing = false;
        return;
    }

    // Gate #0: known printer fault (cached, no live poll) - fail fast.
    // Gate #2 below still always does its own fresh poll immediately before
    // printing regardless of this cache, so a fault that appears between
    // polls is still caught there. This cache can be stale-pessimistic (a
    // fault that actually cleared moments ago) - accepted tradeoff, biased
    // toward safety, self-heals within one poll cycle (<=500ms, or 200ms
    // while a fault is active).
    if (printer_fault_active)
    {
        Serial.print("REJECTED - printer fault active (cached, no poll): ");
        Serial.println(printer_status_to_text(printer_fault_status));
        RGB_flash(cyan);
        scan_processing = false;
        return;
    }

    strncpy(barcode_data, qrcode, sizeof(barcode_data) - 1);
    barcode_data[sizeof(barcode_data) - 1] = '\0';

    if (!validate_raw_format(barcode_data))
    {
        Serial.print("REJECTED - invalid format: ");
        Serial.println(barcode_data);
        RGB_flash(yellow); // rejected: wrong format
        scan_processing = false;
        return;
    }

    data_extract(barcode_data);

    if (extracted_data[0] == '\0')
    {
        Serial.println("Extraction FAILED (empty result)");
        RGB_flash(yellow);
        scan_processing = false;
        return;
    }

    Serial.print("Extracted Data : ");
    Serial.println(extracted_data);
    hmi.Write_UString((uint16_t)QR_CODE, String(extracted_data));

    if (!generate_traceability_qr(extracted_data))
    {
        Serial.println("REJECTED - RTC not available/reliable, cannot generate trustworthy label");
        RGB_flash(white); // rejected: RTC not ready
        scan_processing = false;
        return;
    }

    if (!validate_final_qr(final_qr))
    {
        Serial.print("REJECTED - generated QR failed validation: ");
        Serial.println(final_qr);
       // hmi.Write_UString((uint16_t)QR_CODE, String(final_qr));
        RGB_flash(yellow);
        scan_processing = false;
        return;
    }

    // Gate #2: printer must report NORMAL before we commit to printing.
    // Always a fresh, live poll - this is the real safety authority, Gate #0
    // above is purely a fast-path optimization on top of it.
    printer_status_t pre_status;
    bool pre_ok = poll_printer_status(pre_status);
    set_printer_fault_state(pre_ok, pre_status); // keep cache in sync with this live result too

    if (!pre_ok || pre_status != PRINTER_STATUS_NORMAL)
    {
        Serial.print("REJECTED - printer not ready before print. Status: ");
        Serial.println(pre_ok ? printer_status_to_text(pre_status) : "NO RESPONSE");
        RGB_flash(cyan); // rejected: printer not ready
        scan_processing = false;
        return;
    }

    // If this is the very first scan since boot/last cycle, it both opens the
    // window AND is the first print inside it - open the window BEFORE
    // committing the count, so the reset-to-0 doesn't wipe this print out.
    if (system_state == SYS_WAIT_FOR_START)
    {
        enter_state(SYS_INSPECTION_WINDOW);
    }

    shift_counter++;
    printed_count++;
    hmi.Write_UString((uint16_t)PRINT_COUNTER, String(printed_count));
    hmi.Write_UString((uint16_t)STICKER, String(shift_ok_total + printed_count));
    Serial.print("Final QR : ");
    Serial.println(final_qr);

    send_to_printer(final_qr);

    // PRINT_COUNTER updates on every successful print (event-driven).
    // QR_CODE intentionally NOT touched here - it always shows only the
    // extracted data from the scan, never the full generated/final QR.
    // hmi.Write_UString((uint16_t)PRINT_COUNTER, String(printed_count));

    // Gate #2 (post-check): confirm printer is still healthy after the job.
    // We can't undo a job already sent - this is diagnostic, not a reject.
    // Also feeds the fault cache + HMI, since this is a fresh live read too.
    printer_status_t post_status;
    bool post_ok = poll_printer_status(post_status);
    set_printer_fault_state(post_ok, post_status);

    if (post_ok)
    {
        Serial.print("Printer Status After Print: ");
        Serial.println(printer_status_to_text(post_status));
        if (post_status != PRINTER_STATUS_NORMAL)
        {
            Serial.println("WARNING - printer reported non-normal status after print, label may be affected");
        }
    }
    else
    {
        Serial.println("Printer Status After Print: NO RESPONSE");
    }

    RGB_flash(magenta); // success
    scan_processing = false;
}

#endif