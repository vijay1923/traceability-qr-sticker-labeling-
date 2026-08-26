#include <Arduino.h>
#include "state_machine.h"
#include "rgb_status.h"

// Forward declarations instead of #include "printer_manager.h" / "scanner_manager.h" -
// both of those already include this header, so including them back would be circular.
extern bool printer_fault_active;
bool scanner_connected();

system_state_t system_state = SYS_WAIT_FOR_START;
unsigned long state_start_ms = 0;

uint8_t printed_count = 0;

unsigned long window_pause_accum_ms = 0;
static unsigned long window_pause_start_ms = 0; // millis() when the current pause began - only used inside window_pause_recompute()
bool window_paused_for_printer = false;
bool window_paused_for_scanner = false;
bool window_currently_paused = false;

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

void system_fault_recompute()
{
    bool any_fault = printer_fault_active || !scanner_connected();

    if (any_fault && system_state == SYS_WAIT_FOR_START)
    {
        enter_state(SYS_ERROR);
    }
    else if (!any_fault && system_state == SYS_ERROR)
    {
        enter_state(SYS_WAIT_FOR_START);
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
            hmi_condition_set(HMI_COND_ROUTINE, "Waiting for first scan", HMI_SEV_ROUTINE);
            if (!flashing)
            {
                RGB_setColor(base_color());
            }
            break;

        case SYS_ERROR:
            Serial.println("STATE -> SYSTEM ERROR (printer/scanner fault - production blocked)");
            hmi_condition_set(HMI_COND_ROUTINE, "System Error - Check Printer/Scanner", HMI_SEV_ROUTINE);
            if (!flashing)
            {
                RGB_setColor(red);
            }
            break;

        case SYS_INSPECTION_WINDOW:
            window_pause_accum_ms = 0;
            window_currently_paused = false;
            Serial.println("STATE -> INSPECTION_WINDOW (scan/print allowed)");
            hmi_condition_set(HMI_COND_ROUTINE, "Inspection Window Open", HMI_SEV_ROUTINE);

            // A fault may already be active right as this window opens (e.g.
            // opened automatically on a timer, not preceded by a fresh scan
            // that would have gone through Gate #0/#2). Start paused
            // immediately instead of waiting for the next poll to notice.
            window_pause_recompute();
            break;

        case SYS_CYCLE_TIME:
            // Reset the cycle print count HERE, not when the next
            // INSPECTION_WINDOW opens - so the display shows 0 right away
            // once the mold starts cycling, instead of still showing the
            // previous window's final count until the next window begins.
            printed_count = 0;
            hmi.Write_UString((uint16_t)PRINT_COUNTER, String(0));
            Serial.println("STATE -> CYCLE_TIME (printing blocked)");
            hmi_condition_set(HMI_COND_ROUTINE, "Cycle Running", HMI_SEV_ROUTINE);
            break;
    }
}
