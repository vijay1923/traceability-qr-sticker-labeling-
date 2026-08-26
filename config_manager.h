#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "hmi.h"

// ---- Runtime configuration (Configuration Manager) ----
// Loaded from NVS at boot (see load_config_from_nvs()); "config"/"setconfig"/
// "resetconfig" serial commands read and modify these live, no reflash needed.
// Defined in config_manager.cpp.
extern uint32_t g_cycle_time_s;
extern uint32_t g_inspection_window_s;
extern uint8_t  g_cavity_count;
extern uint16_t g_shift_a_min;  // shift A start, minutes-since-midnight (supports odd times, e.g. 07:30)
extern uint16_t g_shift_b_min;  // shift B start, minutes-since-midnight
extern Preferences cfg_prefs;
// --------------------------------------------------------------------------

void load_config_from_nvs();
void print_config();

// Returns true if `key` was recognized and applied+persisted, false otherwise
// (caller reports the error - keeps this function focused on the mapping).
bool set_config_value(const char *key, long value);

// Parses a scanned config barcode payload (everything after "CFG:").
// Format: comma-separated KEY:VALUE tokens, any subset/order accepted.
//   CVT  = cavity count (raw integer, e.g. CVT:8)
//   CYCT = cycle time in MINUTES (converted to seconds internally, e.g. CYCT:5)
//   INPT = inspection window in MINUTES (converted to seconds internally, e.g. INPT:1)
// Example full QR payload: "CFG:CVT:8,CYCT:5,INPT:1"
// A single field also works: "CFG:CYCT:5"
void parse_config_barcode(const char *payload);

void reset_config_to_defaults();

// Call periodically from loop() (alongside hmi_condition_service()) to
// advance the queued config-confirmation messages parse_config_barcode()
// builds - see config_manager.cpp for why this is a queue rather than a
// single hmi_condition_set() call.
void config_confirm_service();

#endif
