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

void enter_state(system_state_t new_state)
{
    system_state = new_state;
    state_start_ms = millis();

    switch (new_state)
    {
        case SYS_WAIT_FOR_START:
            Serial.println("STATE -> WAIT_FOR_START (waiting for first scan to synchronize)");
           hmi.Write_UString((uint16_t)MESSAGE, String("Waiting for first scan"));
            break;

        case SYS_INSPECTION_WINDOW:
            printed_count = 0;
            Serial.println("STATE -> INSPECTION_WINDOW (scan/print allowed)");
            hmi.Write_UString((uint16_t)MESSAGE, String("Inspection Window Open"));
            hmi_write_number((uint16_t)PRINT_COUNTER, 0);
            break;

        case SYS_CYCLE_TIME:
            Serial.println("STATE -> CYCLE_TIME (printing blocked)");
            hmi.Write_UString((uint16_t)MESSAGE, String("Cycle Running"));
            break;
    }
}

#endif