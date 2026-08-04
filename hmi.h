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

inline void hmi_write_number(uint16_t vp, uint32_t value)
{
    hmi.Write_Number(vp, hmi_to_u16(value));
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

    // hmi.Write_UString((uint16_t)QR_CODE, String("Startup"));
    // hmi_write_number((uint16_t)PRINT_COUNTER, 0);
    // hmi_write_number((uint16_t)STICKER, 0);
    // hmi_write_number((uint16_t)MOLD_CAVITY, CAVITY_COUNT);
}

void hmi_test_help()
{
    Serial.println("HMI commands (Write_UString only):");
    Serial.println("  hmitext <vp_hex> <text>     -> write UTF text to VP");
    Serial.println("  hmiwrite <vp_hex> <number>  -> write number as text to VP");
    Serial.println("  hmihelp                     -> show this HMI command list");
}

#endif