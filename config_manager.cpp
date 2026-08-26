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

// ---- Config-confirmation MESSAGE queue ----
// A config QR can update 1-3 settings in one scan (CVT, CYCT, INPT), but the
// HMI's MESSAGE line only shows one thing at a time (see hmi.h's condition
// registry). Rather than cram every result into a single line, each result
// (success or error) is queued here and shown on HMI_COND_CONFIG one at a
// time, CONFIG_CONFIRM_DISPLAY_MS apiece, so a supervisor scanning all three
// at once can actually read each one before it's replaced by the next.
//
// This is deliberately separate from hmi_condition_service()'s own
// expire-and-clear logic: that logic only ever clears a slot, it doesn't
// know to load a *new* message into the same slot when one expires. This
// queue is what does that "load the next one" step, driven by its own timer
// checked once per loop() pass from config_confirm_service().
#define CONFIG_CONFIRM_QUEUE_LEN     3  // max simultaneous keys in one CFG: payload (CVT, CYCT, INPT)
#define CONFIG_CONFIRM_DISPLAY_MS    2500

static char          s_confirm_queue[CONFIG_CONFIRM_QUEUE_LEN][HMI_CONDITION_TEXT_LEN];
static uint8_t        s_confirm_queue_len = 0;   // number of messages queued this scan
static uint8_t        s_confirm_queue_pos = 0;   // index of the next not-yet-shown message
static unsigned long  s_confirm_advance_at_ms = 0;
static bool           s_confirm_active = false;  // true while a queued message is currently on screen

// Loads the next queued message onto HMI_COND_CONFIG, or clears the slot
// once the queue is exhausted (letting MESSAGE fall back to whatever's
// next-highest-severity active - normally HMI_COND_ROUTINE).
static void confirm_queue_advance()
{
    if (s_confirm_queue_pos >= s_confirm_queue_len)
    {
        hmi_condition_clear(HMI_COND_CONFIG);
        s_confirm_active = false;
        return;
    }

    hmi_condition_set(HMI_COND_CONFIG, s_confirm_queue[s_confirm_queue_pos], HMI_SEV_CONFIG, CONFIG_CONFIRM_DISPLAY_MS);
    s_confirm_advance_at_ms = millis() + CONFIG_CONFIRM_DISPLAY_MS;
    s_confirm_queue_pos++;
    s_confirm_active = true;
}

// Starts a fresh queue for a new config-barcode scan. Called once at the
// top of parse_config_barcode() - a config QR scanned mid-sequence of a
// previous one simply restarts the sequence with the new results, rather
// than mixing the two.
static void confirm_queue_reset()
{
    s_confirm_queue_len = 0;
    s_confirm_queue_pos = 0;
    s_confirm_active = false;
}

static void confirm_queue_push(const char *text)
{
    if (s_confirm_queue_len >= CONFIG_CONFIRM_QUEUE_LEN)
        return; // more than 3 tokens in one payload isn't a supported case - drop silently rather than overflow

    strncpy(s_confirm_queue[s_confirm_queue_len], text, HMI_CONDITION_TEXT_LEN - 1);
    s_confirm_queue[s_confirm_queue_len][HMI_CONDITION_TEXT_LEN - 1] = '\0';
    s_confirm_queue_len++;
}

void config_confirm_service()
{
    if (!s_confirm_active)
        return;

    if ((long)(millis() - s_confirm_advance_at_ms) >= 0)
        confirm_queue_advance();
}
// --------------------------------------------------------------------------

void parse_config_barcode(const char *payload)
{
    char buf[64];
    strncpy(buf, payload, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    confirm_queue_reset();

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

                char msg[HMI_CONDITION_TEXT_LEN];
                snprintf(msg, sizeof(msg), "Config Error: %s", key);
                confirm_queue_push(msg);
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

                    // Operator/supervisor-facing confirmation text - plain
                    // units (minutes, not seconds) matching what they typed
                    // into the QR generator, not the internal key name.
                    char msg[HMI_CONDITION_TEXT_LEN];
                    if (strcmp(internal_key, "cavity") == 0)
                        snprintf(msg, sizeof(msg), "Cavity Set: %ld", raw_val);
                    else if (strcmp(internal_key, "cycletime") == 0)
                        snprintf(msg, sizeof(msg), "Cycle Time: %ld min", raw_val);
                    else if (strcmp(internal_key, "inspwindow") == 0)
                        snprintf(msg, sizeof(msg), "Window: %ld min", raw_val);
                    else
                        snprintf(msg, sizeof(msg), "%s Set: %ld", internal_key, raw_val);
                    confirm_queue_push(msg);
                }
                else
                {
                    Serial.print("CFG ERR - bad value for ");
                    Serial.println(key);

                    char msg[HMI_CONDITION_TEXT_LEN];
                    snprintf(msg, sizeof(msg), "Config Error: bad %s", key);
                    confirm_queue_push(msg);
                }
            }
        }
        token = strtok(nullptr, ",");
    }

    if (any_ok)
        print_config();

    confirm_queue_advance(); // kick off display of the first queued message, if any
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
