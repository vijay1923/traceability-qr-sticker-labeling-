#include <Arduino.h>
#include "hmi.h"

dwindisplay hmi;

// ---- MESSAGE-line condition registry storage (private to this module) ----
struct hmi_condition_t
{
    bool active;
    uint8_t severity;
    char text[HMI_CONDITION_TEXT_LEN];
    unsigned long expire_at_ms; // 0 = persists until explicitly cleared
};

static hmi_condition_t hmi_conditions[HMI_COND_COUNT];
static hmi_condition_id_t hmi_currently_shown = HMI_COND_ROUTINE;

static void hmi_condition_refresh_display()
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

    hmi.Write_UString((uint16_t)MESSAGE, String(hmi_conditions[best].text));
}

void hmi_condition_set(hmi_condition_id_t id, const char *text, uint8_t severity, unsigned long duration_ms)
{
    hmi_conditions[id].active = true;
    hmi_conditions[id].severity = severity;
    strncpy(hmi_conditions[id].text, text, sizeof(hmi_conditions[id].text) - 1);
    hmi_conditions[id].text[sizeof(hmi_conditions[id].text) - 1] = '\0';
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
    Serial.println("HMI initialized");
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
    hmi.Write_UString((uint16_t)QR_CODE, String(""));
    hmi.Write_UString((uint16_t)PRINT_COUNTER, String(""));
    hmi.Write_UString((uint16_t)STICKER, String(""));
    hmi.Write_UString((uint16_t)MOLD_CAVITY, String(""));

    // ROUTINE is the fallback condition and must always be active - every
    // other slot is optional, but the display needs SOMETHING to show once
    // no faults/rejects are active.
    hmi_condition_set(HMI_COND_ROUTINE, "Starting up", HMI_SEV_ROUTINE);
}

void hmi_test_help()
{
    Serial.println("HMI commands (Write_UString only):");
    Serial.println("  hmitext <vp_hex> <text>     -> write UTF text to VP");
    Serial.println("  hmiwrite <vp_hex> <number>  -> write number as text to VP");
    Serial.println("  hmihelp                     -> show this HMI command list");
}
