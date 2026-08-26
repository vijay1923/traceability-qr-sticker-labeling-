#include <Arduino.h>
#include "scanner_manager.h"

// See rgb_status.h for why this is a single extern declaration instead of
// #include <usb_scanner_Lib.h> - that header has no include guard.
extern uint8_t barcode_status;

uint8_t lastConnectedState = 0;

char extracted_data[20];
char barcode_data[50];

bool scanner_connected()
{
    return barcode_status;
}

void set_scanner_fault(bool disconnected)
{
    hmi.Write_UString((uint16_t)SCANNER_STATUS, String(disconnected ? "Disconnected" : "Connected"));

    if (disconnected)
    {
        hmi_condition_set(HMI_COND_SCANNER, "Scanner Disconnected", HMI_SEV_SCANNER);
    }
    else
    {
        hmi_condition_clear(HMI_COND_SCANNER);
    }

    window_paused_for_scanner = disconnected;
    window_pause_recompute();
    system_fault_recompute();
}

bool validate_raw_format(const char *data)
{
    if (data == nullptr || data[0] == '\0')
        return false;

    size_t len = strlen(data);
    if (len >= sizeof(barcode_data))
        return false;

    if (data[0] == EXPECTED_DELIMITER || data[len - 1] == EXPECTED_DELIMITER)
        return false;

    uint8_t segment_count = 1;
    bool prev_was_delim = false;

    for (size_t i = 0; i < len; i++)
    {
        if (data[i] == EXPECTED_DELIMITER)
        {
            if (prev_was_delim)
                return false;

            segment_count++;
            prev_was_delim = true;
        }
        else
        {
            prev_was_delim = false;
        }
    }

    return (segment_count == EXPECTED_SEGMENT_COUNT);
}

void data_extract(const char *data)
{
    size_t i = 0;

    if (data == nullptr)
    {
        extracted_data[0] = '\0';
        return;
    }

    while (data[i] != EXPECTED_DELIMITER && data[i] != '\0' && i < (sizeof(extracted_data) - 1))
    {
        extracted_data[i] = data[i];
        i++;
    }

    extracted_data[i] = '\0';
}
