#ifndef HMI_H
#define HMI_H

#include "dwindisplay.h" // local/library DWIN display driver
#include "config.h"
#include "rtc_driver.h"

// Must match baud configured in DWIN DGUS project.
#define HMI_BAUD 115200
#define HMI_MAIN_PAGE 1
dwindisplay hmi;

inline uint16_t hmi_to_u16(uint32_t value)
{
    return (value > 65535UL) ? 65535U : (uint16_t)value;
}

// Deprecated for the VPs on this screen - every one of them is a Text-type
// widget (per the DWIN project's address table), not a Number-type widget.
// Write_Number sends raw binary bytes; a Text widget fed binary instead of
// proper Unicode text renders garbage. Kept only in case a genuinely
// Number-type widget is ever added to the project - do not use it for
// QR_CODE/MESSAGE/PRINT_COUNTER/STICKER/MOLD_CAVITY/SCANNER_STATUS/PRINTER_STATUS.
inline void hmi_write_number(uint16_t vp, uint32_t value)
{
    hmi.Write_Number(vp, hmi_to_u16(value));
}

// ---- Unified text write: always Write_UString, always blanks first ----
// Blanking first prevents stale trailing characters from a previous, longer
// string showing through past the end of a shorter new one (e.g. "HELLO"
// overwriting "HI" would otherwise leave "HILLO" on screen). Width is one
// conservative value covering every text field on this screen rather than
// per-VP tuning - writing a few extra blank spaces is harmless, writing too
// few is not.
#define HMI_BLANK_WIDTH 32

void hmi_write_text(uint16_t vp, const String &text)
{
    String blank;
    for (uint8_t i = 0; i < HMI_BLANK_WIDTH; i++) blank += ' ';
    hmi.Write_UString(vp, blank);
    hmi.Write_UString(vp, text);
}
// --------------------------------------------------------------------------

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

struct hmi_condition_t
{
    bool active;
    uint8_t severity;
    String text;
    unsigned long expire_at_ms; // 0 = persists until explicitly cleared
};

hmi_condition_t hmi_conditions[HMI_COND_COUNT];
hmi_condition_id_t hmi_currently_shown = HMI_COND_ROUTINE;

void hmi_condition_refresh_display()
{
    int best = -1;
    for (int i = 0; i < HMI_COND_COUNT; i++)
    {
        if (hmi_conditions[i].active &&
            (best == -1 || hmi_conditions[i].severity > hmi_conditions[best].severity))
        {
            best = i;
        }
    }
    if (best == -1)
        return; // nothing active at all - shouldn't happen, ROUTINE should always be active

    if ((hmi_condition_id_t)best != hmi_currently_shown)
    {
        if (hmi_conditions[best].severity >= HMI_BUZZ_SEVERITY_THRESHOLD)
        {
            hmi.buzzerOn(HMI_BUZZ_DURATION_MS);
        }
        hmi_currently_shown = (hmi_condition_id_t)best;
    }

    hmi_write_text((uint16_t)MESSAGE, hmi_conditions[best].text);
}

// duration_ms: 0 = persists until hmi_condition_clear() is called explicitly.
// Non-zero = auto-clears itself after that many ms (used for transient
// rejects like the cavity-limit message).
void hmi_condition_set(hmi_condition_id_t id, const String &text, uint8_t severity, unsigned long duration_ms = 0)
{
    hmi_conditions[id].active = true;
    hmi_conditions[id].severity = severity;
    hmi_conditions[id].text = text;
    hmi_conditions[id].expire_at_ms = duration_ms ? (millis() + duration_ms) : 0;
    hmi_condition_refresh_display();
}

void hmi_condition_clear(hmi_condition_id_t id)
{
    if (!hmi_conditions[id].active)
        return; // already clear, nothing to do
    hmi_conditions[id].active = false;
    hmi_condition_refresh_display();
}

// Call periodically from loop() to expire transient conditions (HMI_COND_REJECT).
void hmi_condition_service()
{
    unsigned long now = millis();
    bool any_expired = false;

    for (int i = 0; i < HMI_COND_COUNT; i++)
    {
        if (hmi_conditions[i].active && hmi_conditions[i].expire_at_ms != 0 &&
            now >= hmi_conditions[i].expire_at_ms)
        {
            hmi_conditions[i].active = false;
            any_expired = true;
        }
    }

    if (any_expired)
        hmi_condition_refresh_display();
}
// --------------------------------------------------------------------------

void hmi_init()
{
    // UART number is selected by DWIN_HMI_UART_NUM in dwindisplay.h.
    hmi.begin(HMI_BAUD, SERIAL_8N1, DWIN_RX_PIN, DWIN_TX_PIN);
    Serial.println("HMI initialized (dwindisplay)");
    delay(100);
    hmi.showPage(HMI_MAIN_PAGE); // show main page on HMI in setup only, after that page changes are controlled by HMI touch events

    uint8_t day = 1, month = 1, hour = 0, minute = 0;
    uint16_t year = 2026;
    if (ds3231_read_time(day, month, year, hour, minute))
    {
        hmi.Time_Stamp(day, month, (uint8_t)(year % 100), hour, minute, 0);
        Serial.print("HMI Time_Stamp set to: ");
        Serial.print(year); Serial.print("-");
        Serial.print(month); Serial.print("-");
        Serial.print(day); Serial.print(" ");
        Serial.print(hour); Serial.print(":");
        Serial.println(minute);
    }
    else
    {
        hmi.Time_Stamp(day, month, 0, hour, minute, 0);
        Serial.println("WARN - failed to read RTC time for HMI Time_Stamp");
    }


    // Boot defaults (all as text via Write_UString)
    String blank = String("");
    hmi.Write_UString((uint16_t)QR_CODE, String(""));
    hmi.Write_UString((uint16_t)PRINT_COUNTER, String(""));
    hmi.Write_UString((uint16_t)STICKER, String(""));
    hmi.Write_UString((uint16_t)MOLD_CAVITY, String(""));

    // ROUTINE is the fallback condition and must always be active - every
    // other slot is optional, but the display needs SOMETHING to show once
    // no faults/rejects are active.
    hmi_condition_set(HMI_COND_ROUTINE, String("Starting up"), HMI_SEV_ROUTINE);
}

void hmi_test_help()
{
    Serial.println("HMI commands (Write_UString only):");
    Serial.println("  hmitext <vp_hex> <text>     -> write UTF text to VP");
    Serial.println("  hmiwrite <vp_hex> <number>  -> write number as text to VP");
    Serial.println("  hmihelp                     -> show this HMI command list");
}

#endif