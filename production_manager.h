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

void onBarcodeScanned(const char *qrcode, int length)
{
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
        return;
    }

    strncpy(barcode_data, qrcode, sizeof(barcode_data) - 1);
    barcode_data[sizeof(barcode_data) - 1] = '\0';

    if (!validate_raw_format(barcode_data))
    {
        Serial.print("REJECTED - invalid format: ");
        Serial.println(barcode_data);
        RGB_flash(yellow); // rejected: wrong format
        return;
    }

    data_extract(barcode_data);

    if (extracted_data[0] == '\0')
    {
        Serial.println("Extraction FAILED (empty result)");
        RGB_flash(yellow);
        return;
    }

    Serial.print("Extracted Data : ");
    Serial.println(extracted_data);
    hmi.Write_UString((uint16_t)QR_CODE, String(extracted_data));

    if (!generate_traceability_qr(extracted_data))
    {
        Serial.println("REJECTED - RTC not available/reliable, cannot generate trustworthy label");
        RGB_flash(white); // rejected: RTC not ready
        return;
    }

    if (!validate_final_qr(final_qr))
    {
        Serial.print("REJECTED - generated QR failed validation: ");
        Serial.println(final_qr);
       // hmi.Write_UString((uint16_t)QR_CODE, String(final_qr));
        RGB_flash(yellow);
        return;
    }

    // Gate #2: printer must report NORMAL before we commit to printing
    printer_status_t pre_status;
    bool pre_ok = poll_printer_status(pre_status);

    if (!pre_ok || pre_status != PRINTER_STATUS_NORMAL)
    {
        Serial.print("REJECTED - printer not ready before print. Status: ");
        Serial.println(pre_ok ? printer_status_to_text(pre_status) : "NO RESPONSE");
        RGB_flash(cyan); // rejected: printer not ready
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
    printer_status_t post_status;
    if (poll_printer_status(post_status))
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
}

#endif