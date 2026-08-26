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
    SYS_CYCLE_TIME,
    SYS_ERROR   // printer and/or scanner fault - production blocked until cleared
} system_state_t;

// Defined in state_machine.cpp.
extern system_state_t system_state;
extern unsigned long state_start_ms;

extern uint8_t printed_count; // resets each time INSPECTION_WINDOW opens, used for OK/NG summary

// ---- INSPECTION_WINDOW pause tracking ----
// Either a printer fault or a scanner disconnect should pause the window
// countdown - both are conditions where production genuinely cannot
// continue, so counting that time toward the window's duration would wrongly
// turn unprinted cavities into NG. Two independent source flags feed one
// combined pause state so overlapping faults (both at once) are handled
// correctly: the pause starts on the FIRST fault to appear and only actually
// resumes once BOTH have cleared, with total paused time accumulated across
// the whole combined period regardless of which flag(s) caused it.
extern unsigned long window_pause_accum_ms;   // total ms this window has spent paused
extern bool window_paused_for_printer;
extern bool window_paused_for_scanner;
extern bool window_currently_paused;

// Call this after changing window_paused_for_printer or window_paused_for_scanner.
void window_pause_recompute();
// --------------------------------------------------------------------------

void enter_state(system_state_t new_state);

// Call after a printer or scanner fault flag changes - moves system_state
// between WAIT_FOR_START and ERROR to reflect whether a fault is active.
// No-op while INSPECTION_WINDOW/CYCLE_TIME are running (those handle faults
// via the window-pause mechanism above instead).
void system_fault_recompute();

#endif
