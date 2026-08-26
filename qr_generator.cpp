#include <Arduino.h>
#include "qr_generator.h"

char final_qr[30];

bool generate_traceability_qr(const char *part_number)
{
    uint8_t day, month, hour, minute;
    uint16_t year;

    if (!ds3231_read_time(day, month, year, hour, minute))
    {
        final_qr[0] = '\0';
        return false;
    }

    char shift = compute_shift(hour, minute);

    int yy = year % 100;

    snprintf(final_qr, sizeof(final_qr), "%s/%02d%02d%02d/%c/%04d",
              part_number,
              day,
              month,
              yy,
              shift,
              shift_counter + 1);

    return true;
}

bool validate_final_qr(const char *qr)
{
    if (qr == nullptr || qr[0] == '\0')
        return false;

    size_t len = strlen(qr);
    if (len >= sizeof(final_qr) - 1)
        return false;

    uint8_t slash_count = 0;
    for (size_t i = 0; i < len; i++)
    {
        if (qr[i] == '/')
            slash_count++;
    }

    return (slash_count == (EXPECTED_SEGMENT_COUNT - 1));
}
