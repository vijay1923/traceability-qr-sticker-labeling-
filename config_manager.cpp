#include <Arduino.h>
#include "config_manager.h"

uint32_t g_cycle_time_s        = CYCLE_TIME_S;
uint32_t g_inspection_window_s = INSPECTION_WINDOW_S;
uint8_t  g_cavity_count        = CAVITY_COUNT;
uint16_t g_shift_a_min         = SHIFT_A_START_MIN;
uint16_t g_shift_b_min         = SHIFT_B_START_MIN;
Preferences cfg_prefs;

void load_config_from_nvs()
{
    g_cycle_time_s        = cfg_prefs.getUInt("cycle_s", CYCLE_TIME_S);
    g_inspection_window_s = cfg_prefs.getUInt("insp_s", INSPECTION_WINDOW_S);
    g_cavity_count        = (uint8_t)cfg_prefs.getUInt("cavity", CAVITY_COUNT);
    g_shift_a_min         = (uint16_t)cfg_prefs.getUInt("shift_a_m", SHIFT_A_START_MIN);
    g_shift_b_min         = (uint16_t)cfg_prefs.getUInt("shift_b_m", SHIFT_B_START_MIN);
}

static void format_hhmm(uint16_t minutes_of_day, char out[6])
{
    snprintf(out, 6, "%02u:%02u", (unsigned)(minutes_of_day / 60), (unsigned)(minutes_of_day % 60));
}

void print_config()
{
    char hhmm[6];
    Serial.println("---- Configuration ----");
    Serial.print("  cycletime   = "); Serial.print(g_cycle_time_s); Serial.println(" s");
    Serial.print("  inspwindow  = "); Serial.print(g_inspection_window_s); Serial.println(" s");
    Serial.print("  cavity      = "); Serial.println(g_cavity_count);
    format_hhmm(g_shift_a_min, hhmm);
    Serial.print("  shift_a     = "); Serial.println(hhmm);
    format_hhmm(g_shift_b_min, hhmm);
    Serial.print("  shift_b     = "); Serial.println(hhmm);
    Serial.println("------------------------");
}

bool set_config_value(const char *key, long value)
{
    if (strcmp(key, "cycletime") == 0)
    {
        if (value <= 0) return false;
        g_cycle_time_s = (uint32_t)value;
        cfg_prefs.putUInt("cycle_s", g_cycle_time_s);
        return true;
    }
    if (strcmp(key, "inspwindow") == 0)
    {
        if (value <= 0) return false;
        g_inspection_window_s = (uint32_t)value;
        cfg_prefs.putUInt("insp_s", g_inspection_window_s);
        return true;
    }
    if (strcmp(key, "cavity") == 0)
    {
        if (value <= 0 || value > 255) return false;
        g_cavity_count = (uint8_t)value;
        cfg_prefs.putUInt("cavity", g_cavity_count);
        return true;
    }
    if (strcmp(key, "shift_a") == 0)
    {
        if (value < 0 || value > 1439) return false; // minutes-since-midnight, 0..23:59
        g_shift_a_min = (uint16_t)value;
        cfg_prefs.putUInt("shift_a_m", g_shift_a_min);
        return true;
    }
    if (strcmp(key, "shift_b") == 0)
    {
        if (value < 0 || value > 1439) return false;
        g_shift_b_min = (uint16_t)value;
        cfg_prefs.putUInt("shift_b_m", g_shift_b_min);
        return true;
    }

    return false; // unknown key
}

void parse_config_barcode(const char *payload)
{
    char buf[64];
    strncpy(buf, payload, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    bool any_ok = false;
    char *token = strtok(buf, ",");
    while (token != nullptr)
    {
        char key[16];
        long raw_val;
        if (sscanf(token, "%15[^:]:%ld", key, &raw_val) == 2)
        {
            // Short QR tokens map onto set_config_value()'s existing
            // internal key names/validation/persistence - only the token
            // format (short name, colon separator) is new here.
            const char *internal_key = nullptr;
            bool is_minutes_field = false;

            if (strcmp(key, "CVT") == 0)
            {
                internal_key = "cavity";
            }
            else if (strcmp(key, "CYCT") == 0)
            {
                internal_key = "cycletime";
                is_minutes_field = true;
            }
            else if (strcmp(key, "INPT") == 0)
            {
                internal_key = "inspwindow";
                is_minutes_field = true;
            }

            if (internal_key == nullptr)
            {
                Serial.print("CFG ERR - unknown key: ");
                Serial.println(key);
            }
            else
            {
                long store_val = is_minutes_field ? raw_val * 60L : raw_val; // minutes -> seconds

                if (set_config_value(internal_key, store_val))
                {
                    Serial.print("CFG OK - ");
                    Serial.print(internal_key);
                    Serial.print(" = ");
                    Serial.println(store_val);
                    if (strcmp(internal_key, "cavity") == 0)
                    hmi.Write_UString((uint16_t)MOLD_CAVITY, String(g_cavity_count));
                    any_ok = true;
                }
                else
                {
                    Serial.print("CFG ERR - bad value for ");
                    Serial.println(key);
                }
            }
        }
        token = strtok(nullptr, ",");
    }

    if (any_ok)
        print_config();
}

void reset_config_to_defaults()
{
    cfg_prefs.clear();
    g_cycle_time_s        = CYCLE_TIME_S;
    g_inspection_window_s = INSPECTION_WINDOW_S;
    g_cavity_count        = CAVITY_COUNT;
    g_shift_a_min         = SHIFT_A_START_MIN;
    g_shift_b_min         = SHIFT_B_START_MIN;
    Serial.println("OK - config reset to compiled-in defaults");
}
