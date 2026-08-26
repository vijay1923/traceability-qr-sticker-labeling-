#ifndef SCANNER_MANAGER_H
#define SCANNER_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "hmi.h"
#include "state_machine.h"

// Defined in scanner_manager.cpp.
extern uint8_t lastConnectedState;

extern char extracted_data[20]; // extracted data
extern char barcode_data[50];   // scanned data

bool scanner_connected();

// Mirrors printer_manager.h's set_printer_fault() - same registry, same
// unified window-pause mechanism. Scanner disconnection can't directly block
// a print (no scan event can fire without a scanner), but the window
// countdown would otherwise keep burning down unattended while the operator
// physically cannot scan - pausing it here prevents that.
void set_scanner_fault(bool disconnected);

bool validate_raw_format(const char *data);
void data_extract(const char *data);

#endif
