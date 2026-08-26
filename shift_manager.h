#ifndef SHIFT_MANAGER_H
#define SHIFT_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "config_manager.h"
#include "state_machine.h"
#include "storage_manager.h"
#include "hmi.h"

// Defined in shift_manager.cpp.
extern char current_shift;   // '\0' = not yet determined
extern uint16_t shift_counter;  // after boot 0

// ---- Shift stats accumulation (RAM, checkpointed to flash) ----
extern uint32_t shift_ok_total;
extern uint32_t shift_ng_total;
extern uint8_t  shift_rec_day;    // date the CURRENT shift started (per spec: a shift's
extern uint8_t  shift_rec_month;  // record uses its start date, even if it ends next
extern uint16_t shift_rec_year;   // calendar day, e.g. Shift B spanning midnight)
extern unsigned long last_shift_check_ms;
// --------------------------------------------------------------------------

char compute_shift(uint8_t hour, uint8_t minute);

void sync_month_stats_file(uint8_t month, uint16_t year);

// Writes one line to /stats/MMYY.csv : DD,shift,ok,ng
bool write_shift_stats(uint8_t day, uint8_t month, uint16_t year, char shift, uint32_t ok, uint32_t ng);

// Reads back the checkpoint file at boot. Returns true and fills the
// out-params if a valid checkpoint was found, false otherwise (e.g. first
// ever boot, or file missing/corrupt - either is treated as "nothing to
// recover", not an error).
bool read_checkpoint(uint8_t &day, uint8_t &month, uint16_t &year, char &shift,
                      uint32_t &ok, uint32_t &ng, uint16_t &counter);

// Called periodically (wall-clock driven, not scan-driven) so a shift
// boundary is never missed just because no scan happened near it. If a scan
// window is currently open, defers the rollover until it closes - see
// shift_manager.cpp for why.
void check_shift_rollover(char new_shift, uint8_t day, uint8_t month, uint16_t year);

// Closes the current INSPECTION_WINDOW: folds this window's OK/NG into the
// running shift totals, checkpoints, applies any deferred shift rollover,
// and transitions to CYCLE_TIME. Shared by two call sites that both need
// identical handling - the normal timer-based close in loop(), and an early
// close the moment printed_count reaches the cavity limit (production_manager.h).
void close_inspection_window();

#endif
