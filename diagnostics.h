#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

// ---- Debug / diagnostics ----
bool verbose_logging = false;      // toggled via "verbose on"/"verbose off"
unsigned long boot_millis_ref = 0; // set once in setup(), used for "uptime"
// --------------------------------------------------------------------------

#endif
