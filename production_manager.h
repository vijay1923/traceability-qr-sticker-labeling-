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

// tracks the raw-material batch field (first segment) of the last accepted scan
static char last_batch_id[20] = "";

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

    strncpy(barcode_data, qrcode, sizeof(barcode_data) - 1);
    barcode_data[sizeof(barcode_data) - 1] = '\0';

    // Config-update barcode: CFG:cycletime=<min>[,cavity=<n>][,inspwindow=<min>]
    // Checked FIRST, before any printer/production-state gates - a config
    // change doesn't touch the printer or consume a cavity slot, so there's
    // no reason a printer fault or a busy CYCLE_TIME should block it. This
    // also means it's the one scan type that always works, useful if the
    // printer fault itself needs a config correction to resolve.
    if (strncmp(barcode_data, "CFG:", 4) == 0)
    {
        parse_config_barcode(barcode_data + 4);
        RGB_flash(white);
        return;
    }

    // Gate #0: printer already known-faulty from the periodic background poll
    // -> reject immediately, don't even start processing this scan. This is
    // the fast path; Gate #2 below re-polls right before printing as a
    // freshness check in case the fault cleared or appeared since the last
    // periodic poll.
    if (printer_fault_active)
    {
        Serial.print("REJECTED - printer fault active: ");
        Serial.println(printer_status_to_text(printer_fault_status));
       // hmi_set_message(String("Printer Fault: ") + printer_status_to_text(printer_fault_status), MSG_SEV_FAULT);
        RGB_flash(cyan); // rejected: printer not ready
        return;
    }

    // Gate #1: system busy in CYCLE_TIME -> reject immediately, don't process further
    if (system_state == SYS_CYCLE_TIME)
    {
        Serial.print("REJECTED - scan received during CYCLE_TIME (busy): ");
        Serial.println(qrcode);
        RGB_flash(blue); // rejected: system busy
        return;
    }

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
    hmi_write_text((uint16_t)QR_CODE, String(extracted_data));

    // New raw-material batch detected → reset cycle so printing can resume
    if (last_batch_id[0] != '\0' && strcmp(extracted_data, last_batch_id) != 0)
    {
        Serial.print("NEW BATCH detected (");
        Serial.print(last_batch_id);
        Serial.print(" -> ");
        Serial.print(extracted_data);
        Serial.println(") - resetting cycle");
        enter_state(SYS_WAIT_FOR_START);
    }

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

        // Keep the fault flag/HMI/window-pause state in sync so the next
        // scan hits the fast Gate #0 path instead of re-discovering the
        // same fault, and the window stops burning down while it's stuck.
        set_printer_fault(true, pre_status, pre_ok);

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

    // Gate #3: never print more labels than the mold has cavities. Checked
    // here (right at the commit point, after the window is guaranteed open)
    // so printed_count is always compared against the live cavity count for
    // THIS window, not a stale one from before a config change mid-run.
    if (printed_count >= g_cavity_count)
    {
        Serial.print("REJECTED - cavity limit reached: ");
        Serial.print(printed_count);
        Serial.print("/");
        Serial.println(g_cavity_count);
        hmi_set_message(String("Cavity Limit Reached"), MSG_SEV_REJECT);
        RGB_flash(orange); // rejected: cavity limit reached
        return;
    }

    shift_counter++;
    printed_count++;
    strncpy(last_batch_id, extracted_data, sizeof(last_batch_id) - 1);
    last_batch_id[sizeof(last_batch_id) - 1] = '\0';
    hmi_write_text((uint16_t)PRINT_COUNTER, String(printed_count));
    hmi_write_text((uint16_t)STICKER, String(shift_ok_total + printed_count));
    Serial.print("Final QR : ");
    Serial.println(final_qr);

    send_to_printer(final_qr);

    // PRINT_COUNTER updates on every successful print (event-driven).
    // QR_CODE intentionally NOT touched here - it always shows only the
    // extracted data from the scan, never the full generated/final QR.
    // hmi_write_text((uint16_t)PRINT_COUNTER, String(printed_count));

    // Gate #2 (post-check): confirm printer is still healthy after the job.
    // We can't undo a job already sent - but we CAN make sure a fault that
    // shows up right after printing is caught immediately (fault flag set,
    // HMI updated, window paused) instead of waiting for the next scheduled
    // periodic poll to notice.
    printer_status_t post_status;
    bool post_ok = poll_printer_status(post_status);

    Serial.print("Printer Status After Print: ");
    Serial.println(post_ok ? printer_status_to_text(post_status) : "NO RESPONSE");

    if (!post_ok || post_status != PRINTER_STATUS_NORMAL)
    {
        Serial.println("WARNING - printer reported non-normal status after print, label may be affected");
        set_printer_fault(true, post_status, post_ok);
    }

    RGB_flash(magenta); // success
}

#endif