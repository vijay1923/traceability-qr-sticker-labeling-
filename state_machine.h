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

// Lets a printer fault pause the INSPECTION_WINDOW countdown instead of it
// silently running out and counting unprinted cavities as NG. Actual
// pause/resume decisions are made in printer_manager.h's set_printer_fault()
// (which has visibility into both this state and the printer fault flag);
// this file only owns resetting the accumulator on window entry, since that
// doesn't need to know anything about the printer.
unsigned long window_pause_accum_ms = 0;   // total ms this window has spent paused
bool window_paused_for_printer = false;    // true while currently paused
unsigned long window_pause_start_ms = 0;   // millis() when the current pause began

void enter_state(system_state_t new_state)
{
    system_state = new_state;
    state_start_ms = millis();

    switch (new_state)
    {
        case SYS_WAIT_FOR_START:
            Serial.println("STATE -> WAIT_FOR_START (waiting for first scan to synchronize)");
            hmi_set_message(String("Waiting for first scan"), MSG_SEV_INFO);
            break;

        case SYS_INSPECTION_WINDOW:
            printed_count = 0;
            window_pause_accum_ms = 0;
            window_paused_for_printer = false;
            Serial.println("STATE -> INSPECTION_WINDOW (scan/print allowed)");
            hmi_set_message(String("Inspection Window Open"), MSG_SEV_INFO);
            hmi_write_text((uint16_t)PRINT_COUNTER, String(0));
            break;

        case SYS_CYCLE_TIME:
            Serial.println("STATE -> CYCLE_TIME (printing blocked)");
            hmi_set_message(String("Cycle Running"), MSG_SEV_INFO);
            break;
    }
}

#endif