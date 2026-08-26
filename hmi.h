#ifndef HMI_H
#define HMI_H

#include <Arduino.h>
#include "dwindisplay.h" // local/library DWIN display driver
#include "config.h"
#include "rtc_driver.h"

// Must match baud configured in DWIN DGUS project.
#define HMI_BAUD 115200
#define HMI_MAIN_PAGE 1

// Defined in hmi.cpp.
extern dwindisplay hmi;

// Every VP on this screen (QR_CODE, MESSAGE, PRINT_COUNTER, STICKER,
// MOLD_CAVITY, SCANNER_STATUS, PRINTER_STATUS) is a Text-type widget per the
// DWIN project's address table - so hmi.Write_UString(vp, String(...)) is
// the ONLY write path used anywhere in this project, whether the value is a
// string or a number (numbers are converted with String(value) first).
// Write_Number sends raw binary bytes, which renders as garbage on a
// Text-type widget - do not use it here.


// ---- MESSAGE-line condition registry ----
// Several independent things want to put text on the HMI's single MESSAGE
// line (routine state, cavity-limit rejections, scanner faults, printer
// faults) - and more than one can be true at once (e.g. printer AND scanner
// both down). A simple "does this outrank what's shown" overwrite loses
// information: if a printer fault is showing and a scanner fault also
// starts, whichever wrote last wins and the other becomes invisible.
//
// Instead, each source owns a fixed condition "slot" - independently active
// or not, with its own severity and text. The display always shows whichever
// ACTIVE slot has the highest severity; when that one clears, the next
// highest active slot takes over automatically. Nothing gets silently lost.
// The registry storage itself (hmi_conditions[], the hmi_condition_t struct,
// and hmi_condition_refresh_display()) is private to hmi.cpp - nothing
// outside this module touches it directly, only through the functions below.
typedef enum
{
    HMI_COND_ROUTINE = 0,  // normal state text - always active, lowest severity, the fallback
    HMI_COND_REJECT,       // transient scan rejection (cavity limit etc.) - auto-expires
    HMI_COND_SCANNER,      // scanner disconnected - persists until reconnect
    HMI_COND_PRINTER,      // printer fault - persists until cleared
    HMI_COND_COUNT
} hmi_condition_id_t;

#define HMI_SEV_ROUTINE  0
#define HMI_SEV_REJECT   1
#define HMI_SEV_SCANNER  2
#define HMI_SEV_PRINTER  3

// Buzzer only fires for conditions at/above this severity - the two faults
// that actually stop production (printer, scanner), not routine text or
// transient rejects like hitting the cavity limit.
#define HMI_BUZZ_SEVERITY_THRESHOLD HMI_SEV_SCANNER
#define HMI_BUZZ_DURATION_MS 300

// Longest formatted message today is "Printer Fault: " + the longest
// composite status name (OUT_OF_RIBBON_OUT_OF_PAPER_HEAD_OPEN, 37 chars) =
// 52 chars + null. 64 leaves comfortable headroom for anything shorter.
// Public: printer_manager.cpp sizes its own snprintf buffer against this.
#define HMI_CONDITION_TEXT_LEN 64

// duration_ms: 0 = persists until hmi_condition_clear() is called explicitly.
// Non-zero = auto-clears itself after that many ms (used for transient
// rejects like the cavity-limit message). text is copied into a fixed
// buffer (see HMI_CONDITION_TEXT_LEN) - no heap allocation here, so this is
// safe to call as often as callers like (e.g. every printer poll while a
// fault is active) without fragmenting the heap over a long uptime.
void hmi_condition_set(hmi_condition_id_t id, const char *text, uint8_t severity, unsigned long duration_ms = 0);
void hmi_condition_clear(hmi_condition_id_t id);

// Call periodically from loop() to expire transient conditions (HMI_COND_REJECT).
void hmi_condition_service();
// --------------------------------------------------------------------------

void hmi_init();
void hmi_test_help();

#endif
