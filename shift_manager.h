#ifndef SHIFT_MANAGER_H
#define SHIFT_MANAGER_H

#include <LittleFS.h>
#include "config.h"
#include "config_manager.h"
#include "state_machine.h"
#include "storage_manager.h"

char current_shift = '\0';   // '\0' = not yet determined
uint16_t shift_counter = 0;  // after boot 0

// ---- Shift stats accumulation (RAM, checkpointed to flash) ----
uint32_t shift_ok_total = 0;
uint32_t shift_ng_total = 0;
uint8_t  shift_rec_day = 0;    // date the CURRENT shift started (per spec: a shift's
uint8_t  shift_rec_month = 0;  // record uses its start date, even if it ends next
uint16_t shift_rec_year = 0;   // calendar day, e.g. Shift B spanning midnight)
unsigned long last_shift_check_ms = 0;

// A shift boundary detected while a scan window is open must not be applied
// immediately - the window's OK/NG counts still need to be folded into the
// OLD shift first. These hold the deferred rollover until the window closes.
bool     shift_rollover_pending = false;
char     pending_shift = '\0';
uint8_t  pending_day = 0;
uint8_t  pending_month = 0;
uint16_t pending_year = 0;
// --------------------------------------------------------------------------

char compute_shift(uint8_t hour)
{
    return (hour >= g_shift_a_hour && hour < g_shift_b_hour) ? 'A' : 'B';
}

// Writes one line to /stats/MMYY.csv : DDMMYY,shift,ok,ng
// Append-only, ~2 writes/day - acceptable flash wear, worst case on power
// loss mid-write is one incomplete trailing line, not corruption of prior data.
bool write_shift_stats(uint8_t day, uint8_t month, uint16_t year, char shift, uint32_t ok, uint32_t ng)
{
    if (!littlefs_check_space_ok("write_shift_stats"))
        return false;

    if (!LittleFS.exists(STATS_DIR))
    {
        LittleFS.mkdir(STATS_DIR);
    }

    char filename[32];
    snprintf(filename, sizeof(filename), STATS_DIR "/%02d%02d.csv", month, (int)(year % 100));

    File f = LittleFS.open(filename, "a");
    if (!f)
    {
        Serial.print("ERR - failed to open stats file for writing: ");
        Serial.println(filename);
        return false;
    }

    char line[48];
    int line_len = snprintf(line, sizeof(line), "%02d%02d%02d,%c,%lu,%lu\n",
              day, month, (int)(year % 100), shift, (unsigned long)ok, (unsigned long)ng);

    size_t written = f.print(line);
    f.close();

    if (line_len < 0 || written != (size_t)line_len)
    {
        Serial.print("ERR - stats write incomplete for ");
        Serial.print(filename);
        Serial.print(" (wrote ");
        Serial.print((unsigned)written);
        Serial.print("/");
        Serial.print(line_len);
        Serial.println(" bytes)");
        return false;
    }

    Serial.print("STATS WRITTEN -> ");
    Serial.print(filename);
    Serial.print(" : ");
    Serial.print(line);
    return true;
}

// Persists the CURRENT (in-progress, not-yet-finalized) shift totals so a
// power loss mid-shift only loses activity since the last window close, not
// the entire shift. Called every time an INSPECTION_WINDOW closes.
bool write_checkpoint()
{
    if (!littlefs_check_space_ok("write_checkpoint"))
        return false;

    if (!LittleFS.exists(STATS_DIR))
    {
        LittleFS.mkdir(STATS_DIR);
    }

    File f = LittleFS.open(STATS_CHECKPOINT_FILE, "w"); // "w" = overwrite, not append
    if (!f)
    {
        Serial.println("ERR - failed to open checkpoint file for writing");
        return false;
    }

    char line[64];
    int line_len = snprintf(line, sizeof(line), "%02d,%02d,%04d,%c,%lu,%lu,%u\n",
              shift_rec_day, shift_rec_month, shift_rec_year,
              current_shift == '\0' ? '?' : current_shift,
              (unsigned long)shift_ok_total, (unsigned long)shift_ng_total,
              (unsigned)shift_counter);

    size_t written = f.print(line);
    f.close();

    if (line_len < 0 || written != (size_t)line_len)
    {
        Serial.println("ERR - checkpoint write incomplete");
        return false;
    }

    return true;
}

