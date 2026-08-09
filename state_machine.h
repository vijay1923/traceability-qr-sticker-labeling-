#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <Arduino.h>
#include "config.h"
#include "hmi.h"

// ---- System state machine ----
typedef enum
{
    SYS_WAIT_FOR_START,
    SYS_INSPECTION_WINDOW,
    SYS_CYCLE_TIME
} system_state_t;

system_state_t system_state = SYS_WAIT_FOR_START;
unsigned long state_start_ms = 0;

uint8_t printed_count = 0; // resets each time INSPECTION_WINDOW opens, used for OK/NG summary

// ---- INSPECTION_WINDOW pause tracking ----
// Either a printer fault or a scanner disconnect should pause the window
// countdown - both are conditions where production genuinely cannot
// continue, so counting that time toward the window's duration would wrongly
// turn unprinted cavities into NG. Two independent source flags feed one
// combined pause state so overlapping faults (both at once) are handled
// correctly: the pause starts on the FIRST fault to appear and only actually
// resumes once BOTH have cleared, with total paused time accumulated across
// the whole combined period regardless of which flag(s) caused it.
unsigned long window_pause_accum_ms = 0;   // total ms this window has spent paused
unsigned long window_pause_start_ms = 0;   // millis() when the current pause began
bool window_paused_for_printer = false;
bool window_paused_for_scanner = false;
bool window_currently_paused = false;

// Call this after changing window_paused_for_printer or window_paused_for_scanner.
void window_pause_recompute()
{
    bool should_pause = (window_paused_for_printer || window_paused_for_scanner)
                         && (system_state == SYS_INSPECTION_WINDOW);

    if (should_pause && !window_currently_paused)
    {
        window_currently_paused = true;
        window_pause_start_ms = millis();
        Serial.println("Inspection window PAUSED");
    }
    else if (!should_pause && window_currently_paused)
    {
        window_pause_accum_ms += millis() - window_pause_start_ms;
        window_currently_paused = false;
        Serial.println("Inspection window RESUMED");
    }
}
// --------------------------------------------------------------------------

void enter_state(system_state_t new_state)
{
    system_state = new_state;
    state_start_ms = millis();

    switch (new_state)
    {
        case SYS_WAIT_FOR_START:
            Serial.println("STATE -> WAIT_FOR_START (waiting for first scan to synchronize)");
            hmi_condition_set(HMI_COND_ROUTINE, String("Waiting for first scan"), HMI_SEV_ROUTINE);
            break;

        case SYS_INSPECTION_WINDOW:
            printed_count = 0;
            window_pause_accum_ms = 0;
            window_currently_paused = false;
            Serial.println("STATE -> INSPECTION_WINDOW (scan/print allowed)");
            hmi_condition_set(HMI_COND_ROUTINE, String("Inspection Window Open"), HMI_SEV_ROUTINE);
            hmi_write_text((uint16_t)PRINT_COUNTER, String(0));

            // A fault may already be active right as this window opens (e.g.
            // opened automatically on a timer, not preceded by a fresh scan
            // that would have gone through Gate #0/#2). Start paused
            // immediately instead of waiting for the next poll to notice.
            window_pause_recompute();
            break;

        case SYS_CYCLE_TIME:
            Serial.println("STATE -> CYCLE_TIME (printing blocked)");
            hmi_condition_set(HMI_COND_ROUTINE, String("Cycle Running"), HMI_SEV_ROUTINE);
            break;
    }
}

#endif