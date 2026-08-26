#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <Arduino.h>

// ---- Debug / diagnostics ----
// Defined in diagnostics.cpp.
extern bool verbose_logging;          // toggled via "verbose on"/"verbose off"
extern unsigned long boot_millis_ref; // set once in setup(), used for "uptime"
// --------------------------------------------------------------------------

#endif