// Reads back the checkpoint file at boot. Returns true and fills the
// out-params if a valid checkpoint was found, false otherwise (e.g. first
// ever boot, or file missing/corrupt - either is treated as "nothing to
// recover", not an error).
bool read_checkpoint(uint8_t &day, uint8_t &month, uint16_t &year, char &shift,
                             uint32_t &ok, uint32_t &ng, uint16_t &counter)
{
    if (!littlefs_mounted)
        return false;

    if (!LittleFS.exists(STATS_CHECKPOINT_FILE))
        return false;

    File f = LittleFS.open(STATS_CHECKPOINT_FILE, "r");
    if (!f)
        return false;

    char line[64];
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = '\0';
    f.close();

    int d, mo, y;
    unsigned long ok_val, ng_val;
    unsigned int counter_val;

    int parsed = sscanf(line, "%d,%d,%d,%c,%lu,%lu,%u",
                         &d, &mo, &y, &shift, &ok_val, &ng_val, &counter_val);

    if (parsed != 7 || shift == '?')
        return false; // corrupt / incomplete line, ignore rather than trust garbage

    day = (uint8_t)d;
    month = (uint8_t)mo;
    year = (uint16_t)y;
    ok = (uint32_t)ok_val;
    ng = (uint32_t)ng_val;
    counter = (uint16_t)counter_val;
    return true;
}

// Called periodically (wall-clock driven, not scan-driven) so a shift
// boundary is never missed just because no scan happened near it.
//
// If a scan window is currently open, DO NOT roll over immediately - defer
// it. The open window's OK/NG counts belong to the shift that is ending,
// and would otherwise be silently dropped if we reset the totals out from
// under it. loop() applies the deferred rollover right after that window's
// counts are folded in and checkpointed.
void check_shift_rollover(char new_shift, uint8_t day, uint8_t month, uint16_t year)
{
    bool is_change = (current_shift != '\0' && new_shift != current_shift);

    if (is_change)
    {
        if (system_state == SYS_INSPECTION_WINDOW)
        {
            if (!shift_rollover_pending)
            {
                Serial.print("SHIFT CHANGE detected (deferred until window closes): ");
                Serial.print(current_shift);
                Serial.print(" -> ");
                Serial.println(new_shift);
            }
            shift_rollover_pending = true;
            pending_shift = new_shift;
            pending_day = day;
            pending_month = month;
            pending_year = year;
            return; // do not touch current_shift / totals yet
        }

        Serial.print("SHIFT CHANGE detected: ");
        Serial.print(current_shift);
        Serial.print(" -> ");
        Serial.println(new_shift);

        write_shift_stats(shift_rec_day, shift_rec_month, shift_rec_year,
                           current_shift, shift_ok_total, shift_ng_total);

        shift_ok_total = 0;
        shift_ng_total = 0;
        shift_counter = 0;
    }

    // Re-anchor the record date whenever a (new) shift begins - covers both
    // the very first determination at boot and every subsequent rollover.
    if (current_shift == '\0' || is_change)
    {
        shift_rec_day = day;
        shift_rec_month = month;
        shift_rec_year = year;
    }

    current_shift = new_shift;
}

// Applies a rollover that was deferred by check_shift_rollover() while a
// scan window was open. Called from loop() immediately after that window's
// OK/NG counts have been folded into shift_ok_total/shift_ng_total and
// checkpointed, so nothing is lost.
void apply_shift_rollover(char new_shift, uint8_t day, uint8_t month, uint16_t year)
{
    Serial.print("SHIFT CHANGE applying (deferred): ");
    Serial.print(current_shift);
    Serial.print(" -> ");
    Serial.println(new_shift);

    write_shift_stats(shift_rec_day, shift_rec_month, shift_rec_year,
                       current_shift, shift_ok_total, shift_ng_total);

    shift_ok_total = 0;
    shift_ng_total = 0;
    shift_counter = 0;

    shift_rec_day = day;
    shift_rec_month = month;
    shift_rec_year = year;

    current_shift = new_shift;

    write_checkpoint(); // persist the freshly-reset totals for the new shift
}

#endif