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

// ---- MESSAGE-line priority ----
// Several places want to put text on the HMI's single top MESSAGE line
// (routine state text, cavity-limit rejections, printer faults). Without
// a priority rule, whichever one writes last wins - so a routine "Cycle
// Running" could silently overwrite an active printer-fault message.
// Higher severity always wins; equal-or-higher can refresh; force=true
// bypasses the check entirely (used when a condition is intentionally
// being cleared).
#define MSG_SEV_INFO   0  // routine state text
#define MSG_SEV_REJECT 1  // transient scan rejection (cavity limit, etc.)
#define MSG_SEV_FAULT  2  // printer fault - persists until explicitly cleared

uint8_t g_message_severity = MSG_SEV_INFO;

void hmi_set_message(const String &text, uint8_t severity, bool force = false)
{
    if (force || severity >= g_message_severity)
    {
        hmi_write_text((uint16_t)MESSAGE, text);
        g_message_severity = severity;
    }
}

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
}

void hmi_test_help()
{
    Serial.println("HMI commands (Write_UString only):");
    Serial.println("  hmitext <vp_hex> <text>     -> write UTF text to VP");
    Serial.println("  hmiwrite <vp_hex> <number>  -> write number as text to VP");
    Serial.println("  hmihelp                     -> show this HMI command list");
}

#endif